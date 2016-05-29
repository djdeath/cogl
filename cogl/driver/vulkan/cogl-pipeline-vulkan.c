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

  VkBuffer *attribute_buffers; /* Content of array not owned */
  VkDeviceSize *attribute_offsets;

  CoglVerticesMode vertices_mode;
} CoglPipelineVulkan;

static CoglUserDataKey vk_pipeline_key;

static CoglPipelineVulkan *
get_vk_pipeline (CoglPipeline *pipeline)
{
  return cogl_object_get_user_data (COGL_OBJECT (pipeline), &vk_pipeline_key);
}

static void
_cogl_pipeline_vulkan_invalidate_internal (CoglPipeline *pipeline)
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

  if (vk_pipeline->attribute_buffers)
    {
      g_free (vk_pipeline->attribute_buffers);
      vk_pipeline->attribute_buffers = NULL;
    }

  if (vk_pipeline->attribute_offsets)
    {
      g_free (vk_pipeline->attribute_offsets);
      vk_pipeline->attribute_offsets = NULL;
    }

  if (vk_pipeline->vertex_inputs)
    {
      g_free (vk_pipeline->vertex_inputs);
      vk_pipeline->vertex_inputs = NULL;
      vk_pipeline->n_vertex_inputs = 0;
    }
}

