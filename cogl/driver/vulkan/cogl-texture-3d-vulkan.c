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
 *
 * Authors:
 *  Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl-buffer-vulkan-private.h"
#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-private.h"
#include "cogl-renderer-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-3d-vulkan-private.h"
#include "cogl-texture-3d-private.h"
#include "cogl-util-vulkan-private.h"

#include <string.h>

typedef struct _CoglTexture3DVulkan CoglTexture3DVulkan;

struct _CoglTexture3DVulkan
{
  VkImage image;
  VkComponentMapping component_mapping;
  VkFormat format;
  VkImageTiling tiling;
  uint32_t mip_levels;

  VkDeviceMemory memory;
  uint32_t memory_size;

  VkImageView image_view;

  VkImageLayout image_layout;
  VkAccessFlags access_mask;

  CoglBool has_mipmap;
};

void
_cogl_texture_3d_vulkan_free (CoglTexture3D *tex_3d)
{
  CoglContext *ctx = tex_3d->_parent.context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;

  if (tex_3d_vk->image_view != VK_NULL_HANDLE)
    VK ( ctx,
         vkDestroyImageView (vk_ctx->device, tex_3d_vk->image_view, NULL) );
  if (tex_3d_vk->image != VK_NULL_HANDLE)
    VK ( ctx,
         vkDestroyImage (vk_ctx->device, tex_3d_vk->image, NULL) );
  if (tex_3d_vk->memory != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeMemory (vk_ctx->device, tex_3d_vk->memory, NULL) );

  g_slice_free (CoglTexture3DVulkan, tex_3d_vk);
}

void
_cogl_texture_3d_vulkan_init (CoglTexture3D *tex_3d)
{
  CoglTexture3DVulkan *tex_3d_vk = g_slice_new0 (CoglTexture3DVulkan);

  tex_3d_vk->image = VK_NULL_HANDLE;
  tex_3d_vk->image_view = VK_NULL_HANDLE;
  tex_3d_vk->memory = VK_NULL_HANDLE;

  tex_3d_vk->image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  tex_3d_vk->access_mask = 0;

  tex_3d->winsys = tex_3d_vk;
}

static CoglBool
create_image (CoglTexture3D *tex_3d,
              VkImageUsageFlags usage,
              VkImageTiling tiling,
              int width, int height, int depth,
              int mip_levels,
              CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkImageCreateInfo image_create_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
    .imageType = VK_IMAGE_TYPE_3D,
    .format = tex_3d_vk->format,
    .extent = {
      .width = width,
      .height = height,
      .depth = depth,
    },
    .mipLevels = mip_levels,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = tiling,
    .usage = (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT/*  | */
              /* VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT */),
    .initialLayout = tex_3d_vk->image_layout,
  };

  tex_3d_vk->tiling = tiling;
  tex_3d_vk->mip_levels = mip_levels;

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImage (vk_ctx->device, &image_create_info, NULL,
                                    &tex_3d_vk->image),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  return TRUE;
}

static CoglBool
allocate_image_memory (CoglTexture3D *tex_3d, CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkMemoryRequirements reqs;
  VkMemoryAllocateInfo allocate_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
  };

  VK ( ctx, vkGetImageMemoryRequirements (vk_ctx->device,
                                          tex_3d_vk->image,
                                          &reqs) );

  allocate_info.allocationSize = tex_3d_vk->memory_size = reqs.size;
  allocate_info.memoryTypeIndex =
    _cogl_vulkan_context_get_memory_heap (ctx, reqs.memoryTypeBits);
  VK_RET_VAL_ERROR ( ctx,
                     vkAllocateMemory (vk_ctx->device, &allocate_info, NULL,
                                       &tex_3d_vk->memory),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK_RET_VAL_ERROR ( ctx,
                     vkBindImageMemory (vk_ctx->device, tex_3d_vk->image,
                                        tex_3d_vk->memory, 0),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  return TRUE;
}

static CoglBool
create_image_view (CoglTexture3D *tex_3d, CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkImageViewCreateInfo image_view_create_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .flags = 0,
    .image = tex_3d_vk->image,
    .viewType = VK_IMAGE_VIEW_TYPE_3D,
    .format = tex_3d_vk->format,
    .components = tex_3d_vk->component_mapping,
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
                                        &tex_3d_vk->image_view),
                     FALSE, error,
                     COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER);

  return TRUE;
}

