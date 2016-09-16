/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2016 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>

#include <wayland-client.h>

#include "cogl-renderer-private.h"
#include "cogl-display-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-onscreen-template-private.h"
#include "cogl-context-private.h"
#include "cogl-onscreen-private.h"
#include "cogl-winsys-vulkan-wayland-private.h"
#include "cogl-error-private.h"
#include "cogl-poll-private.h"
#include "cogl-util-vulkan-private.h"

typedef struct _CoglRendererVulkanWayland
{
  CoglRendererVulkan parent;

  struct wl_compositor *wayland_compositor;
  struct wl_registry *wayland_registry;
  int fd;
} CoglRendererVulkanWayland;

typedef struct _CoglOnscreenVulkanWayland
{
  CoglOnscreenVulkan parent;

  /* Resizing a wayland framebuffer doesn't take affect
   * until the next swap buffers request, so we have to
   * track the resize geometry until then... */
  int pending_width;
  int pending_height;
  int pending_dx;
  int pending_dy;
  CoglBool has_pending;

  CoglBool shell_surface_type_set;

  CoglList frame_callbacks;
} CoglOnscreenVulkanWayland;

typedef struct
{
  CoglList link;
  CoglFrameInfo *frame_info;
  struct wl_callback *callback;
  CoglOnscreen *onscreen;
} FrameCallbackData;

static void
registry_handle_global_cb (void *data,
                           struct wl_registry *registry,
                           uint32_t id,
                           const char *interface,
                           uint32_t version)
{
  CoglRenderer *renderer = data;
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;

  if (strcmp (interface, "wl_compositor") == 0)
    vk_renderer->wayland_compositor =
      wl_registry_bind (registry, id, &wl_compositor_interface, 1);
  else if (strcmp(interface, "wl_shell") == 0)
    renderer->wayland_shell =
      wl_registry_bind (registry, id, &wl_shell_interface, 1);
}

static void
registry_handle_global_remove_cb (void *data,
                                  struct wl_registry *registry,
                                  uint32_t name)
{
  /* Nothing to do for now */
}

static const struct wl_registry_listener registry_listener = {
  registry_handle_global_cb,
  registry_handle_global_remove_cb
};

static int64_t
prepare_wayland_display_events (void *user_data)
{
  CoglRenderer *renderer = user_data;
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;
  int flush_ret;

  flush_ret = wl_display_flush (renderer->wayland_display);

  if (flush_ret == -1)
    {
      /* If the socket buffer became full then we need to wake up the
       * main loop once it is writable again */
      if (errno == EAGAIN)
        {
          _cogl_poll_renderer_modify_fd (renderer,
                                         vk_renderer->fd,
                                         COGL_POLL_FD_EVENT_IN |
                                         COGL_POLL_FD_EVENT_OUT);
        }
      else if (errno != EINTR)
        {
          /* If the flush failed for some other reason then it's
           * likely that it's going to consistently fail so we'll stop
           * waiting on the file descriptor instead of making the
           * application take up 100% CPU. FIXME: it would be nice if
           * there was some way to report this to the application so
           * that it can quit or recover */
          _cogl_poll_renderer_remove_fd (renderer, vk_renderer->fd);
        }
    }

  /* Calling this here is a bit dodgy because Cogl usually tries to
   * say that it won't do any event processing until
   * cogl_poll_renderer_dispatch is called. However Wayland doesn't
   * seem to provide any way to query whether the event queue is empty
   * and we would need to do that in order to force the main loop to
   * wake up to call it from dispatch. */
  wl_display_dispatch_pending (renderer->wayland_display);

  return -1;
}

static void
dispatch_wayland_display_events (void *user_data, int revents)
{
  CoglRenderer *renderer = user_data;
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;

  if ((revents & COGL_POLL_FD_EVENT_IN))
    {
      if (wl_display_dispatch (renderer->wayland_display) == -1 &&
          errno != EAGAIN &&
          errno != EINTR)
        goto socket_error;
    }

  if ((revents & COGL_POLL_FD_EVENT_OUT))
    {
      int ret = wl_display_flush (renderer->wayland_display);

      if (ret == -1)
        {
          if (errno != EAGAIN && errno != EINTR)
            goto socket_error;
        }
      else
        {
          /* There is no more data to write so we don't need to wake
           * up when the write buffer is emptied anymore */
          _cogl_poll_renderer_modify_fd (renderer,
                                         vk_renderer->fd,
                                         COGL_POLL_FD_EVENT_IN);
        }
    }

  return;

 socket_error:
  /* If there was an error on the wayland socket then it's likely that
   * it's going to consistently fail so we'll stop waiting on the file
   * descriptor instead of making the application take up 100% CPU.
   * FIXME: it would be nice if there was some way to report this to
   * the application so that it can quit or recover */
  _cogl_poll_renderer_remove_fd (renderer, vk_renderer->fd);
}

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;

  _cogl_vulkan_renderer_deinit (renderer);

  if (renderer->wayland_display)
    {
      _cogl_poll_renderer_remove_fd (renderer, vk_renderer->fd);

      if (renderer->foreign_wayland_display)
        wl_display_disconnect (renderer->wayland_display);
    }

  g_slice_free (CoglRendererVulkanWayland, renderer->winsys);
  renderer->winsys = NULL;
}

