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

#ifndef _COGL_TEXTURE_3D_VULKAN_PRIVATE_H_
#define _COGL_TEXTURE_3D_VULKAN_PRIVATE_H_

#include "cogl-types.h"
#include "cogl-context-private.h"
#include "cogl-texture.h"

void
_cogl_texture_3d_vulkan_free (CoglTexture3D *tex_3d);

CoglBool
_cogl_texture_3d_vulkan_can_create (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat internal_format);

void
_cogl_texture_3d_vulkan_init (CoglTexture3D *tex_3d);

CoglBool
_cogl_texture_3d_vulkan_allocate (CoglTexture *tex,
                                  CoglError **error);

CoglTexture3D *
_cogl_texture_3d_vulkan_new_from_bitmap (CoglBitmap *bmp,
                                         CoglPixelFormat internal_format,
                                         CoglBool can_convert_in_place,
                                         CoglError **error);

void
_cogl_texture_3d_vulkan_copy_from_framebuffer (CoglTexture3D *tex_3d,
                                               int src_x,
                                               int src_y,
                                               int width,
                                               int height,
                                               CoglFramebuffer *src_fb,
                                               int dst_x,
                                               int dst_y,
                                               int level);

void
_cogl_texture_3d_vulkan_get_gl_info (CoglTexture3D *tex_3d,
                                     CoglTextureGLInfo *info);

void
_cogl_texture_3d_vulkan_generate_mipmap (CoglTexture3D *tex_3d);

CoglBool
_cogl_texture_3d_vulkan_copy_from_bitmap (CoglTexture3D *tex_3d,
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
_cogl_texture_3d_vulkan_get_data (CoglTexture3D *tex_3d,
                                  CoglPixelFormat format,
                                  int rowstride,
                                  uint8_t *data);

CoglBool
_cogl_texture_3d_vulkan_ready_for_sampling (CoglTexture3D *tex_3d);

void
_cogl_texture_3d_vulkan_get_vulkan_info (CoglTexture3D *tex_3d,
                                         CoglTextureVulkanInfo *info);

void
_cogl_texture_3d_vulkan_vulkan_move_to (CoglTexture3D *tex_3d,
                                        CoglTextureDomain domain,
                                        VkCommandBuffer cmd_buffer);

#endif /* _COGL_TEXTURE_3D_VULKAN_PRIVATE_H_ */