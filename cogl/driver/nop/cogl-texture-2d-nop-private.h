/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 *   Robert Bragg <robert@linux.intel.com>
 */

#ifndef _COGL_TEXTURE_2D_NOP_PRIVATE_H_
#define _COGL_TEXTURE_2D_NOP_PRIVATE_H_

#include "cogl-types.h"
#include "cogl-context-private.h"
#include "cogl-texture.h"

void
_cogl_texture_2d_nop_free (CoglTexture2D *tex_2d);

CoglBool
_cogl_texture_2d_nop_can_create (CoglContext *ctx,
                                 int width,
                                 int height,
                                 CoglPixelFormat internal_format);

void
_cogl_texture_2d_nop_init (CoglTexture2D *tex_2d);

CoglBool
_cogl_texture_2d_nop_allocate (CoglTexture *tex,
                               CoglError **error);

void
_cogl_texture_2d_nop_copy_from_framebuffer (CoglTexture2D *tex_2d,
                                            int src_x,
                                            int src_y,
                                            int width,
                                            int height,
                                            CoglFramebuffer *src_fb,
                                            int dst_x,
                                            int dst_y,
                                            int level);

void
_cogl_texture_2d_nop_get_gl_info (CoglTexture2D *tex_2d,
                                  CoglTextureGLInfo *info);

void
_cogl_texture_2d_nop_generate_mipmap (CoglTexture2D *tex_2d);

CoglBool
_cogl_texture_2d_nop_copy_from_bitmap (CoglTexture2D *tex_2d,
                                       int src_x,
                                       int src_y,
                                       int width,
                                       int height,
                                       CoglBitmap *bitmap,
                                       int dst_x,
                                       int dst_y,
                                       int level,
                                       CoglError **error);

void
_cogl_texture_2d_nop_get_vulkan_info (CoglTexture2D *tex_2d,
                                      CoglTextureVulkanInfo *info);

void
_cogl_texture_2d_nop_vulkan_move_to (CoglTexture2D *tex_2d,
                                     CoglTextureDomain domain,
                                     VkCommandBuffer cmd_buffer);

#endif /* _COGL_TEXTURE_2D_NOP_PRIVATE_H_ */
