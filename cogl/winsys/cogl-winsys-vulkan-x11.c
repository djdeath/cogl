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

#include <X11/Xlib.h>

#include <string.h>

#include "cogl-renderer-private.h"
#include "cogl-display-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-onscreen-template-private.h"
#include "cogl-context-private.h"
#include "cogl-onscreen-private.h"
#include "cogl-winsys-vulkan-x11-private.h"
#include "cogl-error-private.h"
#include "cogl-poll-private.h"
#include "cogl-util-vulkan-private.h"
#include "cogl-xlib-renderer-private.h"
#include "cogl-xlib-renderer.h"

#define COGL_ONSCREEN_X11_EVENT_MASK (StructureNotifyMask | ExposureMask)

typedef struct _CoglRendererVulkanX11
{
  CoglRendererVulkan parent;

  CoglClosure *resize_notify_idle;
} CoglRendererVulkanX11;

typedef struct _CoglOnscreenVulkanX11
{
  CoglOnscreenVulkan parent;

  Window xwin;

  CoglBool pending_resize_notify;
} CoglOnscreenVulkanX11;

static CoglOnscreen *
find_onscreen_for_xid (CoglContext *context, uint32_t xid)
{
  GList *l;

  for (l = context->framebuffers; l; l = l->next)
    {
      CoglFramebuffer *framebuffer = l->data;
      CoglOnscreenVulkanX11 *vk_onscreen_x11;

      if (!framebuffer->type == COGL_FRAMEBUFFER_TYPE_ONSCREEN)
        continue;

      vk_onscreen_x11 = COGL_ONSCREEN (framebuffer)->winsys;
      if (vk_onscreen_x11->xwin == (Window)xid)
        return COGL_ONSCREEN (framebuffer);
    }

  return NULL;
}

static void
flush_pending_resize_notifications_cb (void *data,
                                       void *user_data)
{
  CoglFramebuffer *framebuffer = data;

  if (framebuffer->type == COGL_FRAMEBUFFER_TYPE_ONSCREEN)
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);
      CoglOnscreenVulkanX11 *vk_onscreen_x11 = onscreen->winsys;

      if (vk_onscreen_x11->pending_resize_notify)
        {
          _cogl_onscreen_notify_resize (onscreen);
          vk_onscreen_x11->pending_resize_notify = FALSE;
        }
    }
}

static void
flush_pending_resize_notifications_idle (void *user_data)
{
  CoglContext *context = user_data;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererVulkanX11 *vk_renderer = renderer->winsys;

  /* This needs to be disconnected before invoking the callbacks in
   * case the callbacks cause it to be queued again */
  _cogl_closure_disconnect (vk_renderer->resize_notify_idle);
  vk_renderer->resize_notify_idle = NULL;

  g_list_foreach (context->framebuffers,
                  flush_pending_resize_notifications_cb,
                  NULL);
}

static void
notify_resize (CoglContext *context,
               Window drawable,
               int width,
               int height)
{
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererVulkanX11 *vk_renderer = renderer->winsys;
  CoglOnscreen *onscreen = find_onscreen_for_xid (context, drawable);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenVulkanX11 *vk_onscreen_x11;
  CoglError *error = NULL;

  if (!onscreen)
    return;

  vk_onscreen_x11 = onscreen->winsys;

  _cogl_onscreen_vulkan_deinit (onscreen);

  _cogl_framebuffer_winsys_update_size (framebuffer, width, height);

  if (!_cogl_onscreen_vulkan_init (onscreen, &error))
    {
      g_warning ("Resize failed: %s", error->message);
      cogl_error_free (error);
    }

  /* We only want to notify that a resize happened when the
   * application calls cogl_context_dispatch so instead of immediately
   * notifying we queue an idle callback */
  if (!vk_renderer->resize_notify_idle)
    {
      vk_renderer->resize_notify_idle =
        _cogl_poll_renderer_add_idle (renderer,
                                      flush_pending_resize_notifications_idle,
                                      context,
                                      NULL);
    }

  vk_onscreen_x11->pending_resize_notify = TRUE;
}

static CoglFilterReturn
event_filter_cb (XEvent *xevent, void *data)
{
  CoglContext *context = data;

  if (xevent->type == ConfigureNotify)
    {
      notify_resize (context,
                     xevent->xconfigure.window,
                     xevent->xconfigure.width,
                     xevent->xconfigure.height);
    }
  else if (xevent->type == Expose)
    {
      CoglOnscreen *onscreen =
        find_onscreen_for_xid (context, xevent->xexpose.window);

      if (onscreen)
        {
          CoglOnscreenDirtyInfo info;

          info.x = xevent->xexpose.x;
          info.y = xevent->xexpose.y;
          info.width = xevent->xexpose.width;
          info.height = xevent->xexpose.height;

          _cogl_onscreen_queue_dirty (onscreen, &info);
        }
    }

  return COGL_FILTER_CONTINUE;
}

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  _cogl_vulkan_renderer_deinit (renderer);

  _cogl_xlib_renderer_disconnect (renderer);

  g_slice_free (CoglRendererVulkanX11, renderer->winsys);
  renderer->winsys = NULL;
}

