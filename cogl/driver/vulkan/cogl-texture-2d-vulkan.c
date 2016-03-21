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

#include "cogl-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-vulkan-private.h"
#include "cogl-texture-2d-private.h"
#include "cogl-pipeline-opengl-private.h"
#include "cogl-error-private.h"
#include "cogl-util-vulkan-private.h"

VkImage
_cogl_texture_2d_get_vulkan_texture (CoglTexture2D *tex_2d)
{
  return tex_2d->vk_image;
}

VkFormat
_cogl_texture_2d_get_vulkan_format (CoglTexture2D *tex_2d)
{
  return _cogl_pixel_format_to_vulkan_format (tex_2d->internal_format);
}

void
_cogl_texture_2d_vulkan_free (CoglTexture2D *tex_2d)
{
  if (tex_2d->vk_image_valid)
    {
      vkDestroyImage(tex_2d->_parent.context->vk_device,
                     tex_2d->vk_image, NULL);
      tex_2d->vk_image_valid = FALSE;
    }
}

CoglBool
_cogl_texture_2d_vulkan_can_create (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat internal_format)
{
  VkPhysicalDeviceProperties props;

  if (_cogl_pixel_format_to_vulkan_format (internal_format) == VK_FORMAT_UNDEFINED)
    return FALSE;

  vkGetPhysicalDeviceProperties(ctx->vk_physical_device, &props);

  if (width >= props.limits.maxFramebufferWidth ||
      height >= props.limits.maxFramebufferHeight)
    return FALSE;

  return TRUE;
}

