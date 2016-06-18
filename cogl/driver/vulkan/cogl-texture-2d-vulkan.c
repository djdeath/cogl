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

#include <config.h>

#include <string.h>

#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-private.h"
#include "cogl-renderer-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-vulkan-private.h"
#include "cogl-texture-2d-private.h"
#include "cogl-util-vulkan-private.h"

VkImage
_cogl_texture_2d_get_vulkan_image (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_image;
}

VkImageView
_cogl_texture_2d_get_vulkan_image_view (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_image_view;
}

VkFormat
_cogl_texture_2d_get_vulkan_format (CoglTexture2D *tex_2d)
{
  return _cogl_pixel_format_to_vulkan_format_for_sampling (tex_2d->internal_format,
                                                           NULL);
}

void
_cogl_texture_2d_vulkan_free (CoglTexture2D *tex_2d)
{
  CoglContext *ctx = tex_2d->_parent.context;
  CoglContextVulkan *vk_ctx = ctx->winsys;

  if (tex_2d->vk_image_view != VK_NULL_HANDLE)
    VK ( ctx,
         vkDestroyImageView (vk_ctx->device, tex_2d->vk_image_view, NULL) );
  if (tex_2d->vk_image != VK_NULL_HANDLE)
    VK ( ctx,
         vkDestroyImage (vk_ctx->device, tex_2d->vk_image, NULL) );
  if (tex_2d->vk_memory != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeMemory (vk_ctx->device, tex_2d->vk_memory, NULL) );
}

CoglBool
_cogl_texture_2d_vulkan_can_create (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat internal_format)
{
  CoglRenderer *renderer = ctx->display->renderer;
  CoglRendererVulkan *vk_renderer = renderer->winsys;

  if (_cogl_pixel_format_to_vulkan_format_for_sampling (internal_format,
                                                        NULL) == VK_FORMAT_UNDEFINED)
    return FALSE;

  if (width >= vk_renderer->physical_device_properties.limits.maxFramebufferWidth ||
      height >= vk_renderer->physical_device_properties.limits.maxFramebufferHeight)
    return FALSE;

  return TRUE;
}

