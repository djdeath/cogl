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

#include "cogl-blit.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-framebuffer-vulkan-private.h"
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

  tex_2d->vk_component_mapping.r = VK_COMPONENT_SWIZZLE_ZERO;
  tex_2d->vk_component_mapping.g = VK_COMPONENT_SWIZZLE_ZERO;
  tex_2d->vk_component_mapping.b = VK_COMPONENT_SWIZZLE_ZERO;
  tex_2d->vk_component_mapping.a = VK_COMPONENT_SWIZZLE_ZERO;

  switch (COGL_TEXTURE (tex_2d)->components)
    {
    case COGL_TEXTURE_COMPONENTS_A:
      /* Map A to R because COGL_PIXEL_FORMAT_A_8 can only be represented as
         VK_FORMAT_R8_*. */
      tex_2d->vk_component_mapping.a = VK_COMPONENT_SWIZZLE_R;
      break;
    case COGL_TEXTURE_COMPONENTS_RGBA:
    case COGL_TEXTURE_COMPONENTS_DEPTH:
      tex_2d->vk_component_mapping.r = VK_COMPONENT_SWIZZLE_R;
      tex_2d->vk_component_mapping.g = VK_COMPONENT_SWIZZLE_G;
      tex_2d->vk_component_mapping.b = VK_COMPONENT_SWIZZLE_B;
      tex_2d->vk_component_mapping.a = VK_COMPONENT_SWIZZLE_A;
      break;
    case COGL_TEXTURE_COMPONENTS_RGB:
      tex_2d->vk_component_mapping.r = VK_COMPONENT_SWIZZLE_R;
      tex_2d->vk_component_mapping.g = VK_COMPONENT_SWIZZLE_G;
      tex_2d->vk_component_mapping.b = VK_COMPONENT_SWIZZLE_B;
      tex_2d->vk_component_mapping.a = VK_COMPONENT_SWIZZLE_ONE;
      break;
    case COGL_TEXTURE_COMPONENTS_RG:
      tex_2d->vk_component_mapping.r = VK_COMPONENT_SWIZZLE_R;
      tex_2d->vk_component_mapping.g = VK_COMPONENT_SWIZZLE_G;
      break;
    }

  tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_GENERAL;
  tex_2d->vk_access_mask = (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

static CoglBool
is_format_available (CoglTexture2D *tex_2d,
                     VkFormat format,
                     VkImageUsageFlags usage,
                     VkImageTiling tiling)
{
  CoglContext *ctx = tex_2d->_parent.context;
  CoglRenderer *renderer = ctx->display->renderer;
  CoglRendererVulkan *vk_renderer = renderer->winsys;
  VkFormatProperties properties;
  VkFormatFeatureFlags requested_flags = 0;

  if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    requested_flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
    requested_flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    requested_flags |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
  if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    requested_flags |= VK_FORMAT_FEATURE_BLIT_DST_BIT;
  /* TODO: add more checks here. */

  VK ( ctx, vkGetPhysicalDeviceFormatProperties (vk_renderer->physical_device,
                                                 format,
                                                 &properties) );

  if (tiling == VK_IMAGE_TILING_OPTIMAL)
    return ((properties.optimalTilingFeatures & requested_flags) ==
            requested_flags);
  if (tiling == VK_IMAGE_TILING_LINEAR)
    return ((properties.linearTilingFeatures & requested_flags) ==
            requested_flags);
  return FALSE;
}

static CoglBool
super_seeding_format (VkFormat format,
                      VkComponentMapping mapping,
                      VkFormat *next_format,
                      VkComponentMapping *next_mapping)
{
  switch (format)
    {
    case VK_FORMAT_R8_UNORM:
      *next_format = VK_FORMAT_R8G8_UNORM;
      *next_mapping = mapping;
      return TRUE;
    case VK_FORMAT_R8G8_UNORM:
      *next_format = VK_FORMAT_R8G8B8_UNORM;
      *next_mapping = mapping;
      return TRUE;
    case VK_FORMAT_R8G8B8_UNORM:
      *next_format = VK_FORMAT_R8G8B8A8_UNORM;
      *next_mapping = mapping;
      next_mapping->a = VK_COMPONENT_SWIZZLE_ONE;
      return TRUE;
    case VK_FORMAT_R8G8B8A8_UNORM:
      *next_format = format;
      *next_mapping = mapping;
      return TRUE;
    }
  return FALSE;
}

/* Try to find a suitable backing format. For example, we can add some more
   components and map them to 0 or 1. */
static VkFormat
find_best_format_available (CoglTexture2D *tex_2d,
                            VkFormat format,
                            VkImageUsageFlags usage,
                            VkImageTiling tiling)
{
  VkFormat next_format = format;

  while (!is_format_available (tex_2d, next_format, usage, tiling))
    {
      if (!super_seeding_format (next_format, tex_2d->vk_component_mapping,
                                 &next_format, &tex_2d->vk_component_mapping))
        return VK_FORMAT_UNDEFINED;
    }

  COGL_NOTE (VULKAN, "Selecting format=%i for requested format=%i",
             next_format, format);

  return next_format;
}

static CoglBool
create_image (CoglTexture2D *tex_2d,
              VkImageUsageFlags usage,
              VkImageTiling tiling,
              int width, int height,
              int mip_levels,
              CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkImageCreateInfo image_create_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
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
    .tiling = tiling,
    .usage = (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT |
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
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
  VkImageUsageFlags usage = (VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
  VkFormat format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (internal_format, NULL);

  tex_2d->vk_format =
    find_best_format_available (tex_2d, format, usage, tiling);

  if (tex_2d->vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  if (!create_image (tex_2d, usage, tiling, width, height, 1, error))
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
load_bitmap_data_to_texture (CoglTexture2D *tex_2d,
                             CoglBitmap *bitmap,
                             uint32_t memory_size,
                             CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglPixelFormat internal_format = bitmap->format;
  int format_bpp = _cogl_pixel_format_get_bytes_per_pixel (internal_format);
  int width = bitmap->width,
    height = bitmap->height;
  void *data;

  VK_RET_VAL_ERROR ( ctx,
                     vkMapMemory (vk_ctx->device,
                                  tex_2d->vk_memory, 0,
                                  memory_size, 0,
                                  &data),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  if (bitmap->rowstride == width * format_bpp)
    {
      memcpy (data, bitmap->data, width * height * format_bpp);
    }
  else
    {
      int i, src_rowstride = bitmap->rowstride,
        dst_rowstride = width * format_bpp;

      for (i = 0; i < height; i++) {
        memcpy (data + i * dst_rowstride,
                bitmap->data + i * src_rowstride,
                dst_rowstride);
      }
    }

  VK ( ctx, vkUnmapMemory (vk_ctx->device, tex_2d->vk_memory) );

  return TRUE;
}

static CoglBool
load_bitmap_buffer_to_texture (CoglTexture2D *tex_2d,
                               CoglBitmap *bitmap,
                               int dst_x, int dst_y,
                               CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglBufferVulkan *vk_buf = bitmap->buffer->winsys;
  VkBufferImageCopy image_copy = {
    .bufferOffset = 0,
    .bufferRowLength = bitmap->width,
    .bufferImageHeight = bitmap->height,
    .imageSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
    .imageOffset = { dst_x, dst_y, 0, },
    .imageExtent = { bitmap->width, bitmap->height, 1, },
  };
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  CoglBool ret = FALSE;

  if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, error))
    goto error;

  _cogl_buffer_vulkan_move_to_device (bitmap->buffer, cmd_buffer);

  _cogl_texture_2d_vulkan_move_to_transfer_destination (tex_2d, cmd_buffer);

  VK ( ctx,
       vkCmdCopyBufferToImage (cmd_buffer,
                               vk_buf->buffer,
                               tex_2d->vk_image,
                               tex_2d->vk_image_layout,
                               1,
                               &image_copy) );

  _cogl_texture_2d_vulkan_move_to_device_for_sampling (tex_2d, cmd_buffer);

  if (!_cogl_vulkan_context_submit_command_buffer (ctx, cmd_buffer, error))
    goto error;

  ret = TRUE;

 error:
  if (cmd_buffer != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                               1, &cmd_buffer) );

  return ret;
}

static CoglBool
allocate_from_bitmap (CoglTexture2D *tex_2d,
                      CoglTextureLoader *loader,
                      CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglBitmap *bitmap = loader->src.bitmap.bitmap;
  CoglPixelFormat internal_format = bitmap->format;
  int width = bitmap->width, height = bitmap->height;
  uint32_t memory_size;
  VkFormat format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (bitmap->format, NULL);
  VkImageTiling tiling = VK_IMAGE_TILING_LINEAR;
  VkImageUsageFlags usage;

  /* Override default tiling.

     TODO: Consider always using optimal tiling and blit to an intermediate
     linear buffer.
   */
  if (bitmap->shared_bmp || bitmap->buffer)
    {
      tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_GENERAL;
      usage = (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
  else
    {
      tex_2d->vk_image_layout = VK_IMAGE_LAYOUT_GENERAL;
      usage = (VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }

  tex_2d->vk_format = format;

    /* find_best_format_available (tex_2d, format, usage, tiling); */
  if (tex_2d->vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  if (!create_image (tex_2d, usage, VK_IMAGE_TILING_LINEAR,
                     width, height, 1, error))
    return FALSE;

  if (!allocate_image_memory (tex_2d, &memory_size, error))
    return FALSE;

  if (bitmap->shared_bmp)
    {
      VK_TODO();
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Unsupported shared bitmap load to texture");
      return FALSE;
    }
  else if (bitmap->buffer)
    {
      if (!load_bitmap_buffer_to_texture (tex_2d, bitmap, 0, 0, error))
        return FALSE;
    }
  else
    {
      if (!load_bitmap_data_to_texture (tex_2d, bitmap, memory_size, error))
        return FALSE;
    }

  if (!create_image_view (tex_2d, error))
    return FALSE;

  tex_2d->internal_format = bitmap->format;
  _cogl_texture_set_allocated (tex, bitmap->format, width, height);

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
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = src_fb->winsys;
  VkImageCopy image_copy = {
    .srcSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
    .srcOffset = { src_x, src_y, 0, },
    .dstSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = level,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
    .dstOffset = { dst_x, dst_y, 0, },
    .extent = { width, height, 1, },
  };
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  CoglError *error = NULL;

  if (level != 0 && !tex_2d->vk_has_mipmap)
    _cogl_texture_2d_vulkan_generate_mipmap (tex_2d);

  cogl_framebuffer_finish (src_fb);

  if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, &error))
    goto error;

  _cogl_texture_2d_vulkan_move_to_transfer_destination (tex_2d, cmd_buffer);

  VK ( ctx,
       vkCmdCopyImage (cmd_buffer,
                       vk_fb->color_image,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       tex_2d->vk_image,
                       tex_2d->vk_image_layout,
                       1, &image_copy) );

  _cogl_texture_2d_vulkan_move_to_device_for_sampling (tex_2d, cmd_buffer);

  if (!_cogl_vulkan_context_submit_command_buffer (ctx, cmd_buffer, &error))
    goto error;

 error:
  if (error)
    {
      g_warning ("Copy from framebuffer to texture failed : %s",
                 error->message);
      cogl_error_free (error);
    }

  if (cmd_buffer != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                               1, &cmd_buffer) );
}

unsigned int
_cogl_texture_2d_vulkan_get_gl_handle (CoglTexture2D *tex_2d)
{
  uint64_t value = (uint64_t) tex_2d->vk_image;
  uint32_t *pvalue = (uint32_t *) &value;

  return pvalue[0] ^ pvalue[1];
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
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglTexture *src = NULL;
  CoglBool ret = FALSE;
  CoglBlitData data;

  if (level != 0 && !tex_2d->vk_has_mipmap)
    _cogl_texture_2d_vulkan_generate_mipmap (tex_2d);

  if (bmp->shared_bmp)
    {
      /* We need to go deeper. */
      return _cogl_texture_2d_vulkan_copy_from_bitmap (tex_2d, src_x, src_y,
                                                       width, height,
                                                       bmp->shared_bmp,
                                                       dst_x, dst_y,
                                                       level, error);
    }
  else if (bmp->buffer)
    {
      if (src_x == 0 && src_y == 0 && bmp->width == width && bmp->height == height)
        return load_bitmap_buffer_to_texture (tex_2d, bmp, dst_x, dst_y, error);
    }

  /* Slow 2-stage pass */
  if (!cogl_texture_allocate (tex, error))
    goto error;

  src = COGL_TEXTURE (cogl_texture_2d_new_from_bitmap (bmp));
  if (!cogl_texture_allocate (src, error))
    goto error;

  _cogl_blit_begin (&data, tex, src);
  _cogl_blit (&data, src_x, src_y, dst_x, dst_y, width, height);
  _cogl_blit_end (&data);

  ret = TRUE;

 error:
  if (src)
    cogl_object_unref (src);

  return ret;
}

void
_cogl_texture_2d_vulkan_get_data (CoglTexture2D *tex_2d,
                                  CoglPixelFormat format,
                                  int rowstride,
                                  uint8_t *data)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglBuffer *buffer;
  CoglBufferVulkan *vk_buf;
  uint32_t bpp = _cogl_pixel_format_get_bytes_per_pixel (format),
    src_rowstride = bpp * tex->width;
  VkBufferImageCopy image_copy = {
    .bufferOffset = 0,
    .bufferRowLength = rowstride / bpp,
    .bufferImageHeight = tex->height,
    .imageSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
    .imageOffset = { 0, 0, 0, },
    .imageExtent = { tex->width, tex->height, 1, },
  };
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  CoglError *error = NULL;
  void *mapped_data;

  if (tex_2d->internal_format != format)
    {
      g_warning ("Unsupported format conversion from texture format.");
      return;
    }

  buffer = COGL_BUFFER (cogl_pixel_buffer_new (ctx,
                                               src_rowstride * tex->height,
                                               NULL));
  vk_buf = buffer->winsys;

  if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, &error))
    goto error;

  _cogl_texture_2d_vulkan_move_to_transfer_source (tex_2d, cmd_buffer);

  VK ( ctx,
       vkCmdCopyImageToBuffer (cmd_buffer,
                               tex_2d->vk_image,
                               tex_2d->vk_image_layout,
                               vk_buf->buffer,
                               1,
                               &image_copy) );

  _cogl_buffer_vulkan_move_to_host (buffer, cmd_buffer);

  if (!_cogl_vulkan_context_submit_command_buffer (ctx, cmd_buffer, &error))
    goto error;

  mapped_data = cogl_buffer_map (buffer, COGL_BUFFER_ACCESS_READ, 0);

  if (rowstride == src_rowstride)
    {
      memcpy (data, mapped_data, buffer->size);
    }
  else
    {
      int i;

      for (i = 0; i < tex->height; i++)
        {
          memcpy (data + i * rowstride,
                  mapped_data + i * src_rowstride,
                  src_rowstride);
        }
    }

  cogl_buffer_unmap (buffer);

 error:
  if (error)
    g_warning ("Failed to get data from texture : %s", error->message);

  if (buffer)
    cogl_object_unref (buffer);

  if (cmd_buffer != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                               1, &cmd_buffer) );
}

