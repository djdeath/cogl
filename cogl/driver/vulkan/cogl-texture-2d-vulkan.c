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

VkImageLayout
_cogl_texture_2d_get_vulkan_image_layout (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_image_layout;
}

VkImageView
_cogl_texture_2d_get_vulkan_image_view (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_image_view;
}

VkFormat
_cogl_texture_2d_get_vulkan_format (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_format;
}

void
_cogl_texture_2d_vulkan_free (CoglTexture2D *tex_2d)
{
  CoglContext *ctx = tex_2d->_parent.context;
  CoglContextVulkan *vk_ctx = ctx->winsys;

  if (tex_2d->vk_image_view != VK_NULL_HANDLE)
    VK ( ctx,
         vkDestroyImageView (vk_ctx->device, tex_2d->vk_image_view, NULL) );
  if (!tex_2d->is_foreign && tex_2d->vk_image != VK_NULL_HANDLE)
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
  tex_2d->vk_image = VK_NULL_HANDLE;
  tex_2d->vk_image_view = VK_NULL_HANDLE;
  tex_2d->vk_memory = VK_NULL_HANDLE;

  tex_2d->vk_component_mapping.r = VK_COMPONENT_SWIZZLE_R;
  tex_2d->vk_component_mapping.g = VK_COMPONENT_SWIZZLE_G;
  tex_2d->vk_component_mapping.b = VK_COMPONENT_SWIZZLE_B;
  tex_2d->vk_component_mapping.a = VK_COMPONENT_SWIZZLE_A;
  tex_2d->vk_image_tiling = VK_IMAGE_TILING_OPTIMAL;
  tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_GENERAL;
  tex_2d->vk_access_mask = (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

static CoglBool
create_image (CoglTexture2D *tex_2d,
              VkImageUsageFlags usage,
              int width, int height,
              int mip_levels,
              CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkImageCreateInfo image_create_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = tex_2d->vk_format,
    .extent = {
      .width = width,
      .height = height,
      .depth = 1
    },
    .mipLevels = mip_levels,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = tex_2d->vk_image_tiling,
    .usage = usage,
    .flags = 0,
    .initialLayout = tex_2d->vk_image_layout,
  };

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImage (vk_ctx->device, &image_create_info, NULL,
                                    &tex_2d->vk_image),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  return TRUE;
}

static CoglBool
allocate_image_memory (CoglTexture2D *tex_2d, uint32_t *size, CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkMemoryRequirements reqs;
  VkMemoryAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
  };

  VK ( ctx, vkGetImageMemoryRequirements (vk_ctx->device,
                                          tex_2d->vk_image,
                                          &reqs) );

  allocate_info.allocationSize = reqs.size;
  allocate_info.memoryTypeIndex =
    _cogl_vulkan_context_get_memory_heap (ctx, reqs.memoryTypeBits);
  VK_RET_VAL_ERROR ( ctx,
                     vkAllocateMemory (vk_ctx->device, &allocate_info, NULL,
                                       &tex_2d->vk_memory),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkBindImageMemory (vk_ctx->device, tex_2d->vk_image,
                                        tex_2d->vk_memory, 0),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  if (size)
    *size = reqs.size;

  return TRUE;
}

CoglBool
create_image_view (CoglTexture2D *tex_2d, CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkImageViewCreateInfo image_view_create_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .flags = 0,
    .image = tex_2d->vk_image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = tex_2d->vk_format,
    .components = tex_2d->vk_component_mapping,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
  };

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImageView (vk_ctx->device, &image_view_create_info, NULL,
                                        &tex_2d->vk_image_view),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER);

  return TRUE;
}