static CoglBool
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               CoglError **error)
{
  CoglRendererVulkanX11 *vk_renderer_x11 =
    g_slice_new0 (CoglRendererVulkanX11);
  static const char *instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME
  };
  static const char *device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

  renderer->winsys = vk_renderer_x11;

  if (!_cogl_xlib_renderer_connect (renderer, error))
    goto error;

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

  if (!context->vkCreateXlibSurfaceKHR ||
      !context->vkGetPhysicalDeviceXlibPresentationSupportKHR)
    {
      _cogl_set_error (error,
                       COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_INIT,
                       "Unable to find Vulkan X11 extensions");
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

  cogl_xlib_renderer_add_filter (context->display->renderer,
                                 event_filter_cb,
                                 context);

  return TRUE;
}

static void
_cogl_winsys_context_deinit (CoglContext *context)
{
  cogl_xlib_renderer_remove_filter (context->display->renderer,
                                    event_filter_cb,
                                    context);
  _cogl_vulkan_context_deinit (context);
}

static void
_cogl_winsys_onscreen_bind (CoglOnscreen *onscreen)
{
}

static void
_cogl_winsys_onscreen_deinit (CoglOnscreen *onscreen)
{
  CoglContext *ctx = onscreen->_parent.context;
  CoglRenderer *renderer = ctx->display->renderer;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglOnscreenVulkanX11 *vk_onscreen_x11 = onscreen->winsys;
  CoglXlibTrapState old_state;

  _cogl_framebuffer_vulkan_update_framebuffer (framebuffer,
                                               VK_NULL_HANDLE, VK_NULL_HANDLE);
  _cogl_framebuffer_vulkan_deinit (framebuffer);

  _cogl_xlib_renderer_trap_errors (renderer, &old_state);

  if (!onscreen->foreign_xid && vk_onscreen_x11->xwin != None)
    {
      XDestroyWindow (xlib_renderer->xdpy, vk_onscreen_x11->xwin);
      vk_onscreen_x11->xwin = None;
    }
  else
    vk_onscreen_x11->xwin = None;

  XSync (xlib_renderer->xdpy, False);

  if (_cogl_xlib_renderer_untrap_errors (renderer,
                                         &old_state) != Success)
    g_warning ("X Error while destroying X window");

  g_slice_free (CoglOnscreenVulkanX11, vk_onscreen_x11);
  onscreen->winsys = NULL;
}

static XVisualInfo *
get_visual_info (CoglDisplay *display)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  XVisualInfo visinfo_template;
  int template_mask = 0;
  XVisualInfo *visinfo = NULL;
  int visinfos_count;

  visinfo_template.visualid =
    DefaultVisual (xlib_renderer->xdpy,
                   DefaultScreen (xlib_renderer->xdpy))->visualid;
  template_mask |= VisualIDMask;

  visinfo = XGetVisualInfo (xlib_renderer->xdpy,
                            template_mask,
                            &visinfo_template,
                            &visinfos_count);

  return visinfo;
}

