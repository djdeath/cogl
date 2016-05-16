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
#include "cogl-texture-private.h"
#include "cogl-texture-2d-vulkan-private.h"
#include "cogl-texture-2d-private.h"
#include "cogl-pipeline-opengl-private.h"
#include "cogl-error-private.h"
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
  return _cogl_pixel_format_to_vulkan_format (tex_2d->internal_format, NULL);
}

void
_cogl_texture_2d_vulkan_free (CoglTexture2D *tex_2d)
{
  CoglContextVulkan *vk_ctx = tex_2d->_parent.context->winsys;

  if (tex_2d->vk_image_view != VK_NULL_HANDLE)
    vkDestroyImageView (vk_ctx->device, tex_2d->vk_image_view, NULL);
  if (tex_2d->vk_image != VK_NULL_HANDLE)
    vkDestroyImage (vk_ctx->device, tex_2d->vk_image, NULL);
}

CoglBool
_cogl_texture_2d_vulkan_can_create (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat internal_format)
{
  CoglContextVulkan *vk_ctx = ctx->winsys;
  VkPhysicalDeviceProperties props;

  if (_cogl_pixel_format_to_vulkan_format (internal_format,
                                           NULL) == VK_FORMAT_UNDEFINED)
    return FALSE;

  vkGetPhysicalDeviceProperties (vk_ctx->physical_device, &props);

  if (width >= props.limits.maxFramebufferWidth ||
      height >= props.limits.maxFramebufferHeight)
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
  CoglContextVulkan *vk_ctx = tex->context->winsys;
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  VkFormat vk_format;
  VkResult result;
  VkMemoryRequirements requirements;

  internal_format =
    _cogl_texture_determine_internal_format (tex, COGL_PIXEL_FORMAT_ANY);
  vk_format =
    _cogl_pixel_format_to_vulkan_format (internal_format, NULL);

  if (vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  result = vkCreateImage (vk_ctx->device, &(VkImageCreateInfo) {
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
      .samples = 1,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .flags = 0,
      .initialLayout = tex_2d->vk_image_layout,
    },
    NULL,
    &tex_2d->vk_image);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create 2d texture : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkCreateImageView (vk_ctx->device, &(VkImageViewCreateInfo) {
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
    &tex_2d->vk_image_view);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create 2d texture view : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  vkGetImageMemoryRequirements (vk_ctx->device,
                                tex_2d->vk_image,
                                &requirements);
  result = vkAllocateMemory (vk_ctx->device,
                             &(VkMemoryAllocateInfo) {
                               .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = requirements.size,
                               .memoryTypeIndex = 0
                             },
                             NULL,
                             &tex_2d->vk_memory);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to allocate 2d texture memory : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkBindImageMemory(vk_ctx->device, tex_2d->vk_image,
                             tex_2d->vk_memory, 0);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to bind memory to 2d texture : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

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
  CoglBitmap *bmp = loader->src.bitmap.bitmap;
  CoglContextVulkan *vk_ctx = _cogl_bitmap_get_context (bmp)->winsys;
  CoglPixelFormat internal_format;
  int width = cogl_bitmap_get_width (bmp);
  int height = cogl_bitmap_get_height (bmp);
  CoglBool can_convert_in_place = loader->src.bitmap.can_convert_in_place;
  CoglBitmap *upload_bmp;
  VkFormat vk_format;

  internal_format =
    _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp));
  vk_format = _cogl_pixel_format_to_vulkan_format (internal_format, NULL);

  if (vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_SIZE,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  upload_bmp = _cogl_bitmap_convert_for_upload (bmp,
                                                internal_format,
                                                can_convert_in_place,
                                                error);
  if (upload_bmp == NULL)
    return FALSE;

  /* tex_2d->gl_texture = */
  /*   ctx->texture_driver->gen (ctx, GL_TEXTURE_2D, internal_format); */
  /* if (!ctx->texture_driver->upload_to_gl (ctx, */
  /*                                         GL_TEXTURE_2D, */
  /*                                         tex_2d->gl_texture, */
  /*                                         FALSE, */
  /*                                         upload_bmp, */
  /*                                         gl_intformat, */
  /*                                         gl_format, */
  /*                                         gl_type, */
  /*                                         error)) */
  /*   { */
  /*     cogl_object_unref (upload_bmp); */
  /*     return FALSE; */
  /*   } */

  /* tex_2d->gl_internal_format = gl_intformat; */

  /* cogl_object_unref (upload_bmp); */

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
