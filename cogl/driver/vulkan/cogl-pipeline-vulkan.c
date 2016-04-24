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
 */

#include "config.h"

#include "cogl-debug.h"
#include "cogl-buffer-private.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-pipeline-private.h"
#include "cogl-pipeline-fragend-vulkan-private.h"
#include "cogl-pipeline-progend-vulkan-private.h"
#include "cogl-pipeline-vertend-vulkan-private.h"
#include "cogl-pipeline-vulkan-private.h"
#include "cogl-texture-private.h"
#include "cogl-util-vulkan-private.h"

#include <test-fixtures/test-unit.h>

#include <glib.h>
#include <string.h>

typedef struct _CoglPipelineVulkan
{
  VkPipeline pipeline;

  VkPipelineVertexInputStateCreateInfo *vertex_inputs;
  int n_vertex_inputs;

  VkSampler *samplers;
  int n_samplers;
} CoglPipelineVulkan;

static CoglUserDataKey vk_pipeline_key;

static CoglPipelineVulkan *
get_vk_pipeline (CoglPipeline *pipeline)
{
  return cogl_object_get_user_data (COGL_OBJECT (pipeline), &vk_pipeline_key);
}

void
vk_pipeline_destroy (void *user_data,
                     void *instance)
{
  CoglPipelineVulkan *vk_pipeline = user_data;

  g_slice_free (CoglPipelineVulkan, vk_pipeline);
}

static CoglPipelineVulkan *
vk_pipeline_new (CoglPipeline *pipeline)
{
  CoglPipelineVulkan *vk_pipeline;

  vk_pipeline = g_slice_new0 (CoglPipelineVulkan);

  _cogl_object_set_user_data (COGL_OBJECT (pipeline),
                              &vk_pipeline_key,
                              vk_pipeline,
                              vk_pipeline_destroy);

  return vk_pipeline;
}

void
_cogl_pipeline_vulkan_invalidate (CoglPipeline *pipeline)
{
  CoglContextVulkan *vk_ctx = _cogl_context_get_default ()->winsys;
  CoglPipelineVulkan *vk_pipeline = get_vk_pipeline (pipeline);

  if (!vk_pipeline)
    return;

  if (vk_pipeline->pipeline != VK_NULL_HANDLE)
    {
      vkDestroyPipeline (vk_ctx->device, vk_pipeline->pipeline, NULL);
      vk_pipeline->pipeline = VK_NULL_HANDLE;
    }

  if (vk_pipeline->vertex_inputs)
    {
      g_free (vk_pipeline->vertex_inputs);
      vk_pipeline->vertex_inputs = NULL;
      vk_pipeline->n_vertex_inputs = 0;
    }
}

void
_cogl_pipeline_vulkan_invalidate_samplers (CoglPipeline *pipeline)
{
  CoglContextVulkan *vk_ctx = _cogl_context_get_default ()->winsys;
  CoglPipelineVulkan *vk_pipeline = get_vk_pipeline (pipeline);
  int i;

  if (!vk_pipeline)
    return;

  if (vk_pipeline->samplers == NULL)
    return;

  for (int i = 0; i < vk_pipeline->n_samplers; i++)
    {
      if (vk_pipeline->samplers[i] != VK_NULL_HANDLE)
        {
          vkDestroySampler (vk_ctx->device, vk_pipeline->samplers[i], NULL);
          vk_pipeline->samplers[i] = VK_NULL_HANDLE;
        }
    }
}

typedef struct
{
  CoglFramebuffer *framebuffer;
  const CoglPipelineVertend *vertend;
  const CoglPipelineFragend *fragend;
  CoglPipeline *pipeline;
  CoglBool error_adding_layer;
} CoglPipelineAddLayerState;

static CoglBool
vertend_add_layer_cb (CoglPipelineLayer *layer,
                      void *user_data)
{
  CoglPipelineAddLayerState *state = user_data;
  const CoglPipelineVertend *vertend = state->vertend;
  CoglPipeline *pipeline = state->pipeline;
  int unit_index = _cogl_pipeline_layer_get_unit_index (layer);

  /* Either generate per layer code snippets or setup the
   * fixed function glTexEnv for each layer... */
  if (G_LIKELY (!vertend->add_layer (pipeline,
                                     layer,
                                     0,
                                     state->framebuffer)))
    {
      state->error_adding_layer = TRUE;
      return FALSE;
    }

  return TRUE;
}

static CoglBool
fragend_add_layer_cb (CoglPipelineLayer *layer,
                      void *user_data)
{
  CoglPipelineAddLayerState *state = user_data;
  const CoglPipelineFragend *fragend = state->fragend;
  CoglPipeline *pipeline = state->pipeline;
  int unit_index = _cogl_pipeline_layer_get_unit_index (layer);

  /* Either generate per layer code snippets or setup the
   * fixed function glTexEnv for each layer... */
  if (G_UNLIKELY (fragend->add_layer (pipeline,
                                      layer,
                                      0)))
    {
      state->error_adding_layer = TRUE;
      return FALSE;
    }

  return TRUE;
}

