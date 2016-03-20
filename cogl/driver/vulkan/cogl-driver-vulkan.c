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

#include <stdio.h>
#include <string.h>

#include "cogl-private.h"
#include "cogl-context-private.h"
#include "cogl-util-vulkan-private.h"
#include "cogl-feature-private.h"
#include "cogl-renderer-private.h"
#include "cogl-error-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-texture-2d-vulkan-private.h"
/* #include "cogl-attribute-gl-private.h" */
/* #include "cogl-clip-stack-gl-private.h" */
#include "cogl-buffer-vulkan-private.h"

static CoglBool
_cogl_driver_pixel_format_from_gl_internal (CoglContext *context,
                                            GLenum gl_int_format,
                                            CoglPixelFormat *out_format)
{
  // OpenG... What??
  return FALSE;
}

static CoglPixelFormat
_cogl_driver_pixel_format_to_gl (CoglContext *context,
                                 CoglPixelFormat  format,
                                 GLenum *out_glintformat,
                                 GLenum *out_glformat,
                                 GLenum *out_gltype)
{
  return format;
}

static const char *
get_extension_for_winsys_id (CoglWinsysID winsys_id)
{
  switch (winsys_id)
    {
#ifdef VK_KHR_xcb_surface
    case COGL_WINSYS_ID_GLX:
      return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
    case COGL_WINSYS_ID_EGL_XLIB:
      return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
#endif
#ifdef VK_KHR_wayland_surface
    case COGL_WINSYS_ID_EGL_WAYLAND:
      return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
    default:
      return NULL;
    }
}

static CoglBool
_cogl_driver_update_features (CoglContext *ctx,
                              CoglError **error)
{
  CoglFeatureFlags flags = 0;
  unsigned long private_features
    [COGL_FLAGS_N_LONGS_FOR_SIZE (COGL_N_PRIVATE_FEATURES)] = { 0 };

  COGL_FLAGS_SET (private_features,
                  COGL_PRIVATE_FEATURE_OFFSCREEN_BLIT, TRUE);

  flags |= COGL_FEATURE_SHADERS_GLSL;
  COGL_FLAGS_SET (ctx->features, COGL_FEATURE_ID_GLSL, TRUE);

  /* Cache features */
  /* for (i = 0; i < G_N_ELEMENTS (private_features); i++) */
  /*   ctx->private_features[i] |= private_features[i]; */
  ctx->feature_flags |= flags;

  const char *extension = get_extension_for_winsys_id (cogl_renderer_get_winsys_id (ctx->display->renderer));
  vkCreateInstance(&(VkInstanceCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &(VkApplicationInfo) {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Cogl",
        .apiVersion = VK_MAKE_VERSION(1, 0, 2),
      },
      .enabledExtensionCount = (extension != NULL),
      .ppEnabledExtensionNames = &extension,
    },
    NULL,
    &ctx->vk_instance);

  uint32_t count = 1;
  vkEnumeratePhysicalDevices(ctx->vk_instance, &count,
                             &ctx->vk_physical_device);
  printf("%d physical devices\n", count);

  vkCreateDevice(ctx->vk_physical_device,
                 &(VkDeviceCreateInfo) {
                   .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                     .queueCreateInfoCount = 1,
                     .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                     .queueFamilyIndex = 0,
                     .queueCount = 1,
                   }
                 },
                 NULL,
                 &ctx->vk_device);

  vkGetDeviceQueue(ctx->vk_device, 0, 0, &ctx->vk_queue);

  vkCreateFence (ctx->vk_device,
                 &(VkFenceCreateInfo) {
                   .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                   .flags = 0
                 },
                 NULL,
                 &ctx->vk_fence);

  vkCreateCommandPool (ctx->vk_device,
                       &(const VkCommandPoolCreateInfo) {
                         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                         .queueFamilyIndex = 0,
                         .flags = 0
                       },
                       NULL,
                       &ctx->vk_cmd_pool);


  return TRUE;
}

const CoglDriverVtable
_cogl_driver_vulkan =
  {
    _cogl_driver_pixel_format_from_gl_internal,
    _cogl_driver_pixel_format_to_gl,
    _cogl_driver_update_features,
    _cogl_offscreen_vulkan_allocate,
    _cogl_offscreen_vulkan_free,
    _cogl_framebuffer_vulkan_flush_state,
    _cogl_framebuffer_vulkan_clear,
    _cogl_framebuffer_vulkan_query_bits,
    _cogl_framebuffer_vulkan_finish,
    _cogl_framebuffer_vulkan_discard_buffers,
    _cogl_framebuffer_vulkan_draw_attributes,
    _cogl_framebuffer_vulkan_draw_indexed_attributes,
    _cogl_framebuffer_vulkan_read_pixels_into_bitmap,
    _cogl_texture_2d_vulkan_free,
    _cogl_texture_2d_vulkan_can_create,
    _cogl_texture_2d_vulkan_init,
    _cogl_texture_2d_vulkan_allocate,
    _cogl_texture_2d_vulkan_copy_from_framebuffer,
    _cogl_texture_2d_vulkan_get_gl_handle,
    _cogl_texture_2d_vulkan_generate_mipmap,
    _cogl_texture_2d_vulkan_copy_from_bitmap,
    _cogl_texture_2d_vulkan_get_data,
    _cogl_vulkan_flush_attributes_state,
    _cogl_clip_stack_vulkan_flush,
    _cogl_buffer_vulkan_create,
    _cogl_buffer_vulkan_destroy,
    _cogl_buffer_vulkan_map_range,
    _cogl_buffer_vulkan_unmap,
    _cogl_buffer_vulkan_set_data,
  };
