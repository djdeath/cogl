/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011, 2012, 2013 Intel Corporation.
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
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
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
#include "cogl-swap-chain-private.h"
#include "cogl-onscreen-template-private.h"
#include "cogl-context-private.h"
#include "cogl-onscreen-private.h"
#include "cogl-winsys-vulkan-wayland-private.h"
#include "cogl-error-private.h"
#include "cogl-poll-private.h"
#include "cogl-util-vulkan-private.h"

#define MAX_SWAP_CHAIN_LENGTH (6)

typedef struct _CoglRendererVulkanWayland
{
  CoglRendererVulkan parent;

  PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR
    get_wayland_presentation_support;
  PFN_vkCreateWaylandSurfaceKHR create_wayland_surface;

  struct wl_display *wayland_display;
  struct wl_compositor *wayland_compositor;
  struct wl_shell *wayland_shell;
  struct wl_registry *wayland_registry;
  int fd;
} CoglRendererVulkanWayland;

typedef struct _CoglOnscreenVulkanWayland
{
  CoglFramebufferVulkan parent;

  VkSwapchainKHR swap_chain;
  VkSurfaceKHR wsi_surface;

  uint32_t image_index;
  uint32_t image_count;
  VkImage images[MAX_SWAP_CHAIN_LENGTH];
  VkImageView image_views[MAX_SWAP_CHAIN_LENGTH];
  VkFramebuffer framebuffers[MAX_SWAP_CHAIN_LENGTH];

  struct wl_surface *wayland_surface;
  struct wl_shell_surface *wayland_shell_surface;

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

static CoglFuncPtr
_cogl_winsys_renderer_get_proc_address (CoglRenderer *renderer,
                                        const char *name,
                                        CoglBool in_core)
{
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;

  return vkGetInstanceProcAddr (vk_renderer->parent.instance, name);
}

static void
registry_handle_global_cb (void *data,
                           struct wl_registry *registry,
                           uint32_t id,
                           const char *interface,
                           uint32_t version)
{
  CoglRendererVulkanWayland *vk_renderer = data;

  if (strcmp (interface, "wl_compositor") == 0)
    vk_renderer->wayland_compositor =
      wl_registry_bind (registry, id, &wl_compositor_interface, 1);
  else if (strcmp(interface, "wl_shell") == 0)
    vk_renderer->wayland_shell =
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

  flush_ret = wl_display_flush (vk_renderer->wayland_display);

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
  wl_display_dispatch_pending (vk_renderer->wayland_display);

  return -1;
}

static void
dispatch_wayland_display_events (void *user_data, int revents)
{
  CoglRenderer *renderer = user_data;
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;

  if ((revents & COGL_POLL_FD_EVENT_IN))
    {
      if (wl_display_dispatch (vk_renderer->wayland_display) == -1 &&
          errno != EAGAIN &&
          errno != EINTR)
        goto socket_error;
    }

  if ((revents & COGL_POLL_FD_EVENT_OUT))
    {
      int ret = wl_display_flush (vk_renderer->wayland_display);

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

  _cogl_renderer_vulkan_deinit (renderer);

  if (vk_renderer->wayland_display)
    wl_display_disconnect (vk_renderer->wayland_display);

  g_slice_free (CoglRendererVulkanWayland, renderer->winsys);
  renderer->winsys = NULL;
}

static CoglBool
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               CoglError **error)
{
  CoglRendererVulkanWayland *vk_renderer =
    g_slice_new0 (CoglRendererVulkanWayland);

  renderer->winsys = vk_renderer;
  vk_renderer->wayland_display = wl_display_connect (NULL);
   if (!vk_renderer->wayland_display)
     {
       _cogl_set_error (error, COGL_WINSYS_ERROR,
                        COGL_WINSYS_ERROR_INIT,
                        "Failed to connect wayland display");
       goto error;
     }

  vk_renderer->wayland_registry =
    wl_display_get_registry (vk_renderer->wayland_display);

  wl_registry_add_listener (vk_renderer->wayland_registry,
                            &registry_listener,
                            vk_renderer);

  /*
   * Ensure that that we've received the messages setting up the
   * compostor and shell object.
   */
  wl_display_roundtrip (vk_renderer->wayland_display);
  if (!vk_renderer->wayland_compositor || !vk_renderer->wayland_shell)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_INIT,
                       "Unable to find wl_compositor or wl_shell");
      goto error;
    }

  vk_renderer->fd = wl_display_get_fd (vk_renderer->wayland_display);

  if (renderer->wayland_enable_event_dispatch)
    _cogl_poll_renderer_add_fd (renderer,
                                vk_renderer->fd,
                                COGL_POLL_FD_EVENT_IN,
                                prepare_wayland_display_events,
                                dispatch_wayland_display_events,
                                renderer);

  if (!_cogl_vulkan_renderer_init (renderer,
                                   VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
                                   error))
      goto error;

  vk_renderer->get_wayland_presentation_support =
    (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)
    vkGetInstanceProcAddr(vk_renderer->parent.instance,
                          "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
  vk_renderer->create_wayland_surface =
    (PFN_vkCreateWaylandSurfaceKHR)
    vkGetInstanceProcAddr(vk_renderer->parent.instance,
                          "vkCreateWaylandSurfaceKHR");
  if (!vk_renderer->get_wayland_presentation_support ||
      !vk_renderer->create_wayland_surface)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_INIT,
                       "Unable to find Vulkan Wayland extensions");
      goto error;
    }

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
  if (!_cogl_vulkan_context_init (context, error))
    return FALSE;

  if (!_cogl_context_update_features (context, error))
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
  CoglContextVulkan *vk_context = onscreen->_parent.context->winsys;
  CoglOnscreenVulkanWayland *vk_onscreen = onscreen->winsys;
  FrameCallbackData *frame_callback_data, *tmp;
  uint32_t i;

  _cogl_framebuffer_vulkan_deinit (COGL_FRAMEBUFFER (onscreen));

  for (i = 0; i < vk_onscreen->image_count; i++)
    {
      if (vk_onscreen->framebuffers[i] != VK_NULL_HANDLE)
        vkDestroyFramebuffer (vk_context->device, vk_onscreen->framebuffers[i], NULL);
      if (vk_onscreen->image_views[i] != VK_NULL_HANDLE)
        vkDestroyImageView (vk_context->device, vk_onscreen->image_views[i], NULL);
      if (vk_onscreen->images[i] != VK_NULL_HANDLE)
        vkDestroyImage (vk_context->device, vk_onscreen->images[i], NULL);
    }

  _cogl_list_for_each_safe (frame_callback_data,
                            tmp,
                            &vk_onscreen->frame_callbacks,
                            link)
    free_frame_callback_data (frame_callback_data);

  if (!onscreen->foreign_surface)
    {
      /* NB: The wayland protocol docs explicitly state that
       * "wl_shell_surface_destroy() must be called before destroying
       * the wl_surface object." ... */
      if (vk_onscreen->wayland_shell_surface)
        {
          wl_shell_surface_destroy (vk_onscreen->wayland_shell_surface);
          vk_onscreen->wayland_shell_surface = NULL;
        }

      if (vk_onscreen->wayland_surface)
        {
          wl_surface_destroy (vk_onscreen->wayland_surface);
          vk_onscreen->wayland_surface = NULL;
        }
    }

  g_slice_free (CoglOnscreenVulkanWayland, vk_onscreen);
  onscreen->winsys = NULL;
}