static CoglBool
_cogl_texture_3d_can_create (CoglContext *ctx,
                             int width,
                             int height,
                             int depth,
                             CoglPixelFormat internal_format,
                             CoglError **error)
{
  GLenum gl_intformat;
  GLenum gl_type;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          NULL,
                                          &gl_type);

  /* Check that the driver can create a texture with that size */
  if (!ctx->texture_driver->size_supported_3d (ctx,
                                               GL_TEXTURE_3D,
                                               gl_intformat,
                                               gl_type,
                                               width,
                                               height,
                                               depth))
    {
      _cogl_set_error (error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_UNSUPPORTED,
                       "The requested dimensions are not supported by the GPU");
      return FALSE;
    }

  return TRUE;
}

static CoglBool
allocate_with_size (CoglTexture3D *tex_3d,
                    CoglTextureLoader *loader,
                    CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglPixelFormat internal_format =
    _cogl_texture_determine_internal_format (COGL_TEXTURE (tex_3d),
                                             COGL_PIXEL_FORMAT_ANY);
  int width = loader->src.sized.width, height = loader->src.sized.height,
    depth = loader->src.sized.depth;
  VkImageUsageFlags usage = (VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

  tex_3d_vk->format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (tex->context,
                                                      internal_format, NULL,
                                                      &tex_3d_vk->component_mapping);

  if (tex_3d_vk->format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 3d due to format constraints");
      return FALSE;
    }

  if (!create_image (tex_3d, usage, tiling, width, height, depth,
                     1 + floor (log2 (MAX (width, height))), error))
    return FALSE;

  if (!allocate_image_memory (tex_3d, error))
    return FALSE;

  if (!create_image_view (tex_3d, error))
    return FALSE;

  tex_3d->internal_format = internal_format;
  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static CoglBool
load_bitmap_data_to_texture (CoglTexture3D *tex_3d,
                             int width,
                             int height,
                             int depth,
                             CoglBitmap *bitmap,
                             CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  void *src_data, *dst_data = NULL;
  CoglBool ret = FALSE;
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  VkImageSubresource img_sub_resource = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .mipLevel = 0,
    .arrayLayer = 0,
  };
  VkSubresourceLayout img_sub_layout;
  int i, j, src_rowstride, src_imagestride, dst_rowstride, dst_imagestride;

  if (bitmap->buffer)
    src_data = cogl_buffer_map (bitmap->buffer, COGL_BUFFER_ACCESS_READ, 0);
  else
    src_data = bitmap->data;

  if (tex_3d_vk->image_layout != VK_IMAGE_LAYOUT_GENERAL)
    {
      if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, error))
        goto error;

      _cogl_texture_vulkan_move_to (tex,
                                    COGL_TEXTURE_DOMAIN_HOST,
                                    cmd_buffer);

      if (!_cogl_vulkan_context_submit_command_buffer (ctx, cmd_buffer, error))
        goto error;
    }

  VK_ERROR ( ctx,
             vkMapMemory (vk_ctx->device,
                          tex_3d_vk->memory, 0,
                          tex_3d_vk->memory_size, 0,
                          &dst_data),
             error, COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_BAD_PARAMETER );

  VK ( ctx,
       vkGetImageSubresourceLayout (vk_ctx->device,
                                    tex_3d_vk->image,
                                    &img_sub_resource,
                                    &img_sub_layout) );

  src_rowstride = bitmap->rowstride;
  src_imagestride = (src_rowstride * cogl_bitmap_get_height (bitmap)) / depth;
  dst_rowstride = img_sub_layout.rowPitch;
  dst_imagestride = img_sub_layout.depthPitch;

  for (i = 0; i < depth; i++) {
    for (j = 0; j < height; j++) {
      memcpy ((uint8_t *) dst_data + i * dst_imagestride + j * dst_rowstride,
              (uint8_t *) src_data + i * src_imagestride + j * src_rowstride,
              MIN (src_rowstride, dst_rowstride));
    }
  }

  ret = TRUE;

 error:

  if (bitmap->buffer)
    cogl_buffer_unmap (bitmap->buffer);

  if (dst_data != NULL)
    VK ( ctx, vkUnmapMemory (vk_ctx->device, tex_3d_vk->memory) );

  if (cmd_buffer != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                               1, &cmd_buffer) );

  return ret;
}