void
_cogl_texture_2d_vulkan_init (CoglTexture2D *tex_2d)
{
  tex_2d->gl_texture = 0;

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
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  VkFormat vk_format;
  VkResult result;
  VkMemoryRequirements requirements;

  internal_format =
    _cogl_texture_determine_internal_format (tex, COGL_PIXEL_FORMAT_ANY);
  vk_format =
    _cogl_pixel_format_to_vulkan_format (internal_format);

  if (vk_format == VK_FORMAT_UNDEFINED)
    {
      _cogl_set_error (error, COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_BAD_PARAMETER,
                       "Failed to create texture 2d due to format constraints");
      return FALSE;
    }

  result = vkCreateImage(ctx->vk_device,
                         &(VkImageCreateInfo) {
                           .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                           .imageType = VK_IMAGE_TYPE_2D,
                           .format = vk_format,
                           .extent = { .width = width, .height = height, .depth = 1 },
                           .mipLevels = 1 + floor (log2 (MAX (width, height)),
                           .arrayLayers = 1,
                           .samples = 1,
                           .tiling = VK_IMAGE_TILING_OPTIMAL,
                           .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           .flags = 0,
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
  tex_2d->vk_image_valid = TRUE;

  vkGetImageMemoryRequirements (ctx->vk_device,
                                tex_2d->vk_image,
                                &requirements);
  result = vkAllocateMemory (ctx->vk_device,
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

  result = vkBindImageMemory(ctx->vk_device, tex_2d->vk_image,
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

/* static CoglBool */
/* allocate_from_bitmap (CoglTexture2D *tex_2d, */
/*                       CoglTextureLoader *loader, */
/*                       CoglError **error) */
/* { */
/*   CoglTexture *tex = COGL_TEXTURE (tex_2d); */
/*   CoglBitmap *bmp = loader->src.bitmap.bitmap; */
/*   CoglContext *ctx = _cogl_bitmap_get_context (bmp); */
/*   CoglPixelFormat internal_format; */
/*   int width = cogl_bitmap_get_width (bmp); */
/*   int height = cogl_bitmap_get_height (bmp); */
/*   CoglBool can_convert_in_place = loader->src.bitmap.can_convert_in_place; */
/*   CoglBitmap *upload_bmp; */
/*   GLenum gl_intformat; */
/*   GLenum gl_format; */
/*   GLenum gl_type; */

/*   internal_format = */
/*     _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp)); */

/*   if (!_cogl_texture_2d_vulkan_can_create (ctx, */
/*                                        width, */
/*                                        height, */
/*                                        internal_format)) */
/*     { */
/*       _cogl_set_error (error, COGL_TEXTURE_ERROR, */
/*                        COGL_TEXTURE_ERROR_SIZE, */
/*                        "Failed to create texture 2d due to size/format" */
/*                        " constraints"); */
/*       return FALSE; */
/*     } */

/*   upload_bmp = _cogl_bitmap_convert_for_upload (bmp, */
/*                                                 internal_format, */
/*                                                 can_convert_in_place, */
/*                                                 error); */
/*   if (upload_bmp == NULL) */
/*     return FALSE; */

/*   ctx->driver_vtable->pixel_format_to_gl (ctx, */
/*                                           cogl_bitmap_get_format (upload_bmp), */
/*                                           NULL, /\* internal format *\/ */
/*                                           &gl_format, */
/*                                           &gl_type); */
/*   ctx->driver_vtable->pixel_format_to_gl (ctx, */
/*                                           internal_format, */
/*                                           &gl_intformat, */
/*                                           NULL, */
/*                                           NULL); */

/*   /\* Keep a copy of the first pixel so that if glGenerateMipmap isn't */
/*      supported we can fallback to using GL_GENERATE_MIPMAP *\/ */
/*   if (!cogl_has_feature (ctx, COGL_FEATURE_ID_OFFSCREEN)) */
/*     { */
/*       CoglError *ignore = NULL; */
/*       uint8_t *data = _cogl_bitmap_map (upload_bmp, */
/*                                         COGL_BUFFER_ACCESS_READ, 0, */
/*                                         &ignore); */
/*       CoglPixelFormat format = cogl_bitmap_get_format (upload_bmp); */

/*       tex_2d->first_pixel.gl_format = gl_format; */
/*       tex_2d->first_pixel.gl_type = gl_type; */

/*       if (data) */
/*         { */
/*           memcpy (tex_2d->first_pixel.data, data, */
/*                   _cogl_pixel_format_get_bytes_per_pixel (format)); */
/*           _cogl_bitmap_unmap (upload_bmp); */
/*         } */
/*       else */
/*         { */
/*           g_warning ("Failed to read first pixel of bitmap for " */
/*                      "glGenerateMipmap fallback"); */
/*           cogl_error_free (ignore); */
/*           memset (tex_2d->first_pixel.data, 0, */
/*                   _cogl_pixel_format_get_bytes_per_pixel (format)); */
/*         } */
/*     } */

/*   tex_2d->gl_texture = */
/*     ctx->texture_driver->gen (ctx, GL_TEXTURE_2D, internal_format); */
/*   if (!ctx->texture_driver->upload_to_gl (ctx, */
/*                                           GL_TEXTURE_2D, */
/*                                           tex_2d->gl_texture, */
/*                                           FALSE, */
/*                                           upload_bmp, */
/*                                           gl_intformat, */
/*                                           gl_format, */
/*                                           gl_type, */
/*                                           error)) */
/*     { */
/*       cogl_object_unref (upload_bmp); */
/*       return FALSE; */
/*     } */

/*   tex_2d->gl_internal_format = gl_intformat; */

/*   cogl_object_unref (upload_bmp); */

/*   tex_2d->internal_format = internal_format; */

/*   _cogl_texture_set_allocated (tex, internal_format, width, height); */

/*   return TRUE; */
/* } */

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
      /* TODO: */
    /* case COGL_TEXTURE_SOURCE_TYPE_BITMAP: */
    /*   return allocate_from_bitmap (tex_2d, loader, error); */
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

  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   tex_2d->gl_texture,
                                   tex_2d->is_foreign);

  ctx->glCopyTexSubImage2D (GL_TEXTURE_2D,
                            0, /* level */
                            dst_x, dst_y,
                            src_x, src_y,
                            width, height);
}

unsigned int
_cogl_texture_2d_vulkan_get_gl_handle (CoglTexture2D *tex_2d)
{
    return tex_2d->gl_texture;
}

void
_cogl_texture_2d_vulkan_generate_mipmap (CoglTexture2D *tex_2d)
{
  CoglContext *ctx = COGL_TEXTURE (tex_2d)->context;

  /* glGenerateMipmap is defined in the FBO extension. If it's not
     available we'll fallback to temporarily enabling
     GL_GENERATE_MIPMAP and reuploading the first pixel */
  if (cogl_has_feature (ctx, COGL_FEATURE_ID_OFFSCREEN))
    _cogl_texture_gl_generate_mipmaps (COGL_TEXTURE (tex_2d));
#if defined(HAVE_COGL_GLES) || defined(HAVE_COGL_GL)
  else
    {
      _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                       tex_2d->gl_texture,
                                       tex_2d->is_foreign);

      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_GENERATE_MIPMAP,
                                GL_TRUE) );
      GE( ctx, glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 1, 1,
                                tex_2d->first_pixel.gl_format,
                                tex_2d->first_pixel.gl_type,
                                tex_2d->first_pixel.data) );
      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_GENERATE_MIPMAP,
                                GL_FALSE) );
    }
