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

#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-pipeline-vulkan-private.h"
#include "cogl-util-vulkan-private.h"

typedef struct _CoglPipelineVulkan
{
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline;
} CoglPipelineVulkan;

static CoglUserDataKey pipeline_vulkan_key;

static void
_destroy_pipeline_vulkan (CoglPipelineVulkan *vk_pipeline)
{

}

static CoglPipelineVulkan *
_create_pipeline_vulkan (CoglContext *context)
{
  CoglContextVulkan *vk_ctx = context->winsys;
  CoglPipelineVulkan *vk_pipeline = g_slice_new0 (CoglPipelineVulkan);

  /* We always have a vertex stage. */
  vkCreateDescriptorSetLayout (vk_ctx->device, &(VkDescriptorSetLayoutCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = (VkDescriptorSetLayoutBinding[]) {
        {
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .pImmutableSamplers = NULL
        }
      }
    },
    NULL,
    &vk_pipeline->set_layout);

  vkCreatePipelineLayout (vk_ctx->device, &(VkPipelineLayoutCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &vk_pipeline->set_layout,
    },
    NULL,
    &vk_pipeline->pipeline_layout);

  return vk_pipeline;
}

static CoglPipelineVulkan *
_get_pipeline_vulkan (CoglPipeline *pipeline, CoglContext *context)
{
  CoglPipelineVulkan *vk_pipeline =
    cogl_object_get_user_data (COGL_OBJECT (pipeline), &pipeline_vulkan_key);

  if (!vk_pipeline)
    {
      vk_pipeline = _create_pipeline_vulkan (context);
      cogl_object_set_user_data (COGL_OBJECT (pipeline), &pipeline_vulkan_key,
                                 vk_pipeline,
                                 (CoglUserDataDestroyCallback) _destroy_pipeline_vulkan);
    }

  return vk_pipeline;
}

void
_cogl_vulkan_flush_attributes_state (CoglFramebuffer *framebuffer,
                                     CoglPipeline *pipeline,
                                     CoglFlushLayerState *layer_state,
                                     CoglDrawFlags flags,
                                     CoglAttribute **attributes,
                                     int n_attributes)
{
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  CoglPipelineVulkan *vk_pipeline = _get_pipeline_vulkan (pipeline,
                                                          framebuffer->context);
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
  int n_layers = cogl_pipeline_get_n_layers (pipeline);
  unsigned long *layer_differences =
    g_alloca (sizeof (unsigned long) * n_layers);
  int i;
  const CoglPipelineProgend *progend;
  const CoglPipelineVertend *vertend;
  const CoglPipelineFragend *fragend;
  /* int n_layers; */

  VK_TODO();

  _cogl_pipeline_set_progend (pipeline, COGL_PIPELINE_PROGEND_VULKAN);

  for (i = 0; i < n_layers; i++)
    layer_differences[i] = COGL_PIPELINE_LAYER_STATE_ALL;

  /*   /\* Get a layer_differences mask for each layer to be flushed *\/ */
  /* n_layers = cogl_pipeline_get_n_layers (pipeline); */
  /* if (n_layers) */
  /*   { */
  /*     CoglPipelineCompareLayersState state; */
  /*     layer_differences = g_alloca (sizeof (unsigned long) * n_layers); */
  /*     memset (layer_differences, 0, sizeof (unsigned long) * n_layers); */
  /*     state.i = 0; */
  /*     state.layer_differences = layer_differences; */
  /*     _cogl_pipeline_foreach_layer_internal (pipeline, */
  /*                                            compare_layer_differences_cb, */
  /*                                            &state); */
  /*   } */
  /* else */
  /*   layer_differences = NULL; */


  /* CoglPipelineAddLayerState state; */

  progend = _cogl_pipeline_progends[COGL_PIPELINE_PROGEND_VULKAN];
  vertend = _cogl_pipeline_vertends[COGL_PIPELINE_VERTEND_VULKAN];
  fragend = _cogl_pipeline_fragends[COGL_PIPELINE_FRAGEND_VULKAN];

  /* g_assert (progend->start (pipeline)); */

  vertend->start (pipeline,
                  n_layers,
                  COGL_PIPELINE_STATE_ALL);

  vertend->end (pipeline,
                COGL_PIPELINE_STATE_ALL);

  /*     state.framebuffer = framebuffer; */
  /*     state.vertend = vertend; */
  /*     state.pipeline = pipeline; */
  /*     state.layer_differences = layer_differences; */
  /*     state.error_adding_layer = FALSE; */
  /*     state.added_layer = FALSE; */

  /*     _cogl_pipeline_foreach_layer_internal (pipeline, */
  /*                                            vertend_add_layer_cb, */
  /*                                            &state); */

  /*     if (G_UNLIKELY (state.error_adding_layer)) */
  /*       continue; */

  /*     if (G_UNLIKELY (!vertend->end (pipeline, pipelines_difference))) */
  /*       continue; */

  /*     /\* Now prepare the fragment processing state (fragend) */
  /*      * */
  /*      * NB: We can't combine the setup of the vertend and fragend */
  /*      * since the backends that do code generation share */
  /*      * ctx->codegen_source_buffer as a scratch buffer. */
  /*      *\/ */

  /*     fragend = _cogl_pipeline_fragends[progend->fragend]; */
  /*     state.fragend = fragend; */

  /*     fragend->start (pipeline, */
  /*                     n_layers, */
  /*                     pipelines_difference); */

  /*     _cogl_pipeline_foreach_layer_internal (pipeline, */
  /*                                            fragend_add_layer_cb, */
  /*                                            &state); */

  /*     if (G_UNLIKELY (state.error_adding_layer)) */
  /*       continue; */

  /*     if (!state.added_layer) */
  /*       { */
  /*         if (fragend->passthrough && */
  /*             G_UNLIKELY (!fragend->passthrough (pipeline))) */
  /*           continue; */
  /*       } */

  /*     if (G_UNLIKELY (!fragend->end (pipeline, pipelines_difference))) */
  /*       continue; */

  /*     if (progend->end) */
  /*       progend->end (pipeline, pipelines_difference); */
  /*     break; */
  /*   } */



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