void
vk_pipeline_destroy (void *user_data,
                     void *instance)
{
  CoglPipelineVulkan *vk_pipeline = user_data;

  _cogl_pipeline_vulkan_invalidate_internal (instance);

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
  _cogl_pipeline_vulkan_invalidate_internal (pipeline);
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
  if (G_UNLIKELY (!fragend->add_layer (pipeline,
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
  CoglShaderVulkan *shader =
    _cogl_pipeline_progend_get_vulkan_shader (pipeline);
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

  vk_pipeline->attribute_buffers = g_malloc (sizeof (VkBuffer) * n_attributes);
  vk_pipeline->attribute_offsets = g_malloc (sizeof (VkDeviceSize) * n_attributes);

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

          vertex_desc->location =
            _cogl_shader_vulkan_get_input_attribute_location (shader,
                                                              COGL_GLSL_SHADER_TYPE_VERTEX,
                                                              attribute->name_state->name);
          vertex_desc->binding = i;
          vertex_desc->offset = 0;
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
            {
              COGL_NOTE (VULKAN,
                         "Invalidate pipeline because of vertex layout");
              _cogl_pipeline_vulkan_invalidate (pipeline);
            }

          vk_pipeline->attribute_buffers[i] = vk_buf->buffer;
          vk_pipeline->attribute_offsets[i] = attribute->d.buffered.offset;
        }
      else
        {
          VK_TODO();
          g_assert_not_reached();
        }
    }

  if (vk_pipeline->vertex_inputs == NULL)
    {
      if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_VULKAN)))
        {
          for (i = 0; i < n_attributes; i++)
            {
              CoglAttribute *attribute = attributes[i];
              VkVertexInputAttributeDescription *vertex_desc =
                (VkVertexInputAttributeDescription *) &info->pVertexAttributeDescriptions[i];

              COGL_NOTE (VULKAN,
                         "Attribute '%s' location=%i offset=%i"
                         " stride=%i n_components=%i vk_format=%i",
                         attribute->name_state->name,
                         vertex_desc->location,
                         attribute->d.buffered.offset,
                         attribute->d.buffered.stride,
                         attribute->d.buffered.n_components,
                         vertex_desc->format);
            }
        }

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

  VkPipelineColorBlendStateCreateInfo vk_blend_state;
  VkPipelineColorBlendAttachmentState vk_blend_attachment_state;

  VkPipelineRasterizationStateCreateInfo vk_raster_state;

  if (vk_pipeline->pipeline != VK_NULL_HANDLE)
    return;

  vk_pipeline->vertices_mode = vk_fb->vertices_mode;

  {
    CoglPipeline *blend_authority =
      _cogl_pipeline_get_authority (pipeline, COGL_PIPELINE_STATE_BLEND);
    CoglPipelineBlendState *blend_state =
      &blend_authority->big_state->blend_state;

    memset (&vk_blend_state, 0, sizeof (vk_blend_state));
    memset (&vk_blend_attachment_state, 0, sizeof (vk_blend_attachment_state));

    vk_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    vk_blend_state.attachmentCount = 1;
    vk_blend_state.pAttachments = &vk_blend_attachment_state;

    vk_blend_state.blendConstants[0] =
      cogl_color_get_red_float (&blend_state->blend_constant);
    vk_blend_state.blendConstants[1] =
      cogl_color_get_green_float (&blend_state->blend_constant);
    vk_blend_state.blendConstants[2] =
      cogl_color_get_blue_float (&blend_state->blend_constant);
    vk_blend_state.blendConstants[3] =
      cogl_color_get_alpha_float (&blend_state->blend_constant);

    vk_blend_attachment_state.blendEnable =
      _cogl_pipeline_get_blend_enabled (blend_authority) == COGL_PIPELINE_BLEND_ENABLE_DISABLED ? VK_FALSE : VK_TRUE;
    vk_blend_attachment_state.colorWriteMask = (VK_COLOR_COMPONENT_A_BIT |
                                                VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT);
    vk_blend_attachment_state.srcColorBlendFactor =
      _cogl_pipeline_blend_factor_to_vulkan_blend_factor (blend_state->blend_src_factor_rgb);
    vk_blend_attachment_state.dstColorBlendFactor =
      _cogl_pipeline_blend_factor_to_vulkan_blend_factor (blend_state->blend_dst_factor_rgb);
    vk_blend_attachment_state.colorBlendOp =
      _cogl_pipeline_blend_equation_to_vulkan_blend_op (blend_state->blend_equation_rgb);
    vk_blend_attachment_state.srcAlphaBlendFactor =
      _cogl_pipeline_blend_factor_to_vulkan_blend_factor (blend_state->blend_src_factor_alpha);
    vk_blend_attachment_state.dstAlphaBlendFactor =
      _cogl_pipeline_blend_factor_to_vulkan_blend_factor (blend_state->blend_dst_factor_alpha);
    vk_blend_attachment_state.alphaBlendOp =
      _cogl_pipeline_blend_equation_to_vulkan_blend_op (blend_state->blend_equation_alpha);
  }

  {
    memset (&vk_raster_state, 0, sizeof (vk_raster_state));

    vk_raster_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    vk_raster_state.rasterizerDiscardEnable = VK_FALSE;
    vk_raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    vk_raster_state.cullMode =
      _cogl_pipeline_cull_mode_to_vulkan_cull_mode (cogl_pipeline_get_cull_face_mode (pipeline));
    vk_raster_state.frontFace =
      _cogl_winding_to_vulkan_front_face (cogl_pipeline_get_front_face_winding (pipeline));

  }

  /* TODO: Break this down. */
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
                                   .topology = _cogl_vertices_mode_to_vulkan_primitive_topology (vk_pipeline->vertices_mode),
                                   .primitiveRestartEnable = VK_FALSE,
                                 },
                                 .pViewportState = &(VkPipelineViewportStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                   .viewportCount = 1,
                                   .pViewports = &(VkViewport) {
                                     .x = 0,
                                     .y = 0,
                                     .width = framebuffer->width,
                                     .height = framebuffer->height,
                                     .minDepth = 0,
                                     .maxDepth = 1,
                                   },
                                   .scissorCount = 1,
                                   .pScissors = &(VkRect2D) {
                                     .offset = {
                                       .x = 0,
                                       .y = 0,
                                     },
                                     .extent = {
                                       .width = framebuffer->width,
                                       .height = framebuffer->height,
                                     },
                                   },
                                 },
                                 .pRasterizationState = &vk_raster_state,
                                 .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                   .rasterizationSamples = 1,
                                 },
                                 .pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                 },
                                 .pColorBlendState = &vk_blend_state,
                                 .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
                                   .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                   .dynamicStateCount = 1,
                                   .pDynamicStates = (VkDynamicState []) {
                                     VK_DYNAMIC_STATE_VIEWPORT
                                   },
                                 },
                                 .flags = 0,
                                 .layout = _cogl_pipeline_progend_get_vulkan_pipeline_layout (pipeline),
                                 .renderPass = vk_fb->render_pass,
                                 .subpass = 0,
                                 .basePipelineHandle = NULL,
                                 .basePipelineIndex = -1,
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
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  CoglPipelineVulkan *vk_pipeline = get_vk_pipeline (pipeline);
  int i, n_layers = cogl_pipeline_get_n_layers (pipeline);
  const CoglPipelineProgend *progend;
  const CoglPipelineVertend *vertend;
  const CoglPipelineFragend *fragend;
  CoglPipelineAddLayerState state;
  VkPipelineLayout pipeline_layout;
  VkDescriptorSet descriptor_set;

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
      if (vk_fb->vertices_mode != vk_pipeline->vertices_mode)
        _cogl_pipeline_vulkan_invalidate (pipeline);

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

  vkCmdBindPipeline (vk_fb->cmd_buffer,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     vk_pipeline->pipeline);

  pipeline_layout = _cogl_pipeline_progend_get_vulkan_pipeline_layout (pipeline);
  descriptor_set = _cogl_pipeline_progend_get_vulkan_descriptor_set (pipeline);
  vkCmdBindDescriptorSets (vk_fb->cmd_buffer,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_layout,
                           0, 1,
                           &descriptor_set, 0, NULL);

  vkCmdBindVertexBuffers (vk_fb->cmd_buffer, 0, vk_pipeline->n_vertex_inputs,
                          vk_pipeline->attribute_buffers,
                          vk_pipeline->attribute_offsets);

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

void
_cogl_pipeline_vulkan_pre_change_notify (CoglPipeline *pipeline,
                                         CoglPipelineState change)
{
  if (change & (COGL_PIPELINE_STATE_BLEND |
                COGL_PIPELINE_STATE_BLEND_ENABLE |
                COGL_PIPELINE_STATE_CULL_FACE |
                COGL_PIPELINE_STATE_DEPTH))
    _cogl_pipeline_vulkan_invalidate (pipeline);
}
