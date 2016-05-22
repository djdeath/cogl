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

#ifndef _COGL_UTIL_VULKAN_PRIVATE_H_
#define _COGL_UTIL_VULKAN_PRIVATE_H_

#include "cogl-types.h"

#include "cogl-gl-header.h"
#include "cogl-pipeline-private.h"

#define VK_TODO() do {                                                  \
    g_warning("Unimplemented function %s : %s", G_STRFUNC, G_STRLOC);   \
  } while(0)

//#define VK()

VkFormat
_cogl_pixel_format_to_vulkan_format (CoglPixelFormat format,
                                     CoglBool *premultiplied);

VkFormat
_cogl_pixel_format_to_vulkan_format_for_sampling (CoglPixelFormat format,
                                                  CoglBool *premultiplied);

VkFormat
_cogl_attribute_type_to_vulkan_format (CoglAttributeType type,
                                       int n_components);

const char *
_cogl_vulkan_error_to_string (VkResult error);

void
_cogl_pipeline_filter_to_vulkan_filter (CoglPipelineFilter filter,
                                        VkFilter *vkfilter,
                                        VkSamplerMipmapMode *vksamplermode);

VkSamplerAddressMode
_cogl_pipeline_wrap_mode_to_vulkan_address_mode (CoglPipelineWrapMode mode);

VkCullModeFlagBits
_cogl_pipeline_cull_mode_to_vulkan_cull_mode (CoglPipelineCullFaceMode mode);

VkFrontFace
_cogl_winding_to_vulkan_front_face (CoglWinding winding);

VkIndexType
_cogl_indices_type_to_vulkan_indices_type (CoglIndicesType type);

VkPrimitiveTopology
_cogl_vertices_mode_to_vulkan_primitive_topology (CoglVerticesMode mode);

void
_cogl_vulkan_util_get_texture_target_string (CoglTextureType texture_type,
                                             const char **target_string_out,
                                             const char **swizzle_out);

#endif /* _COGL_UTIL_VULKAN_PRIVATE_H_ */