static void
_cogl_pipeline_vulkan_compute_attributes (CoglPipeline *pipeline,
                                          CoglPipelineVulkan *vk_pipeline,
                                          CoglAttribute **attributes,
                                          int n_attributes)
{
  int i;
  VkPipelineVertexInputStateCreateInfo *info;
  void *ptr =
    g_malloc0 (sizeof (VkPipelineVertexInputStateCreateInfo) +
               n_attributes * sizeof (VkVertexInputBindingDescription) +
               n_attributes * sizeof (VkVertexInputAttributeDescription));

  info = ptr;
  info->pVertexBindingDescriptions =
    ptr + sizeof (VkPipelineVertexInputStateCreateInfo);
  info->pVertexAttributeDescriptions =
    ptr + sizeof (VkPipelineVertexInputStateCreateInfo) +
    n_attributes * sizeof (VkVertexInputBindingDescription);

  info->sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  info->vertexBindingDescriptionCount =
    info->vertexAttributeDescriptionCount = n_attributes;

  if (vk_pipeline->n_vertex_inputs != n_attributes)
    _cogl_pipeline_vulkan_invalidate (pipeline);

  for (i = 0; i < n_attributes; i++)
    {
      CoglAttribute *attribute = attributes[i];

      if (attribute->is_buffered)
        {
          CoglBuffer *buffer = COGL_BUFFER (attribute->d.buffered.attribute_buffer);
          CoglBufferVulkan *vk_buf = buffer->winsys;

          VkVertexInputBindingDescription *vertex_bind =
            (VkVertexInputBindingDescription *) &info->pVertexBindingDescriptions[i];
          VkVertexInputAttributeDescription *vertex_desc =
            (VkVertexInputAttributeDescription *) &info->pVertexAttributeDescriptions[i];

          vertex_bind->binding = i;
          vertex_bind->stride = attribute->d.buffered.stride;
          vertex_bind->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

          vertex_desc->location = i;
          vertex_desc->binding = i;
          vertex_desc->offset = attribute->d.buffered.offset;
          vertex_desc->format =
            _cogl_attribute_type_to_vulkan_format (attribute->d.buffered.type,
                                                   attribute->d.buffered.n_components);

          if (vk_pipeline->vertex_inputs &&
              (memcmp (&vk_pipeline->vertex_inputs->pVertexBindingDescriptions[i],
                       vertex_bind,
                       sizeof (VkVertexInputBindingDescription)) != 0 ||
               memcmp (&vk_pipeline->vertex_inputs->pVertexAttributeDescriptions[i],
                       vertex_desc,
                       sizeof (VkVertexInputAttributeDescription))))
            _cogl_pipeline_vulkan_invalidate (pipeline);

        }
      else
        {
          VK_TODO();
          g_assert_not_reached();
        }
    }

  if (vk_pipeline->vertex_inputs == NULL)
    {
      vk_pipeline->vertex_inputs = info;
      vk_pipeline->n_vertex_inputs = n_attributes;
    }
  else
    g_free (info);
}

static void
_cogl_pipeline_vulkan_create_pipeline (CoglPipeline *pipeline,
                                       CoglPipelineVulkan *vk_pipeline,
                                       CoglFramebuffer *framebuffer)
{
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkResult result;

  if (vk_pipeline->pipeline != VK_NULL_HANDLE)
    return;

  result =
    vkCreateGraphicsPipelines (vk_ctx->device,
                               (VkPipelineCache) { VK_NULL_HANDLE },
                               1,
                               &(VkGraphicsPipelineCreateInfo) {
                                 .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                 .stageCount = 2,
                                 .pStages = _cogl_pipeline_progend_get_vulkan_stage_info (pipeline),
                                 .pVertexInputState = vk_pipeline->vertex_inputs,
                                 .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                   .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                                   .primitiveRestartEnable = VK_FALSE,
                                 },
                                 .pViewportState = &(VkPipelineViewportStateCreateInfo) {
                                   .viewportCount = 1,
                                   .pViewports = &(VkViewport) {
                                     .x = 0,
                                     .y = 0,
                                     .width = framebuffer->width,
                                     .height = framebuffer->height,
                                     .minDepth = 0,
                                     .maxDepth = 1,
                                   },
                                 },
                                 .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                   .rasterizerDiscardEnable = VK_FALSE,
                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                   .cullMode = VK_CULL_MODE_BACK_BIT,
                                   .frontFace = VK_FRONT_FACE_CLOCKWISE
                                 },
                                 .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
                                   .rasterizationSamples = 1,
                                 },
                                 .pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo) {},
                                 .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                   .attachmentCount = 1,
                                   .pAttachments = (VkPipelineColorBlendAttachmentState []) {
                                     { .colorWriteMask = VK_COLOR_COMPONENT_A_BIT |
                                       VK_COLOR_COMPONENT_R_BIT |
                                       VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT },
                                   }
                                 },
                                 .flags = 0,
                                 .layout = _cogl_pipeline_progend_get_vulkan_pipeline_layout (pipeline),
                                 .renderPass = vk_fb->render_pass,
                                 .subpass = 0,
                                 .basePipelineHandle = (VkPipeline) { 0 },
                                 .basePipelineIndex = 0
                               },
                               NULL,
                               &vk_pipeline->pipeline);
  if (result != VK_SUCCESS)
    g_warning ("%s: Cannot create pipeline (%d) : %s", G_STRLOC, result,
               _cogl_vulkan_error_to_string (result));
}

