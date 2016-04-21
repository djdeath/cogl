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

#ifndef _COGL_DRIVER_VULKAN_PRIVATE_H_
#define _COGL_DRIVER_VULKAN_PRIVATE_H_

#include "cogl-types.h"
#include "cogl-gl-header.h"
#include "cogl-context.h"

typedef struct _CoglRendererVulkan
{
  VkInstance instance;
} CoglRendererVulkan;

typedef struct _CoglContextVulkan
{
  VkPhysicalDevice physical_device;
  VkPhysicalDeviceProperties physical_device_properties;
  VkDevice device;
  VkQueue queue;
  VkFence fence;
  VkCommandPool cmd_pool;
} CoglContextVulkan;

CoglBool _cogl_vulkan_renderer_init (CoglRenderer *renderer,
                                     const char *extension,
                                     CoglError **error);
void _cogl_renderer_vulkan_deinit (CoglRenderer *renderer);

CoglBool _cogl_vulkan_context_init (CoglContext *context, CoglError **error);
void _cogl_vulkan_context_deinit (CoglContext *context);

#endif /* _COGL_DRIVER_VULKAN_PRIVATE_H_ */
