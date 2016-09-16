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

#include <string.h>

#include "cogl-private.h"
#include "cogl-texture-3d-nop-private.h"
#include "cogl-texture-3d-private.h"
#include "cogl-error-private.h"

void
_cogl_texture_3d_nop_free (CoglTexture3D *tex_3d)
{
}

void
_cogl_texture_3d_nop_init (CoglTexture3D *tex_3d)
{
}

CoglBool
_cogl_texture_3d_nop_allocate (CoglTexture *tex,
                               CoglError **error)
{
  return TRUE;
}

void
_cogl_texture_3d_nop_get_gl_info (CoglTexture3D *tex_3d,
                                  CoglTextureGLInfo *info)
{
}

void
_cogl_texture_3d_nop_generate_mipmap (CoglTexture3D *tex_3d)
{
}

void
_cogl_texture_3d_nop_get_vulkan_info (CoglTexture3D *tex_3d,
                                      CoglTextureVulkanInfo *info)
{
}

void
_cogl_texture_3d_nop_vulkan_move_to (CoglTexture3D *tex_3d,
                                     CoglTextureDomain domain,
                                     VkCommandBuffer cmd_buffer)
{
}
