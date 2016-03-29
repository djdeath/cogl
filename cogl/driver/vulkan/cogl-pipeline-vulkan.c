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

#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-pipeline-vulkan-private.h"

void
_cogl_vulkan_flush_attributes_state (CoglFramebuffer *framebuffer,
                                     CoglPipeline *pipeline,
                                     CoglFlushLayerState *layer_state,
                                     CoglDrawFlags flags,
                                     CoglAttribute **attributes,
                                     int n_attributes)
{
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  int i, n;
  CoglBool const_attributes = FALSE;
  VkBuffer *vk_buffers = g_alloca (sizeof (VkBuffer) * n_attributes);
  VkDeviceSize *vk_buffer_sizes =
    g_alloca (sizeof (VkDeviceSize) * n_attributes);
  VkVertexInputBindingDescription *vk_vertex_binds =
    g_alloca (sizeof (VkVertexInputBindingDescription) * n_attributes);
  VkVertexInputAttributeDescription *vk_vertex_descs =
    g_alloca (sizeof (VkVertexInputAttributeDescription) * n_attributes);
  VkPipelineVertexInputStateCreateInfo vk_vi_create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = n_attributes, /* TODO: Inaccurate */
    .pVertexBindingDescriptions = vk_vertex_binds,
    .vertexAttributeDescriptionCount = n_attributes, /* TODO: Inaccurate */
    .pVertexAttributeDescriptions = vk_vertex_descs,
  };

  /* for (i = 0; i < n_attributes; i++) */
  /*   { */
  /*     CoglAttribute *attribute = attributes[i]; */

  /*     if (attributes->is_buffered) */
  /*       { */
  /*         CoglBuffer *attribute_buffer = COGL_BUFFER (attribute->d.attribute_buffer); */
  /*         CoglBufferVulkan *vk_buf = attribute_buffer->winsys; */

  /*         vk_vertex_binds[i].binding = i; */
  /*         vk_vertex_binds[i].stride = attribute->d.buffered.stride; */
  /*         vk_vertex_binds[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX; */

  /*         vk_vertex_descs[i].location = i; */
  /*         vk_vertex_descs[i].binding = i; */
  /*         vk_vertex_descs[i].offset = 0;//attributes->d.buffered.offset; */
  /*         vk_vertex_descs[i].format = _cogl_attribute_type_to_vulkan_format (attribute->d.type, */
  /*                                                                            attribute->d.n_components); */

  /*         vk_buffers[i] = vk_buf->buffer; */
  /*         vk_buffer_sizes[i] = attributes->d.buffered; */
  /*       } */
  /*     else */
  /*       { */
  /*         g_assert_not_reached(); */
  /*       } */
  /*   } */

  /* vkCmdBindVertexBuffers (vk_fd->cmd_buffer, 0, n_attributes /\* TODO: inaccurate *\/, */
  /*                         vk_buffers, vk_buffer_sizes); */

}