static CoglBool
_cogl_winsys_onscreen_init (CoglOnscreen *onscreen,
                            CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = framebuffer->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererVulkanWayland *vk_renderer = renderer->winsys;
  CoglContextVulkan *vk_context = context->winsys;
  CoglOnscreenVulkanWayland *vk_onscreen = onscreen->winsys;
  VkResult result;
  uint32_t i;

  vk_onscreen = g_slice_new0 (CoglOnscreenVulkanWayland);
  framebuffer->winsys = onscreen->winsys = vk_onscreen;

  _cogl_list_init (&vk_onscreen->frame_callbacks);

  if (onscreen->foreign_surface)
    vk_onscreen->wayland_surface = onscreen->foreign_surface;
  else
    vk_onscreen->wayland_surface =
      wl_compositor_create_surface (vk_renderer->wayland_compositor);

  if (!vk_onscreen->wayland_surface)
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Error while creating wayland surface for CoglOnscreen");
      goto error;
    }

  if (!onscreen->foreign_surface)
    vk_onscreen->wayland_shell_surface =
      wl_shell_get_shell_surface (vk_renderer->wayland_shell,
                                  vk_onscreen->wayland_surface);

  if (!vk_renderer->get_wayland_presentation_support (vk_context->physical_device,
                                                      0,
                                                      vk_renderer->wayland_display))
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Vulkan not supported on given Wayland surface");
      goto error;
    }

  result = vk_renderer->create_wayland_surface (vk_renderer->parent.instance, &(VkWaylandSurfaceCreateInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
      .display = vk_renderer->wayland_display,
      .surface = vk_onscreen->wayland_surface,
    },
    NULL,
    &vk_onscreen->wsi_surface);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Unable to create Vulkan Wayland surface : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  result = vkCreateSwapchainKHR (vk_context->device, &(VkSwapchainCreateInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = vk_onscreen->wsi_surface,
      .minImageCount = 2,
      .imageFormat = _cogl_pixel_format_to_vulkan_format (onscreen->_parent.internal_format, NULL),
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = { framebuffer->width, framebuffer->height },
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = (uint32_t[]) { 0 },
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    },
    NULL,
    &vk_onscreen->swap_chain);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Cannot create Vulkan Swapchain");
      goto error;
    }

  vkGetSwapchainImagesKHR (vk_context->device, vk_onscreen->swap_chain,
                           &vk_onscreen->image_count, NULL);
  g_assert (vk_onscreen->image_count <= MAX_SWAP_CHAIN_LENGTH);

  vkGetSwapchainImagesKHR (vk_context->device,
                           vk_onscreen->swap_chain,
                           &vk_onscreen->image_count,
                           vk_onscreen->images);

  for (i = 0; i < vk_onscreen->image_count; i++)
    {
      result = vkCreateImageView (vk_context->device, &(VkImageViewCreateInfo) {
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk_onscreen->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .components = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A,
          },
            .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
          },
               },
        NULL,
        &vk_onscreen->image_views[i]);
      if (result != VK_SUCCESS)
        {
          _cogl_set_error (error,
                           COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                           "Cannot create image view : %s",
                           _cogl_vulkan_error_to_string (result));
          goto error;
        }

      result = vkCreateFramebuffer (vk_context->device, &(VkFramebufferCreateInfo) {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .attachmentCount = 1,
          .pAttachments = &vk_onscreen->image_views[i],
          .width = framebuffer->width,
          .height = framebuffer->height,
          .layers = 1
        },
        NULL,
        &vk_onscreen->framebuffers[i]);
      if (result != VK_SUCCESS)
        {
          _cogl_set_error (error,
                           COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                           "Cannot create framebuffer : %s",
                           _cogl_vulkan_error_to_string (result));
          goto error;
        }
    }

  if (!_cogl_framebuffer_vulkan_init (framebuffer, error))
    goto error;

  result = vkAcquireNextImageKHR (vk_context->device, vk_onscreen->swap_chain, 0,
                                  VK_NULL_HANDLE, VK_NULL_HANDLE,
                                  &vk_onscreen->image_index);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Cannot acquire first image : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  _cogl_framebuffer_vulkan_update_framebuffer (framebuffer,
                                               vk_onscreen->framebuffers[vk_onscreen->image_index]);

  return TRUE;

 error:
  _cogl_winsys_onscreen_deinit (onscreen);
  return FALSE;
}