#endif
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
  CoglBitmap *upload_bmp;
  CoglPixelFormat upload_format;
  GLenum gl_format;
  GLenum gl_type;
  CoglBool status = TRUE;

  upload_bmp =
    _cogl_bitmap_convert_for_upload (bmp,
                                     _cogl_texture_get_format (tex),
                                     FALSE, /* can't convert in place */
                                     error);
  if (upload_bmp == NULL)
    return FALSE;

  upload_format = cogl_bitmap_get_format (upload_bmp);

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          upload_format,
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);

  /* If this touches the first pixel then we'll update our copy */
  if (dst_x == 0 && dst_y == 0 &&
      !cogl_has_feature (ctx, COGL_FEATURE_ID_OFFSCREEN))
    {
      CoglError *ignore = NULL;
      uint8_t *data =
        _cogl_bitmap_map (upload_bmp, COGL_BUFFER_ACCESS_READ, 0, &ignore);
      CoglPixelFormat bpp =
        _cogl_pixel_format_get_bytes_per_pixel (upload_format);

      tex_2d->first_pixel.gl_format = gl_format;
      tex_2d->first_pixel.gl_type = gl_type;

      if (data)
        {
          memcpy (tex_2d->first_pixel.data,
                  (data +
                   cogl_bitmap_get_rowstride (upload_bmp) * src_y +
                   bpp * src_x),
                  bpp);
          _cogl_bitmap_unmap (bmp);
        }
      else
        {
          g_warning ("Failed to read first bitmap pixel for "
                     "glGenerateMipmap fallback");
          cogl_error_free (ignore);
          memset (tex_2d->first_pixel.data, 0, bpp);
        }
    }

  status = ctx->texture_driver->upload_subregion_to_gl (ctx,
                                                        tex,
                                                        FALSE,
                                                        src_x, src_y,
                                                        dst_x, dst_y,
                                                        width, height,
                                                        level,
                                                        upload_bmp,
                                                        gl_format,
                                                        gl_type,
                                                        error);

  cogl_object_unref (upload_bmp);

  _cogl_texture_gl_maybe_update_max_level (tex, level);

  return status;
}

void
_cogl_texture_2d_vulkan_get_data (CoglTexture2D *tex_2d,
                                  CoglPixelFormat format,
                                  int rowstride,
                                  uint8_t *data)
{
  CoglContext *ctx = COGL_TEXTURE (tex_2d)->context;
  int bpp;
  int width = COGL_TEXTURE (tex_2d)->width;
  GLenum gl_format;
  GLenum gl_type;

  bpp = _cogl_pixel_format_get_bytes_per_pixel (format);

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          format,
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);

  ctx->texture_driver->prep_gl_for_pixels_download (ctx,
                                                    rowstride,
                                                    width,
                                                    bpp);

  _cogl_bind_gl_texture_transient (GL_TEXTURE_2D,
                                   tex_2d->gl_texture,
                                   tex_2d->is_foreign);

  ctx->texture_driver->gl_get_tex_image (ctx,
                                         GL_TEXTURE_2D,
                                         gl_format,
                                         gl_type,
                                         data);
}