static CoglBool
load_bitmap_buffer_to_texture (CoglTexture3D *tex_3d,
                               CoglBitmap *bitmap,
                               int width, int height, int depth,
                               CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
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
    .imageOffset = { 0, 0, 0, },
    .imageExtent = { bitmap->width, bitmap->height, depth, },
  };
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  CoglBool ret = FALSE;

  if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, error))
    goto error;

  _cogl_buffer_vulkan_move_to_device (bitmap->buffer, cmd_buffer);

  _cogl_texture_vulkan_move_to (tex,
                                COGL_TEXTURE_DOMAIN_TRANSFER_DESTINATION,
                                cmd_buffer);

  VK ( ctx,
       vkCmdCopyBufferToImage (cmd_buffer,
                               vk_buf->buffer,
                               tex_3d_vk->image,
                               tex_3d_vk->image_layout,
                               1,
                               &image_copy) );

  _cogl_texture_vulkan_move_to (tex,
                                COGL_TEXTURE_DOMAIN_SAMPLING,
                                cmd_buffer);

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
allocate_from_bitmap (CoglTexture3D *tex_3d,
                      CoglTextureLoader *loader,
                      CoglError **error)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglContext *ctx = tex->context;
  CoglBitmap *bitmap = loader->src.bitmap.bitmap;
  CoglPixelFormat internal_format;
  CoglBool ret = FALSE;
  int width = cogl_bitmap_get_width (bitmap),
    height = loader->src.bitmap.height,
    depth = loader->src.bitmap.depth;
  VkImageUsageFlags usage;

  internal_format =
    _cogl_texture_determine_internal_format (tex,
                                             cogl_bitmap_get_format (bitmap));
  if (!_cogl_texture_3d_can_create (ctx,
                                    width,
                                    height,
                                    depth,
                                    internal_format,
                                    error))
    return FALSE;

  tex_3d_vk->format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (ctx,
                                                      internal_format, NULL,
                                                      &tex_3d_vk->component_mapping);
  if (tex_3d_vk->format == VK_FORMAT_UNDEFINED)
    internal_format = COGL_PIXEL_FORMAT_RGBA_8888;

  /* Override default tiling.

     TODO: Consider always using optimal tiling and blit to an intermediate
     linear buffer.
   */
  if (bitmap->shared_bmp || bitmap->buffer)
    {
      usage = (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
  else
    {
      tex_3d_vk->image_layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
      tex_3d_vk->access_mask = VK_ACCESS_HOST_WRITE_BIT;
      usage = (VK_IMAGE_USAGE_SAMPLED_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }

 bitmap =
    _cogl_bitmap_convert_for_upload (bitmap,
                                     internal_format,
                                     loader->src.bitmap.can_convert_in_place,
                                     error);
  if (bitmap == NULL)
    goto error;

  tex_3d_vk->format =
    _cogl_pixel_format_to_vulkan_format_for_sampling (ctx,
                                                      bitmap->format, NULL,
                                                      &tex_3d_vk->component_mapping);
  if (tex_3d_vk->format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 3d due to format constraints");
      goto error;
    }

  if (!create_image (tex_3d, usage, VK_IMAGE_TILING_LINEAR,
                     width, height, depth, 1, error))
    goto error;

  if (!allocate_image_memory (tex_3d, error))
    goto error;

  if (bitmap->shared_bmp)
    {
      VK_TODO();
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Unsupported shared bitmap load to texture");
      goto error;
    }
  else if (bitmap->buffer)
    {
      if (!load_bitmap_buffer_to_texture (tex_3d, bitmap,
                                          width, height, depth,
                                          error))
        goto error;
    }
  else
    {
      if (!load_bitmap_data_to_texture (tex_3d,
                                        width, height, depth,
                                        bitmap, error))
        goto error;
    }

  if (!create_image_view (tex_3d, error))
    goto error;

  tex_3d->internal_format = bitmap->format;
  _cogl_texture_set_allocated (tex, bitmap->format, width, height);

  ret = TRUE;

 error:
  if (bitmap)
    cogl_object_unref (bitmap);

  return ret;
}

CoglBool
_cogl_texture_3d_vulkan_allocate (CoglTexture *tex,
                                  CoglError **error)
{
  CoglTexture3D *tex_3d = COGL_TEXTURE_3D (tex);
  CoglTextureLoader *loader = tex->loader;

  _COGL_RETURN_VAL_IF_FAIL (loader, FALSE);

  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZED:
      return allocate_with_size (tex_3d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      return allocate_from_bitmap (tex_3d, loader, error);
    default:
      break;
    }

  g_return_val_if_reached (FALSE);
}

void
_cogl_texture_3d_vulkan_get_gl_info (CoglTexture3D *tex_3d,
                                     CoglTextureGLInfo *info)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  uint64_t value = (uint64_t) tex_3d_vk->image;
  uint32_t *pvalue = (uint32_t *) &value;

  info->format = 0;
  info->handle = pvalue[0] ^ pvalue[1];
}

void
_cogl_texture_3d_vulkan_generate_mipmap (CoglTexture3D *tex_3d)
{
  CoglTexture *tex = COGL_TEXTURE (tex_3d);
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglContext *ctx = tex->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
  CoglError *error = NULL;
  VkImageBlit blit_region = {
    .srcSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseArrayLayer = 0,
      .baseArrayLayer = 1,
    },
    .srcOffsets = {
      { 0, 0, 0, },
      { tex->width, tex->height, tex_3d->depth, },
    },
    .dstSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseArrayLayer = 0,
      .baseArrayLayer = 1,
    },
    .dstOffsets = {
      { 0, 0, 0, },
      { tex->width, tex->height, tex_3d->depth, },
    },
  };

  if (tex_3d_vk->mip_levels <= 1)
    return;

  if (!_cogl_vulkan_context_create_command_buffer (ctx, &cmd_buffer, &error))
    goto end;

  _cogl_texture_vulkan_move_to (tex,
                                COGL_TEXTURE_DOMAIN_ATTACHMENT,
                                cmd_buffer);

  for (uint32_t l = 1; l < tex_3d_vk->mip_levels; l++) {
    blit_region.srcSubresource.mipLevel = l - 1;
    blit_region.dstSubresource.mipLevel = l;

    VK ( ctx,
         vkCmdBlitImage (cmd_buffer,
                         tex_3d_vk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         tex_3d_vk->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &blit_region,
                         VK_FILTER_LINEAR ) );
  }

  if (!_cogl_vulkan_context_submit_command_buffer (ctx, cmd_buffer, &error))
    goto end;

 end:
  if (error)
    {
      g_warning ("Failed to generate 3D mipmap : %s", error->message);
      cogl_error_free (error);
    }

  if (cmd_buffer != VK_NULL_HANDLE)
    VK ( ctx,
         vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                               1, &cmd_buffer) );
}