static CoglBool
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               CoglError **error)
{
  CoglRendererVulkanWayland *vk_renderer_wl =
    g_slice_new0 (CoglRendererVulkanWayland);
  CoglRendererVulkan *vk_renderer =
    (CoglRendererVulkan *) vk_renderer_wl;
  static const char *instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
  };
  static const char *device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

  renderer->winsys = vk_renderer;

  if (renderer->foreign_wayland_display)
    {
      renderer->wayland_display = renderer->foreign_wayland_display;
    }
  else
    {
      renderer->wayland_display = wl_display_connect (NULL);
      if (!renderer->wayland_display)
        {
          _cogl_set_error (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "Failed to connect wayland display");
          goto error;
        }
    }

  vk_renderer_wl->wayland_registry =
    wl_display_get_registry (renderer->wayland_display);

  wl_registry_add_listener (vk_renderer_wl->wayland_registry,
                            &registry_listener,
                            renderer);

  /*
   * Ensure that that we've received the messages setting up the
   * compostor and shell object.
   */
  wl_display_roundtrip (renderer->wayland_display);
  if (!vk_renderer_wl->wayland_compositor || !renderer->wayland_shell)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_INIT,
                       "Unable to find wl_compositor or wl_shell");
      goto error;
    }

  vk_renderer_wl->fd = wl_display_get_fd (renderer->wayland_display);

  if (renderer->wayland_enable_event_dispatch)
    _cogl_poll_renderer_add_fd (renderer,
                                vk_renderer_wl->fd,
                                COGL_POLL_FD_EVENT_IN,
                                prepare_wayland_display_events,
                                dispatch_wayland_display_events,
                                renderer);

  if (!_cogl_vulkan_renderer_init (renderer,
                                   instance_extensions,
                                   G_N_ELEMENTS(instance_extensions),
                                   device_extensions,
                                   G_N_ELEMENTS(device_extensions),
                                   error))
      goto error;

  return TRUE;

 error:
  _cogl_winsys_renderer_disconnect (renderer);
  return FALSE;
}

static void
_cogl_winsys_display_destroy (CoglDisplay *display)
{
}

static CoglBool
_cogl_winsys_display_setup (CoglDisplay *display,
                            CoglError **error)
{
  return TRUE;
}

static CoglBool
_cogl_winsys_context_init (CoglContext *context, CoglError **error)
{
  if (!_cogl_context_update_features (context, error))
    return FALSE;

  if (!context->vkCreateWaylandSurfaceKHR ||
      !context->vkGetPhysicalDeviceWaylandPresentationSupportKHR)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_INIT,
                       "Unable to find Vulkan Wayland extensions");
      return FALSE;
    }

  if (!_cogl_vulkan_context_init (context, error))
    return FALSE;

  context->feature_flags |= COGL_FEATURE_ONSCREEN_MULTIPLE;
  COGL_FLAGS_SET (context->features,
                  COGL_FEATURE_ID_ONSCREEN_MULTIPLE, TRUE);
  COGL_FLAGS_SET (context->winsys_features,
                  COGL_WINSYS_FEATURE_MULTIPLE_ONSCREEN,
                  TRUE);
  COGL_FLAGS_SET (context->winsys_features,
                  COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT,
                  TRUE);

  return TRUE;
}

static void
_cogl_winsys_context_deinit (CoglContext *context)
{
  _cogl_vulkan_context_deinit (context);
}

static void
_cogl_winsys_onscreen_bind (CoglOnscreen *onscreen)
{
}

static void
free_frame_callback_data (FrameCallbackData *callback_data)
{
  cogl_object_unref (callback_data->frame_info);
  wl_callback_destroy (callback_data->callback);
  _cogl_list_remove (&callback_data->link);
  g_slice_free (FrameCallbackData, callback_data);
}

