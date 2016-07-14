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

#include "cogl-context-private.h"
#include "cogl-texture-driver.h"
#include "cogl-util-vulkan-private.h"

static GLuint
_cogl_texture_driver_gen (CoglContext *ctx,
                          GLenum gl_target,
                          CoglPixelFormat internal_format)
{
  return 0;
}

static void
_cogl_texture_driver_prep_gl_for_pixels_upload (CoglContext *ctx,
                                                int pixels_rowstride,
                                                int pixels_bpp)
{
}

static void
_cogl_texture_driver_prep_gl_for_pixels_download (CoglContext *ctx,
                                                  int image_width,
                                                  int pixels_rowstride,
                                                  int pixels_bpp)
{
}

static CoglBool
_cogl_texture_driver_upload_subregion_to_gl (CoglContext *ctx,
                                             CoglTexture *texture,
                                             CoglBool is_foreign,
                                             int src_x,
                                             int src_y,
                                             int dst_x,
                                             int dst_y,
                                             int width,
                                             int height,
                                             int level,
                                             CoglBitmap  *source_bmp,
				             GLuint source_gl_format,
				             GLuint source_gl_type,
                                             CoglError **error)
{
  return FALSE;
}

static CoglBool
_cogl_texture_driver_upload_to_gl (CoglContext *ctx,
                                   GLenum gl_target,
                                   GLuint gl_handle,
                                   CoglBool is_foreign,
                                   CoglBitmap *source_bmp,
                                   GLint internal_gl_format,
                                   GLuint source_gl_format,
                                   GLuint source_gl_type,
                                   CoglError **error)
{
  return FALSE;
}

static CoglBool
_cogl_texture_driver_upload_to_gl_3d (CoglContext *ctx,
                                      GLenum gl_target,
                                      GLuint gl_handle,
                                      CoglBool is_foreign,
                                      GLint height,
                                      GLint depth,
                                      CoglBitmap *source_bmp,
                                      GLint internal_gl_format,
                                      GLuint source_gl_format,
                                      GLuint source_gl_type,
                                      CoglError **error)
{
  return FALSE;
}

static CoglBool
_cogl_texture_driver_gl_get_tex_image (CoglContext *ctx,
                                       GLenum gl_target,
                                       GLenum dest_gl_format,
                                       GLenum dest_gl_type,
                                       uint8_t *dest)
{
  return FALSE;
}

static CoglBool
_cogl_texture_driver_size_supported_3d (CoglContext *ctx,
                                        GLenum gl_target,
                                        GLenum gl_format,
                                        GLenum gl_type,
                                        int width,
                                        int height,
                                        int depth)
{
  return FALSE;
}

static CoglBool
_cogl_texture_driver_size_supported (CoglContext *ctx,
                                     GLenum gl_target,
                                     GLenum gl_intformat,
                                     GLenum gl_format,
                                     GLenum gl_type,
                                     int width,
                                     int height)
{
  VK_TODO();
  return FALSE;
}

static void
_cogl_texture_driver_try_setting_gl_border_color
                                       (CoglContext *ctx,
                                        GLuint gl_target,
                                        const GLfloat *transparent_color)
{
}

static CoglBool
_cogl_texture_driver_allows_foreign_gl_target (CoglContext *ctx,
                                               GLenum gl_target)
{
  return FALSE;
}

static CoglPixelFormat
_cogl_texture_driver_find_best_gl_get_data_format
                                            (CoglContext *context,
                                             CoglPixelFormat format,
                                             GLenum *closest_gl_format,
                                             GLenum *closest_gl_type)
{
  return format;
}

const CoglTextureDriver
_cogl_texture_driver_vulkan =
  {
    _cogl_texture_driver_gen,
    _cogl_texture_driver_prep_gl_for_pixels_upload,
    _cogl_texture_driver_upload_subregion_to_gl,
    _cogl_texture_driver_upload_to_gl,
    _cogl_texture_driver_upload_to_gl_3d,
    _cogl_texture_driver_prep_gl_for_pixels_download,
    _cogl_texture_driver_gl_get_tex_image,
    _cogl_texture_driver_size_supported,
    _cogl_texture_driver_size_supported_3d,
    _cogl_texture_driver_try_setting_gl_border_color,
    _cogl_texture_driver_allows_foreign_gl_target,
    _cogl_texture_driver_find_best_gl_get_data_format
  };
