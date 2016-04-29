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
#include "cogl-driver-vulkan-private.h"
#include "cogl-util-vulkan-private.h"
#include "cogl-feature-private.h"
#include "cogl-renderer-private.h"
#include "cogl-error-private.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-pipeline-vulkan-private.h"
#include "cogl-texture-2d-vulkan-private.h"

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
  COGL_FLAGS_SET (ctx->private_features, COGL_PRIVATE_FEATURE_OFFSCREEN_BLIT, TRUE);
  COGL_FLAGS_SET (ctx->private_features, COGL_PRIVATE_FEATURE_VBOS, TRUE);
  COGL_FLAGS_SET (ctx->private_features, COGL_FEATURE_ID_GLSL, TRUE);

  ctx->feature_flags |= COGL_FEATURE_SHADERS_GLSL;

  return TRUE;
}

CoglBool
_cogl_vulkan_renderer_init (CoglRenderer *renderer,
                            const char **extensions,
                            int n_extensions,
                            CoglError **error)
{
  CoglRendererVulkan *vk_renderer = renderer->winsys;
  VkResult result;

  result = vkCreateInstance (&(VkInstanceCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &(VkApplicationInfo) {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Cogl",
        .apiVersion = VK_MAKE_VERSION(1, 0, 2),
      },
      .enabledExtensionCount = n_extensions,
      .ppEnabledExtensionNames = extensions,
    },
    NULL,
    &vk_renderer->instance);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_INTERNAL,
                       "Cannot create vulkan instance : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  return TRUE;
}

void
_cogl_renderer_vulkan_deinit (CoglRenderer *renderer)
{
  CoglRendererVulkan *vk_renderer = renderer->winsys;

  if (vk_renderer->instance != VK_NULL_HANDLE)
    vkDestroyInstance (vk_renderer->instance, NULL);
}

CoglBool
_cogl_vulkan_context_init (CoglContext *context, CoglError **error)
{
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererVulkan *vk_renderer = renderer->winsys;
  CoglContextVulkan *vk_ctx = g_slice_new0 (CoglContextVulkan);
  VkResult result;
  uint32_t count = 1;

  context->winsys = vk_ctx;

  context->glsl_version_to_use = 450;

  result = vkEnumeratePhysicalDevices (vk_renderer->instance, &count,
                                      &vk_ctx->physical_device);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_INTERNAL,
                       "Cannot enumerate physical vulkan devices : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }
  printf("%d physical devices\n", count);

  vkGetPhysicalDeviceProperties (vk_ctx->physical_device,
                                 &vk_ctx->physical_device_properties);

  result = vkCreateDevice(vk_ctx->physical_device, &(VkDeviceCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
        .queueFamilyIndex = 0,
        .queueCount = 1,
      }
    },
    NULL,
    &vk_ctx->device);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_INTERNAL,
                       "Cannot create vulkan device : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  vkGetDeviceQueue(vk_ctx->device, 0, 0, &vk_ctx->queue);

  result = vkCreateFence (vk_ctx->device, &(VkFenceCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = 0,
    },
    NULL,
    &vk_ctx->fence);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_INTERNAL,
                       "Cannot create vulkan fence : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  result = vkCreateCommandPool (vk_ctx->device, &(const VkCommandPoolCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = 0,
      .flags = 0
    },
    NULL,
    &vk_ctx->cmd_pool);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_INTERNAL,
                       "Cannot create command pool : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  /* { */
  /*   const VkDescriptorPoolCreateInfo create_info = { */
  /*     .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, */
  /*     .pNext = NULL, */
  /*     .flags = 0, */
  /*     .maxSets = 1, */
  /*     .poolSizeCount = 1, */
  /*     .pPoolSizes = (VkDescriptorPoolSize[]) { */
  /*       { */
  /*         .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, */
  /*         .descriptorCount = 1 */
  /*       }, */
  /*     } */
  /*   }; */

  /*   result = vkCreateDescriptorPool (vk_ctx->device, &create_info, */
  /*                                    NULL, &vk_ctx->desc_pool); */
  /*   if (result != VK_SUCCESS) */
  /*     { */
  /*       _cogl_set_error (error, COGL_DRIVER_ERROR, */
  /*                      COGL_DRIVER_ERROR_INTERNAL, */
  /*                      "Cannot create descriptor pool : %s", */
  /*                      _cogl_vulkan_error_to_string (result)); */
  /*       goto error; */
  /*     } */
  /* } */

  return TRUE;

 error:
  _cogl_vulkan_context_deinit (context);

  return FALSE;
}

void
_cogl_vulkan_context_deinit (CoglContext *context)
{
  CoglContextVulkan *vk_ctx = context->winsys;

  /* if (vk_ctx->desc_pool != VK_NULL_HANDLE) */
  /*   vkDestroyDescriptorPool (vk_ctx->device, vk_ctx->desc_pool, NULL); */
  if (vk_ctx->cmd_pool != VK_NULL_HANDLE)
    vkDestroyCommandPool (vk_ctx->device, vk_ctx->cmd_pool, NULL);
  if (vk_ctx->fence != VK_NULL_HANDLE)
    vkDestroyFence (vk_ctx->device, vk_ctx->fence, NULL);
  if (vk_ctx->device != VK_NULL_HANDLE)
    vkDestroyDevice (vk_ctx->device, NULL);

  g_slice_free (CoglContextVulkan, vk_ctx);
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