void
_cogl_texture_3d_vulkan_get_vulkan_info (CoglTexture3D *tex_3d,
                                         CoglTextureVulkanInfo *info)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;

  info->format = tex_3d_vk->format;
  info->image = tex_3d_vk->image;
  info->image_view = tex_3d_vk->image_view;
  info->image_layout = tex_3d_vk->image_layout;
  info->component_mapping = tex_3d_vk->component_mapping;
}

void
_cogl_texture_3d_vulkan_vulkan_move_to (CoglTexture3D *tex_3d,
                                        CoglTextureDomain domain,
                                        VkCommandBuffer cmd_buffer)
{
  CoglTexture3DVulkan *tex_3d_vk = tex_3d->winsys;
  CoglContext *ctx = tex_3d->_parent.context;
  VkImageLayout new_layout;
  VkAccessFlags new_access_mask;
  VkPipelineStageFlags dst_stage;
  VkImageMemoryBarrier image_barrier = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .srcAccessMask = tex_3d_vk->access_mask,
    .dstAccessMask = 0,
    .oldLayout = tex_3d_vk->image_layout,
    .newLayout = 0,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = tex_3d_vk->image,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
  };

  cogl_texture_domain_to_vulkan_layout_and_access_mask (domain,
                                                        &new_layout,
                                                        &new_access_mask,
                                                        &dst_stage);

  if (tex_3d_vk->image_layout == new_layout)
    return;

  image_barrier.newLayout = new_layout;
  image_barrier.dstAccessMask = new_access_mask;

  tex_3d_vk->image_layout = new_layout;
  tex_3d_vk->access_mask = new_access_mask;

  VK ( ctx, vkCmdPipelineBarrier (cmd_buffer,
                                  VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  dst_stage,
                                  0,
                                  0, NULL,
                                  0, NULL,
                                  1, &image_barrier) );
}