static CoglBool
_cogl_winsys_onscreen_init (CoglOnscreen *onscreen,
                            CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = framebuffer->context;
  CoglDisplay *display = ctx->display;
  CoglRenderer *renderer = display->renderer;
  CoglRendererVulkan *vk_renderer = renderer->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  CoglOnscreenVulkan *vk_onscreen;
  CoglOnscreenVulkanX11 *vk_onscreen_x11;
  Window xwin;

  vk_onscreen_x11 = g_slice_new0 (CoglOnscreenVulkanX11);
  framebuffer->winsys = onscreen->winsys =
    vk_onscreen = (CoglOnscreenVulkan *) vk_onscreen_x11;

  if (onscreen->foreign_xid)
    {
      Status status;
      CoglXlibTrapState state;
      XWindowAttributes attr;
      int xerror;

      xwin = onscreen->foreign_xid;

      _cogl_xlib_renderer_trap_errors (display->renderer, &state);

      status = XGetWindowAttributes (xlib_renderer->xdpy, xwin, &attr);
      xerror = _cogl_xlib_renderer_untrap_errors (display->renderer,
                                                  &state);
      if (status == 0 || xerror)
        {
          char message[1000];
          XGetErrorText (xlib_renderer->xdpy, xerror,
                         message, sizeof (message));
          _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Unable to query geometry of foreign "
                       "xid 0x%08lX: %s",
                       xwin, message);
          return FALSE;
        }

      _cogl_framebuffer_winsys_update_size (framebuffer,
                                            attr.width, attr.height);

      /* Make sure the app selects for the events we require... */
      onscreen->foreign_update_mask_callback (onscreen,
                                              COGL_ONSCREEN_X11_EVENT_MASK,
                                              onscreen->
                                              foreign_update_mask_data);
    }
  else
    {
      int width;
      int height;
      CoglXlibTrapState state;
      XVisualInfo *xvisinfo;
      XSetWindowAttributes xattr;
      unsigned long mask;
      int xerror;

      width = cogl_framebuffer_get_width (framebuffer);
      height = cogl_framebuffer_get_height (framebuffer);

      _cogl_xlib_renderer_trap_errors (display->renderer, &state);

      xvisinfo = get_visual_info (display);
      if (xvisinfo == NULL)
        {
          _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Unable to retrieve the X11 visual of context's "
                       "fbconfig");
          return FALSE;
        }

      /* window attributes */
      xattr.background_pixel =
        WhitePixel (xlib_renderer->xdpy,
                    DefaultScreen (xlib_renderer->xdpy));
      xattr.border_pixel = 0;
      /* XXX: is this an X resource that we are leaking‽... */
      xattr.colormap =
        XCreateColormap (xlib_renderer->xdpy,
                         DefaultRootWindow (xlib_renderer->xdpy),
                         xvisinfo->visual,
                         AllocNone);
      xattr.event_mask = COGL_ONSCREEN_X11_EVENT_MASK;

      mask = CWBorderPixel | CWColormap | CWEventMask;

      xwin = XCreateWindow (xlib_renderer->xdpy,
                            DefaultRootWindow (xlib_renderer->xdpy),
                            0, 0,
                            width, height,
                            0,
                            xvisinfo->depth,
                            InputOutput,
                            xvisinfo->visual,
                            mask, &xattr);

      XFree (xvisinfo);

      XSync (xlib_renderer->xdpy, False);
      xerror =
        _cogl_xlib_renderer_untrap_errors (display->renderer, &state);
      if (xerror)
        {
          char message[1000];
          XGetErrorText (xlib_renderer->xdpy, xerror,
                         message, sizeof (message));
          _cogl_set_error (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                           "X error while creating Window for CoglOnscreen: %s",
                           message);
          return FALSE;
        }
    }

  vk_onscreen_x11->xwin = xwin;

  if (!VK (ctx,
           vkGetPhysicalDeviceXlibPresentationSupportKHR (vk_renderer->physical_device,
                                                          0,
                                                          xlib_renderer->xdpy,
                                                          DefaultVisual (xlib_renderer->xdpy, DefaultScreen(xlib_renderer->xdpy))->visualid)))
    {
      _cogl_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "Cannot get x11 presentation support");
      goto error;
    }

  VK_ERROR ( ctx,
             vkCreateXlibSurfaceKHR (vk_renderer->instance, &(VkXlibSurfaceCreateInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = xlib_renderer->xdpy,
        .window = vk_onscreen_x11->xwin,
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
_cogl_winsys_onscreen_update_swap_throttled (CoglOnscreen *onscreen)
{
  VK_TODO ();
}

static void
_cogl_winsys_onscreen_set_visibility (CoglOnscreen *onscreen,
                                      CoglBool visibility)
{
  CoglContext *context = COGL_FRAMEBUFFER (onscreen)->context;
  CoglRenderer *renderer = context->display->renderer;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  CoglOnscreenVulkanX11 *vk_onscreen_x11 = onscreen->winsys;

  if (visibility)
    XMapWindow (xlib_renderer->xdpy, vk_onscreen_x11->xwin);
  else
    XUnmapWindow (xlib_renderer->xdpy, vk_onscreen_x11->xwin);
}

const CoglWinsysVtable *
_cogl_winsys_vulkan_x11_get_vtable (void)
{
  static CoglBool vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      memset (&vtable, 0, sizeof (vtable));

      vtable.id = COGL_WINSYS_ID_VULKAN_XLIB;
      vtable.name = "VULKAN_XLIB";
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
        _cogl_onscreen_vulkan_swap_buffers_with_damage;
      vtable.onscreen_update_swap_throttled =
        _cogl_winsys_onscreen_update_swap_throttled;
      vtable.onscreen_set_visibility = _cogl_winsys_onscreen_set_visibility;

      vtable_inited = TRUE;
    }

  return &vtable;
}