void
_cogl_texture_2d_vulkan_init (CoglTexture2D *tex_2d)
{
  tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  /* We default to GL_LINEAR for both filters */
  tex_2d->vk_min_filter = VK_FILTER_LINEAR;
  tex_2d->vk_mag_filter = VK_FILTER_LINEAR;

  /* Wrap mode to repeat */
  tex_2d->vk_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  tex_2d->vk_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

static CoglBool
allocate_with_size (CoglTexture2D *tex_2d,
                    CoglTextureLoader *loader,
                    CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  VkFormat vk_format;
  VkResult result;
  VkMemoryRequirements reqs;

  internal_format =
    _cogl_texture_determine_internal_format (tex, COGL_PIXEL_FORMAT_ANY);
  vk_format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (internal_format, NULL);

  if (vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImage (vk_ctx->device, &(VkImageCreateInfo) {
                         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                         .imageType = VK_IMAGE_TYPE_2D,
                         .format = vk_format,
                         .extent = {
                           .width = width,
                           .height = height,
                           .depth = 1
                         },
                         .mipLevels = 1 + floor (log2 (MAX (width, height))),
                         .arrayLayers = 1,
                         .samples = VK_SAMPLE_COUNT_1_BIT,
                         .tiling = VK_IMAGE_TILING_OPTIMAL,
                         .usage = (VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                         .flags = 0,
                         .initialLayout = tex_2d->vk_image_layout,
                       },
                       NULL,
                       &tex_2d->vk_image),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK ( ctx, vkGetImageMemoryRequirements (vk_ctx->device,
                                          tex_2d->vk_image,
                                          &reqs) );
  VK_RET_VAL_ERROR ( ctx,
                     vkAllocateMemory (vk_ctx->device,
                                       &(VkMemoryAllocateInfo) {
                                         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                         .allocationSize = reqs.size,
                                         .memoryTypeIndex =
                                           _cogl_vulkan_context_get_memory_heap (ctx, reqs.memoryTypeBits),
                                       },
                                       NULL,
                                       &tex_2d->vk_memory),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkBindImageMemory (vk_ctx->device, tex_2d->vk_image,
                                        tex_2d->vk_memory, 0),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImageView (vk_ctx->device, &(VkImageViewCreateInfo) {
                         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                         .flags = 0,
                         .image = tex_2d->vk_image,
                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                         .format = vk_format,
                         .components = {
                           .r = VK_COMPONENT_SWIZZLE_R,
                           .g = VK_COMPONENT_SWIZZLE_G,
                           .b = VK_COMPONENT_SWIZZLE_B,
                           .a = VK_COMPONENT_SWIZZLE_A,
                         },
                         .subresourceRange = {
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1,
                         },
                       },
                       NULL,
                       &tex_2d->vk_image_view),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER);

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static CoglBool
allocate_from_bitmap (CoglTexture2D *tex_2d,
                      CoglTextureLoader *loader,
                      CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglPixelFormat internal_format = loader->src.bitmap.bitmap->format;
  int format_bpp = _cogl_pixel_format_get_bytes_per_pixel (internal_format);
  int width = loader->src.bitmap.bitmap->width;
  int height = loader->src.bitmap.bitmap->height;
  VkFormat vk_format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (internal_format, NULL);
  VkResult result;
  VkMemoryRequirements reqs;
  void *data;

  /* TODO: Deal with cases where the bitmap has a backing CoglBuffer. */

  if (vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImage (vk_ctx->device, &(VkImageCreateInfo) {
                         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                         .imageType = VK_IMAGE_TYPE_2D,
                         .format = vk_format,
                         .extent = {
                           .width = width,
                           .height = height,
                           .depth = 1
                         },
                         .mipLevels = 1,
                         .arrayLayers = 1,
                         .samples = VK_SAMPLE_COUNT_1_BIT,
                         .tiling = VK_IMAGE_TILING_LINEAR,
                         .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                         .flags = 0,
                         .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
                       },
                       NULL,
                       &tex_2d->vk_image),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK ( ctx,
       vkGetImageMemoryRequirements (vk_ctx->device,
                                     tex_2d->vk_image,
                                     &reqs) );
  VK_RET_VAL_ERROR ( ctx,
                     vkAllocateMemory (vk_ctx->device,
                                       &(VkMemoryAllocateInfo) {
                                         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                         .allocationSize = reqs.size,
                                         .memoryTypeIndex =
                                           _cogl_vulkan_context_get_memory_heap (ctx, reqs.memoryTypeBits),
                                       },
                                       NULL,
                                       &tex_2d->vk_memory),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkBindImageMemory(vk_ctx->device, tex_2d->vk_image,
                                       tex_2d->vk_memory, 0),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkMapMemory (vk_ctx->device,
                                  tex_2d->vk_memory, 0,
                                  reqs.size, 0,
                                  &data),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  if (loader->src.bitmap.bitmap->rowstride ==
      width * _cogl_pixel_format_get_bytes_per_pixel (internal_format))
    {
      memcpy (data, loader->src.bitmap.bitmap->data,
              width * height * format_bpp);
    }
  else
    {
      int i, dst_rowstride = width * format_bpp;

      for (i = 0; i < height; i++) {
        memcpy (data + i * dst_rowstride,
                loader->src.bitmap.bitmap->data +
                i * loader->src.bitmap.bitmap->rowstride,
                dst_rowstride);
      }
    }

  VK ( ctx, vkUnmapMemory (vk_ctx->device, tex_2d->vk_memory) );

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImageView (vk_ctx->device, &(VkImageViewCreateInfo) {
                         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                         .flags = 0,
                         .image = tex_2d->vk_image,
                         .viewType = VK_IMAGE_VIEW_TYPE_2D,
                         .format = vk_format,
                         .components = {
                           .r = VK_COMPONENT_SWIZZLE_R,
                           .g = VK_COMPONENT_SWIZZLE_G,
                           .b = VK_COMPONENT_SWIZZLE_B,
                           .a = VK_COMPONENT_SWIZZLE_A,
                         },
                         .subresourceRange = {
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1,
                         },
                       },
                       NULL,
                       &tex_2d->vk_image_view),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

CoglBool
_cogl_texture_2d_vulkan_allocate (CoglTexture *tex,
                                  CoglError **error)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglTextureLoader *loader = tex->loader;

  _COGL_RETURN_VAL_IF_FAIL (loader, FALSE);

  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZED:
      return allocate_with_size (tex_2d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      return allocate_from_bitmap (tex_2d, loader, error);
    default:
      break;
    }

  g_return_val_if_reached (FALSE);
}

void
_cogl_texture_2d_vulkan_copy_from_framebuffer (CoglTexture2D *tex_2d,
                                               int src_x,
                                               int src_y,
                                               int width,
                                               int height,
                                               CoglFramebuffer *src_fb,
                                               int dst_x,
                                               int dst_y,
                                               int level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;

  /* Make sure the current framebuffers are bound, though we don't need to
   * flush the clip state here since we aren't going to draw to the
   * framebuffer. */
  _cogl_framebuffer_flush_state (ctx->current_draw_buffer,
                                 src_fb,
                                 COGL_FRAMEBUFFER_STATE_ALL &
                                 ~COGL_FRAMEBUFFER_STATE_CLIP);

  /* _cogl_bind_gl_texture_transient (GL_TEXTURE_2D, */
  /*                                  tex_2d->gl_texture, */
  /*                                  tex_2d->is_foreign); */

  /* ctx->glCopyTexSubImage2D (GL_TEXTURE_2D, */
  /*                           0, /\* level *\/ */
  /*                           dst_x, dst_y, */
  /*                           src_x, src_y, */
  /*                           width, height); */

  VK_TODO();
}

unsigned int
_cogl_texture_2d_vulkan_get_gl_handle (CoglTexture2D *tex_2d)
{
    return tex_2d->gl_texture;
}

void
_cogl_texture_2d_vulkan_generate_mipmap (CoglTexture2D *tex_2d)
{
  VK_TODO();
}

CoglBool
_cogl_texture_2d_vulkan_copy_from_bitmap (CoglTexture2D *tex_2d,
                                          int src_x,
                                          int src_y,
                                          int width,
                                          int height,
                                          CoglBitmap *bmp,
                                          int dst_x,
                                          int dst_y,
                                          int level,
                                          CoglError **error)
{
  VK_TODO();
  return FALSE;
}

void
_cogl_texture_2d_vulkan_get_data (CoglTexture2D *tex_2d,
                                  CoglPixelFormat format,
                                  int rowstride,
                                  uint8_t *data)
{
  VK_TODO();
}