static void
flush_pending_resize (CoglOnscreen *onscreen)
{
  CoglOnscreenVulkanWayland *vk_onscreen = onscreen->winsys;

  if (vk_onscreen->has_pending)
    {
      _cogl_framebuffer_winsys_update_size (COGL_FRAMEBUFFER (onscreen),
                                            vk_onscreen->pending_width,
                                            vk_onscreen->pending_height);

      _cogl_onscreen_queue_full_dirty (onscreen);

      vk_onscreen->pending_dx = 0;
      vk_onscreen->pending_dy = 0;
      vk_onscreen->has_pending = FALSE;
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
  CoglContextVulkan *vk_ctx = onscreen->_parent.context->winsys;
  CoglOnscreenVulkanWayland *vk_onscreen = onscreen->winsys;
  FrameCallbackData *frame_callback_data = g_slice_new (FrameCallbackData);
  VkResult result;

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
    wl_surface_frame (vk_onscreen->wayland_surface);
  wl_callback_add_listener (frame_callback_data->callback,
                            &frame_listener,
                            frame_callback_data);

  _cogl_list_insert (&vk_onscreen->frame_callbacks,
                     &frame_callback_data->link);

  _cogl_framebuffer_flush_state (COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER_STATE_BIND);

  vkQueuePresentKHR (vk_ctx->queue, &(VkPresentInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .swapchainCount = 1,
      .pSwapchains = (VkSwapchainKHR[]) { vk_onscreen->swap_chain, },
      .pImageIndices = (uint32_t[]) { vk_onscreen->image_index, },
      .pResults = &result,
    });
  if (result != VK_SUCCESS)
    {
      g_warning ("Could not present wayland frame : %s",
                 _cogl_vulkan_error_to_string (result));
      return;
    }

  result = vkAcquireNextImageKHR (vk_ctx->device,
                                  vk_onscreen->swap_chain, 0,
                                  VK_NULL_HANDLE, VK_NULL_HANDLE,
                                  &vk_onscreen->image_index);
  if (result != VK_SUCCESS)
    {
      g_warning ("Cannot acquire first image : %s",
                 _cogl_vulkan_error_to_string (result));
      return;
    }
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
  CoglOnscreenVulkanWayland *vk_onscreen = onscreen->winsys;

  /* The first time the onscreen is shown we will set it to toplevel
   * so that it will appear on the screen. If the surface is foreign
   * then we won't have the shell surface and we'll just let the
   * application deal with setting the surface type. */
  if (visibility &&
      vk_onscreen->wayland_shell_surface &&
      !vk_onscreen->shell_surface_type_set)
    {
      wl_shell_surface_set_toplevel (vk_onscreen->wayland_shell_surface);
      vk_onscreen->shell_surface_type_set = TRUE;
      _cogl_onscreen_queue_full_dirty (onscreen);
    }

  /* FIXME: We should also do something here to hide the surface when
   * visilibity == FALSE. It sounds like there are currently ongoing
   * discussions about adding support for hiding surfaces in the
   * Wayland protocol so we might as well wait until then to add that
   * here. */
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

      vtable.renderer_get_proc_address = _cogl_winsys_renderer_get_proc_address;
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

      vtable_inited = TRUE;
    }

  return &vtable;
}