CoglBool
_cogl_texture_2d_vulkan_ready_for_sampling (CoglTexture2D *tex_2d)
{
  return (tex_2d->vk_access_mask & VK_ACCESS_SHADER_READ_BIT) != 0 &&
    tex_2d->vk_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  VK_PIPELINE_STAGE_HOST_BIT,
                                  0,
                                  0, NULL,
                                  0, NULL,
                                  1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_device_for_sampling (CoglTexture2D *tex_2d,
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

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  0,
                                  0, NULL,
                                  0, NULL,
                                  1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_color_attachment (CoglTexture2D *tex_2d,
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

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  0,
                                  0, NULL,
                                  0, NULL,
                                  1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_transfer_destination (CoglTexture2D *tex_2d,
                                                      VkCommandBuffer cmd_buffer)
{
  CoglContext *ctx = tex_2d->_parent.context;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_2d->vk_access_mask,
    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    .oldLayout = tex_2d->vk_image_layout,
    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  0,
                                  0, NULL,
                                  0, NULL,
                                  1, &image_barrier) );
}

void
_cogl_texture_2d_vulkan_move_to_transfer_source (CoglTexture2D *tex_2d,
                                                 VkCommandBuffer cmd_buffer)
{
  CoglContext *ctx = tex_2d->_parent.context;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_2d->vk_access_mask,
    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
    .oldLayout = tex_2d->vk_image_layout,
    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
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