static CoglBool
allocate_with_size (CoglTexture2D *tex_2d,
                    CoglTextureLoader *loader,
                    CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglPixelFormat internal_format =
    _cogl_texture_determine_internal_format (COGL_TEXTURE (tex_2d),
                                             COGL_PIXEL_FORMAT_ANY);
  int width = loader->src.sized.width, height = loader->src.sized.height;

  tex_2d->vk_format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (internal_format, NULL);
  if (tex_2d->vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  if (!create_image (tex_2d, (VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                     width, height,
                     1 + floor (log2 (MAX (width, height))), error))
    return FALSE;

  if (!allocate_image_memory (tex_2d, NULL, error))
    return FALSE;

  if (!create_image_view (tex_2d, error))
    return FALSE;

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
  int width = loader->src.bitmap.bitmap->width,
    height = loader->src.bitmap.bitmap->height;
  uint32_t memory_size;
  void *data;

  /* TODO: Deal with cases where the bitmap has a backing CoglBuffer. */

  /* Override default tiling.

     TODO: Consider always using optimal tiling and blit to an intermediate
     linear buffer.
   */
  tex_2d->vk_image_tiling = VK_IMAGE_TILING_LINEAR;
  tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  tex_2d->vk_format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (internal_format, NULL);

  if (tex_2d->vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  if (!create_image (tex_2d, (VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                     width, height, 1, error))
    return FALSE;

  if (!allocate_image_memory (tex_2d, &memory_size, error))
    return FALSE;

  VK_RET_VAL_ERROR ( ctx,
                     vkMapMemory (vk_ctx->device,
                                  tex_2d->vk_memory, 0,
                                  memory_size, 0,
                                  &data),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  if (loader->src.bitmap.bitmap->rowstride == width * format_bpp)
    {
      memcpy (data, loader->src.bitmap.bitmap->data,
              width * height * format_bpp);
    }
  else
    {
      int i, src_rowstride = loader->src.bitmap.bitmap->rowstride,
        dst_rowstride = width * format_bpp;

      for (i = 0; i < height; i++) {
        memcpy (data + i * dst_rowstride,
                loader->src.bitmap.bitmap->data + i * src_rowstride,
                dst_rowstride);
      }
    }

  VK ( ctx, vkUnmapMemory (vk_ctx->device, tex_2d->vk_memory) );

  if (!create_image_view (tex_2d, error))
    return FALSE;

  tex_2d->internal_format = internal_format;
  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static CoglBool
allocate_from_foreign_vulkan (CoglTexture2D *tex_2d,
                              CoglTextureLoader *loader,
                              CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  int width = loader->src.vulkan_foreign.width,
    height = loader->src.vulkan_foreign.height;

  tex_2d->is_foreign = TRUE;

  tex_2d->vk_format = loader->src.vulkan_foreign.format;
  tex_2d->vk_image = loader->src.vulkan_foreign.image;
  tex_2d->vk_component_mapping = loader->src.vulkan_foreign.component_mapping;
  tex_2d->vk_image_layout = loader->src.vulkan_foreign.image_layout;
  tex_2d->vk_access_mask = loader->src.vulkan_foreign.access_mask;
  tex_2d->internal_format =
    _cogl_vulkan_format_to_pixel_format (loader->src.vulkan_foreign.format);

  if (!create_image_view (tex_2d, error))
      return FALSE;

  _cogl_texture_set_allocated (tex, tex_2d->internal_format, width, height);

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
    case COGL_TEXTURE_SOURCE_TYPE_VULKAN_FOREIGN:
      return allocate_from_foreign_vulkan (tex_2d, loader, error);
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

void
_cogl_texture_2d_vulkan_move_to_host (CoglTexture2D *tex_2d,
                                      VkCommandBuffer cmd_buffer)
{
  CoglContext *ctx = tex_2d->_parent.context;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_2d->vk_access_mask,
    .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    .oldLayout = tex_2d->vk_image_layout,
    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = tex_2d->vk_image,
    .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.levelCount = 1,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.layerCount = 1,
  };

  if (tex_2d->vk_image_layout == image_barrier.newLayout)
    return;

  tex_2d->vk_image_layout = image_barrier.newLayout;
  tex_2d->vk_access_mask = image_barrier.dstAccessMask;

  VK ( ctx,  vkCmdPipelineBarrier (cmd_buffer,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                   VK_PIPELINE_STAGE_HOST_BIT,
                                   0,
                                   0, NULL,
                                   0, NULL,
                                   1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_device_for_read (CoglTexture2D *tex_2d,
                                                 VkCommandBuffer cmd_buffer)
{
  CoglContext *ctx = tex_2d->_parent.context;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_2d->vk_access_mask,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    .oldLayout = tex_2d->vk_image_layout,
    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = tex_2d->vk_image,
    .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.levelCount = 1,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.layerCount = 1,
  };

  if (tex_2d->vk_image_layout == image_barrier.newLayout)
    return;

  tex_2d->vk_image_layout = image_barrier.newLayout;
  tex_2d->vk_access_mask = image_barrier.dstAccessMask;

  VK ( ctx,  vkCmdPipelineBarrier (cmd_buffer,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                   0,
                                   0, NULL,
                                   0, NULL,
                                   1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_device_for_write (CoglTexture2D *tex_2d,
                                                  VkCommandBuffer cmd_buffer)
{
  CoglContext *ctx = tex_2d->_parent.context;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_2d->vk_access_mask,
    .dstAccessMask = (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
    .oldLayout = tex_2d->vk_image_layout,
    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = tex_2d->vk_image,
    .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .subresourceRange.baseMipLevel = 0,
    .subresourceRange.levelCount = 1,
    .subresourceRange.baseArrayLayer = 0,
    .subresourceRange.layerCount = 1,
  };

  if (tex_2d->vk_image_layout == image_barrier.newLayout)
    return;

  tex_2d->vk_image_layout = image_barrier.newLayout;
  tex_2d->vk_access_mask = image_barrier.dstAccessMask;

  VK ( ctx,  vkCmdPipelineBarrier (cmd_buffer,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                   VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                   0,
                                   0, NULL,
                                   0, NULL,
                                   1, &image_barrier) );
}

CoglTexture2D *
_cogl_texture_2d_vulkan_new_for_foreign (CoglContext *ctx,
                                         int width,
                                         int height,
                                         VkImage image,
                                         VkFormat format,
                                         VkComponentMapping component_mapping,
                                         VkImageLayout image_layout,
                                         VkAccessFlags access_mask)
{
  CoglTextureLoader *loader;

  _COGL_RETURN_VAL_IF_FAIL (image != VK_NULL_HANDLE, NULL);
  _COGL_RETURN_VAL_IF_FAIL (width > 0 && height > 0, NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_VULKAN_FOREIGN;
  loader->src.vulkan_foreign.width = width;
  loader->src.vulkan_foreign.height = height;
  loader->src.vulkan_foreign.image = image;
  loader->src.vulkan_foreign.format = format;
  loader->src.vulkan_foreign.component_mapping = component_mapping;
  loader->src.vulkan_foreign.image_layout = image_layout;
  loader->src.vulkan_foreign.access_mask = access_mask;

  return _cogl_texture_2d_create_base (ctx, width, height, format, loader);
}