void
_cogl_pipeline_flush_vulkan_state (CoglFramebuffer *framebuffer,
                                   CoglPipeline *pipeline,
                                   CoglAttribute **attributes,
                                   int n_attributes)
{
  int n_layers;
  unsigned long *layer_differences;
  int i;
  const CoglPipelineProgend *progend;
  const CoglPipelineVertend *vertend;
  const CoglPipelineFragend *fragend;
  CoglPipelineAddLayerState state;
  CoglPipelineVulkan *vk_pipeline = get_vk_pipeline (pipeline);

  COGL_STATIC_TIMER (pipeline_flush_timer,
                     "Mainloop", /* parent */
                     "Material Flush",
                     "The time spent flushing material state",
                     0 /* no application private data */);

  COGL_TIMER_START (_cogl_uprof_context, pipeline_flush_timer);

  if (!pipeline->dirty_real_blend_enable &&
      vk_pipeline &&
      vk_pipeline->pipeline != VK_NULL_HANDLE &&
      _cogl_pipeline_vertend_vulkan_get_shader (pipeline) != NULL &&
      _cogl_pipeline_fragend_vulkan_get_shader (pipeline) != NULL)
    {

      _cogl_pipeline_vulkan_compute_attributes (pipeline, vk_pipeline,
                                                attributes, n_attributes);

      if (vk_pipeline->pipeline != VK_NULL_HANDLE)
        goto done;
    }

  if (!vk_pipeline)
    vk_pipeline = vk_pipeline_new (pipeline);

  if (pipeline->progend == COGL_PIPELINE_PROGEND_UNDEFINED)
    _cogl_pipeline_set_progend (pipeline, COGL_PIPELINE_PROGEND_VULKAN);
  progend = _cogl_pipeline_progends[COGL_PIPELINE_PROGEND_VULKAN];

  /* Build up vertex shader. */
  g_assert (progend->start (pipeline));

  vertend = _cogl_pipeline_vertends[COGL_PIPELINE_VERTEND_VULKAN];
  vertend->start (pipeline, n_layers, 0);
  state.framebuffer = framebuffer;
  state.vertend = vertend;
  state.pipeline = pipeline;
  state.error_adding_layer = FALSE;

  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         vertend_add_layer_cb,
                                         &state);
  g_assert (!state.error_adding_layer);
  g_assert (vertend->end (pipeline, 0));

  /* Build up fragment shader. */
  fragend = _cogl_pipeline_fragends[COGL_PIPELINE_FRAGEND_VULKAN];
  state.fragend = fragend;
  fragend->start (pipeline, n_layers, 0);

  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         fragend_add_layer_cb,
                                         &state);
  g_assert (!state.error_adding_layer);
  g_assert (fragend->end (pipeline, 0));

  progend->end (pipeline, 0);

  _cogl_pipeline_vulkan_compute_attributes (pipeline, vk_pipeline,
                                            attributes, n_attributes);
  _cogl_pipeline_vulkan_create_pipeline (pipeline, vk_pipeline, framebuffer);

done:

  progend = _cogl_pipeline_progends[pipeline->progend];

  /* Give the progend a chance to update any uniforms that might not
   * depend on the material state. This is used on GLES2 to update the
   * matrices */
  if (progend->pre_paint)
    progend->pre_paint (pipeline, framebuffer);

  COGL_TIMER_STOP (_cogl_uprof_context, pipeline_flush_timer);
}

void
_cogl_vulkan_flush_attributes_state (CoglFramebuffer *framebuffer,
                                     CoglPipeline *pipeline,
                                     CoglFlushLayerState *layer_state,
                                     CoglDrawFlags flags,
                                     CoglAttribute **attributes,
                                     int n_attributes)
{
  _cogl_pipeline_flush_vulkan_state (framebuffer,
                                     pipeline,
                                     attributes,
                                     n_attributes);
}