static void
_cogl_winsys_onscreen_deinit (CoglOnscreen *onscreen)
{
  CoglOnscreenVulkanWayland *vk_onscreen_wl = onscreen->winsys;
  FrameCallbackData *frame_callback_data, *tmp;

  _cogl_list_for_each_safe (frame_callback_data,
                            tmp,
                            &vk_onscreen_wl->frame_callbacks,
                            link)
    free_frame_callback_data (frame_callback_data);

  if (!onscreen->wayland.foreign_surface)
    {
      /* NB: The wayland protocol docs explicitly state that
       * "wl_shell_surface_destroy() must be called before destroying
       * the wl_surface object." ... */
      if (onscreen->wayland.shell_surface)
        {
          wl_shell_surface_destroy (onscreen->wayland.shell_surface);
          onscreen->wayland.shell_surface = NULL;
        }

      if (onscreen->wayland.surface)
        {
          wl_surface_destroy (onscreen->wayland.surface);
          onscreen->wayland.surface = NULL;
        }
    }

  g_slice_free (CoglOnscreenVulkanWayland, vk_onscreen_wl);
  onscreen->winsys = NULL;
}

static CoglBool
_cogl_winsys_onscreen_init (CoglOnscreen *onscreen,
                            CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = framebuffer->context;
  CoglRenderer *renderer = ctx->display->renderer;
  CoglRendererVulkan *vk_renderer = renderer->winsys;
  CoglRendererVulkanWayland *vk_renderer_wl = renderer->winsys;
  CoglOnscreenVulkan *vk_onscreen;
  CoglOnscreenVulkanWayland *vk_onscreen_wl;

  vk_onscreen_wl = g_slice_new0 (CoglOnscreenVulkanWayland);
  framebuffer->winsys = onscreen->winsys =
    vk_onscreen = (CoglOnscreenVulkan *) vk_onscreen_wl;

  _cogl_list_init (&vk_onscreen_wl->frame_callbacks);

  if (onscreen->wayland.foreign_surface)
    onscreen->wayland.surface = onscreen->wayland.foreign_surface;
  else
    onscreen->wayland.surface =
      wl_compositor_create_surface (vk_renderer_wl->wayland_compositor);

  if (!onscreen->wayland.surface)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Error while creating wayland surface for CoglOnscreen");
      goto error;
    }

  if (!onscreen->wayland.foreign_surface)
    onscreen->wayland.shell_surface =
      wl_shell_get_shell_surface (renderer->wayland_shell,
                                  onscreen->wayland.surface);

  if (!VK (ctx,
           vkGetPhysicalDeviceWaylandPresentationSupportKHR (vk_renderer->physical_device,
                                                             0,
                                                             renderer->wayland_display)))
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Cannot get wayland presentation support");
      goto error;
    }

  VK_ERROR ( ctx,
             vkCreateWaylandSurfaceKHR (vk_renderer->instance, &(VkWaylandSurfaceCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = renderer->wayland_display,
        .surface = onscreen->wayland.surface,
      },
      NULL,
      &vk_onscreen->wsi_surface),
    error, COGL_WINSYS_ERROR, COGL_WINSYS_ERROR_CREATE_ONSCREEN );

  if (!_cogl_onscreen_vulkan_init (onscreen, error))
    goto error;

  return TRUE;

 error:
  _cogl_winsys_onscreen_deinit (onscreen);
  return FALSE;
}

static void
flush_pending_resize (CoglOnscreen *onscreen)
{
  CoglOnscreenVulkanWayland *vk_onscreen_wl = onscreen->winsys;

  if (vk_onscreen_wl->has_pending)
    {
      CoglError *error = NULL;

      _cogl_onscreen_vulkan_deinit (onscreen);

      _cogl_framebuffer_winsys_update_size (COGL_FRAMEBUFFER (onscreen),
                                            vk_onscreen_wl->pending_width,
                                            vk_onscreen_wl->pending_height);

      if (!_cogl_onscreen_vulkan_init (onscreen, &error)) {
        g_warning ("Failed to resize: %s", error->message);
        cogl_error_free (error);
      }

      _cogl_onscreen_queue_full_dirty (onscreen);

      vk_onscreen_wl->pending_dx = 0;
      vk_onscreen_wl->pending_dy = 0;
      vk_onscreen_wl->has_pending = FALSE;
    }
}

static void
frame_cb (void *data,
          struct wl_callback *callback,
          uint32_t time)
{
  FrameCallbackData *callback_data = data;
  CoglFrameInfo *info = callback_data->frame_info;
  CoglOnscreen *onscreen = callback_data->onscreen;

  g_assert (callback_data->callback == callback);

  _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_SYNC, info);
  _cogl_onscreen_queue_event (onscreen, COGL_FRAME_EVENT_COMPLETE, info);

  free_frame_callback_data (callback_data);
}

static const struct wl_callback_listener
frame_listener =
{
  frame_cb
};

