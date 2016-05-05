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

#ifndef _COGL_SHADER_VULKAN_PRIVATE_H_
#define _COGL_SHADER_VULKAN_PRIVATE_H_

#include "cogl-types.h"
#include "cogl-gl-header.h"
#include "cogl-context.h"
#include "cogl-glsl-shader-private.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _CoglShaderVulkanAttribute
{
  char *name;
  int location;
} CoglShaderVulkanAttribute;

typedef struct _CoglShaderVulkanUniform
{
  char *name;
  int offset;
} CoglShaderVulkanUniform;

typedef struct _CoglShaderVulkanBinding
{
  char *name;
  int binding;
} CoglShaderVulkanBinding;

typedef struct _CoglShaderVulkan CoglShaderVulkan;

CoglShaderVulkan *
_cogl_shader_vulkan_new (CoglContext *context);

void
_cogl_shader_vulkan_free (CoglShaderVulkan *shader);

void
_cogl_shader_vulkan_set_source (CoglShaderVulkan *shader,
                                CoglGlslShaderType type,
                                const char *string);

CoglBool
_cogl_shader_vulkan_link (CoglShaderVulkan *shader);

int
_cogl_shader_vulkan_get_uniform_block_size (CoglShaderVulkan *shader,
                                            CoglGlslShaderType stage,
                                            int index);

int
_cogl_shader_vulkan_get_input_attribute_location (CoglShaderVulkan *shader,
                                                  CoglGlslShaderType stage,
                                                  const char *name);

CoglShaderVulkanUniform *
_cogl_shader_vulkan_get_uniform (CoglShaderVulkan *shader,
                                 CoglGlslShaderType stage,
                                 const char *name);

int
_cogl_shader_vulkan_get_uniform_index (CoglShaderVulkan *shader,
                                       CoglGlslShaderType stage,
                                       const char *name);

int
_cogl_shader_vulkan_get_uniform_buffer_offset (CoglShaderVulkan *shader,
                                               CoglGlslShaderType stage,
                                               int index);

void *
_cogl_shader_vulkan_stage_to_spirv (CoglShaderVulkan *shader,
                                    CoglGlslShaderType type,
                                    uint32_t *size);

VkShaderModule
_cogl_shader_vulkan_get_shader_module (CoglShaderVulkan *shader,
                                       CoglGlslShaderType type);

#ifdef __cplusplus
}
#endif

#endif /* _COGL_SHADER_VULKAN_PRIVATE_H_ */
