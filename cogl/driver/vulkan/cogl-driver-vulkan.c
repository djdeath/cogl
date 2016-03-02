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

#include <string.h>

#include <vulkan/vulkan.h>

#include "cogl-private.h"
#include "cogl-context-private.h"
#include "cogl-util-gl-private.h"
#include "cogl-feature-private.h"
#include "cogl-renderer-private.h"
#include "cogl-error-private.h"
#include "cogl-framebuffer-gl-private.h"
#include "cogl-texture-2d-gl-private.h"
#include "cogl-attribute-gl-private.h"
#include "cogl-clip-stack-gl-private.h"
#include "cogl-buffer-gl-private.h"

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
    case COGL_WINSYS_ID_GLX:
      return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
    case COGL_WINSYS_ID_EGL_XLIB:
      return VK_KHR_XCB_SURFACE_EXTENSION_NAME;
    case COGL_WINSYS_ID_EGL_WAYLAND:
      return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
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
  for (i = 0; i < G_N_ELEMENTS (private_features); i++)
    ctx->private_features[i] |= private_features[i];
  ctx->feature_flags |= flags;

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
  const char *extension = get_extension_for_winsys_id (cogl_renderer_get_winsys_id (ctx->display->renderer));
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

  /* TODO: per framebuffer? */
  vkCreateRenderPass(vc->device,
                     &(VkRenderPassCreateInfo) {
                       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                         .attachmentCount = 1,
                         .pAttachments = (VkAttachmentDescription[]) {
                         {
                           .format = VK_FORMAT_R8G8B8A8_SRGB,
                           .samples = 1,
                           .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                           .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                           .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         }
                       },
                         .subpassCount = 1,
                            .pSubpasses = (VkSubpassDescription []) {
                         {
                           .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                           .inputAttachmentCount = 0,
                           .colorAttachmentCount = 1,
                           .pColorAttachments = (VkAttachmentReference []) {
                             {
                               .attachment = 0,
                               .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                             }
                           },
                           .pResolveAttachments = (VkAttachmentReference []) {
                             {
                               .attachment = VK_ATTACHMENT_UNUSED,
                               .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                             }
                           },
                           .pDepthStencilAttachment = NULL,
                           .preserveAttachmentCount = 1,
                           .pPreserveAttachments = (uint32_t []) { 0 },
                         }
                       },
                            .dependencyCount = 0
                               },
                     NULL,
                     &ctx->render_pass);


  return TRUE;
}

const CoglDriverVtable
_cogl_driver_gl =
  {
    _cogl_driver_pixel_format_from_gl_internal,
    _cogl_driver_pixel_format_to_gl,
    _cogl_driver_update_features,
    _cogl_offscreen_gl_allocate,
    _cogl_offscreen_gl_free,
    _cogl_framebuffer_gl_flush_state,
    _cogl_framebuffer_gl_clear,
    _cogl_framebuffer_gl_query_bits,
    _cogl_framebuffer_gl_finish,
    _cogl_framebuffer_gl_discard_buffers,
    _cogl_framebuffer_gl_draw_attributes,
    _cogl_framebuffer_gl_draw_indexed_attributes,
    _cogl_framebuffer_gl_read_pixels_into_bitmap,
    _cogl_texture_2d_gl_free,
    _cogl_texture_2d_gl_can_create,
    _cogl_texture_2d_gl_init,
    _cogl_texture_2d_gl_allocate,
    _cogl_texture_2d_gl_copy_from_framebuffer,
    _cogl_texture_2d_gl_get_gl_handle,
    _cogl_texture_2d_gl_generate_mipmap,
    _cogl_texture_2d_gl_copy_from_bitmap,
    _cogl_texture_2d_gl_get_data,
    _cogl_gl_flush_attributes_state,
    _cogl_clip_stack_gl_flush,
    _cogl_buffer_gl_create,
    _cogl_buffer_gl_destroy,
    _cogl_buffer_gl_map_range,
    _cogl_buffer_gl_unmap,
    _cogl_buffer_gl_set_data,
  };