static void
_cogl_winsys_onscreen_swap_buffers_with_damage (CoglOnscreen *onscreen,
                                                const int *rectangles,
                                                int n_rectangles)
{
  CoglOnscreenVulkanWayland *vk_onscreen_wl = onscreen->winsys;
  FrameCallbackData *frame_callback_data = g_slice_new (FrameCallbackData);

  flush_pending_resize (onscreen);

  /* Before calling the winsys function,
   * cogl_onscreen_swap_buffers_with_damage() will have pushed the
   * frame info object onto the end of the pending frames. We can grab
   * it out of the queue now because we don't care about the order and
   * we will just directly queue the event corresponding to the exact
   * frame that Wayland reports as completed. This will steal the
   * reference */
  frame_callback_data->frame_info =
    g_queue_pop_tail (&onscreen->pending_frame_infos);
  frame_callback_data->onscreen = onscreen;

  frame_callback_data->callback =
    wl_surface_frame (onscreen->wayland.surface);
  wl_callback_add_listener (frame_callback_data->callback,
                            &frame_listener,
                            frame_callback_data);

  _cogl_list_insert (&vk_onscreen_wl->frame_callbacks,
                     &frame_callback_data->link);

  _cogl_onscreen_vulkan_swap_buffers_with_damage (onscreen, rectangles,
                                                  n_rectangles);
}

static void
_cogl_winsys_onscreen_update_swap_throttled (CoglOnscreen *onscreen)
{
  VK_TODO();
}

static void
_cogl_winsys_onscreen_set_visibility (CoglOnscreen *onscreen,
                                      CoglBool visibility)
{
  /* The first time the onscreen is shown we will set it to toplevel
   * so that it will appear on the screen. If the surface is foreign
   * then we won't have the shell surface and we'll just let the
   * application deal with setting the surface type. */
  if (visibility &&
      onscreen->wayland.shell_surface &&
      !onscreen->wayland.shell_surface_type_set)
    {
      wl_shell_surface_set_toplevel (onscreen->wayland.shell_surface);
      onscreen->wayland.shell_surface_type_set = TRUE;
      _cogl_onscreen_queue_full_dirty (onscreen);
    }

  /* FIXME: We should also do something here to hide the surface when
   * visilibity == FALSE. It sounds like there are currently ongoing
   * discussions about adding support for hiding surfaces in the
   * Wayland protocol so we might as well wait until then to add that
   * here. */
}

static void
_cogl_winsys_onscreen_resize (CoglOnscreen *onscreen,
                              int           width,
                              int           height,
                              int           offset_x,
                              int           offset_y)
{
  CoglFramebuffer *fb = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenVulkanWayland *vk_onscreen_wl = onscreen->winsys;

  if (cogl_framebuffer_get_width (fb) != width ||
      cogl_framebuffer_get_height (fb) != height ||
      offset_x ||
      offset_y)
    {
      vk_onscreen_wl->pending_width = width;
      vk_onscreen_wl->pending_height = height;
      vk_onscreen_wl->pending_dx += offset_x;
      vk_onscreen_wl->pending_dy += offset_y;
      vk_onscreen_wl->has_pending = TRUE;

      /* If nothing has been drawn to the framebuffer since the last swap
       * then wl_egl_window_resize will take effect immediately. Otherwise
       * it might not take effect until the next swap, depending on the
       * version of Mesa. To keep consistent behaviour we'll delay the
       * resize until the next swap unless we're sure nothing has been
       * drawn */
      if (!fb->mid_scene)
        flush_pending_resize (onscreen);
    }
}

const CoglWinsysVtable *
_cogl_winsys_vulkan_wayland_get_vtable (void)
{
  static CoglBool vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      memset (&vtable, 0, sizeof (vtable));

      vtable.id = COGL_WINSYS_ID_VULKAN_WAYLAND;
      vtable.name = "VULKAN_WAYLAND";
      vtable.constraints = COGL_RENDERER_CONSTRAINT_USES_VULKAN;

      vtable.renderer_get_proc_address = _cogl_vulkan_renderer_get_proc_address;
      vtable.renderer_connect = _cogl_winsys_renderer_connect;
      vtable.renderer_disconnect = _cogl_winsys_renderer_disconnect;
      vtable.display_setup = _cogl_winsys_display_setup;
      vtable.display_destroy = _cogl_winsys_display_destroy;
      vtable.context_init = _cogl_winsys_context_init;
      vtable.context_deinit = _cogl_winsys_context_deinit;
      vtable.onscreen_init = _cogl_winsys_onscreen_init;
      vtable.onscreen_deinit = _cogl_winsys_onscreen_deinit;
      vtable.onscreen_bind = _cogl_winsys_onscreen_bind;
      vtable.onscreen_swap_buffers_with_damage =
        _cogl_winsys_onscreen_swap_buffers_with_damage;
      vtable.onscreen_update_swap_throttled =
        _cogl_winsys_onscreen_update_swap_throttled;
      vtable.onscreen_set_visibility = _cogl_winsys_onscreen_set_visibility;

      vtable.wayland_onscreen_resize = _cogl_winsys_onscreen_resize;

      vtable_inited = TRUE;
    }

  return &vtable;
}
