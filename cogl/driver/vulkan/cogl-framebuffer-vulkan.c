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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#include "cogl-buffer-vulkan-private.h"
#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-indices-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-vulkan-private.h"
#include "cogl-util-vulkan-private.h"

static CoglBool
_cogl_framebuffer_vulkan_allocate_depth_buffer (CoglFramebuffer *framebuffer,
                                                CoglError **error)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = NULL,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = vk_fb->depth_format,
    .extent = {
      .width = framebuffer->width,
      .height = framebuffer->height,
      .depth = 1,
    },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .flags = 0,
  };
  VkImageViewCreateInfo image_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = NULL,
    .image = VK_NULL_HANDLE,
    .format = vk_fb->depth_format,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    },
    .flags = 0,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
  };
  VkMemoryAllocateInfo mem_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
  };
  VkMemoryRequirements mem_reqs;

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImage (vk_ctx->device, &image_info, NULL,
                                    &vk_fb->depth_image),
                     FALSE,
                     error, COGL_FRAMEBUFFER_ERROR,
                     COGL_FRAMEBUFFER_ERROR_ALLOCATE );

  VK ( ctx,
       vkGetImageMemoryRequirements (vk_ctx->device,
                                     vk_fb->depth_image, &mem_reqs) );

  mem_info.allocationSize = mem_reqs.size;
  mem_info.memoryTypeIndex =
        _cogl_vulkan_context_get_memory_heap (framebuffer->context,
                                              mem_reqs.memoryTypeBits);
  VK_RET_VAL_ERROR ( ctx,
                     vkAllocateMemory (vk_ctx->device, &mem_info, NULL,
                                       &vk_fb->depth_memory),
                     FALSE,
                     error, COGL_FRAMEBUFFER_ERROR,
                     COGL_FRAMEBUFFER_ERROR_ALLOCATE );

  VK_RET_VAL_ERROR ( ctx,
                     vkBindImageMemory (vk_ctx->device, vk_fb->depth_image,
                                        vk_fb->depth_memory, 0),
                     FALSE,
                     error, COGL_FRAMEBUFFER_ERROR,
                     COGL_FRAMEBUFFER_ERROR_ALLOCATE );

  image_view_info.image = vk_fb->depth_image;
  VK_RET_VAL_ERROR ( ctx,
                     vkCreateImageView (vk_ctx->device, &image_view_info, NULL,
                                        &vk_fb->depth_image_view),
                     FALSE,
                     error, COGL_FRAMEBUFFER_ERROR,
                     COGL_FRAMEBUFFER_ERROR_ALLOCATE );

  return TRUE;
}

void
_cogl_framebuffer_vulkan_deinit (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  if (vk_fb->cmd_buffer)
    VK ( ctx, vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                                   1, &vk_fb->cmd_buffer) );
  if (vk_fb->render_pass != VK_NULL_HANDLE)
    VK ( ctx, vkDestroyRenderPass (vk_ctx->device, vk_fb->render_pass, NULL) );
  if (vk_fb->depth_image_view)
    VK ( ctx, vkDestroyImageView (vk_ctx->device, vk_fb->depth_image_view,
                                  NULL) );
  if (vk_fb->depth_image)
    VK ( ctx, vkDestroyImage (vk_ctx->device, vk_fb->depth_image, NULL) );
  if (vk_fb->depth_memory)
    VK ( ctx, vkFreeMemory (vk_ctx->device, vk_fb->depth_memory, NULL) );
}

CoglBool
_cogl_framebuffer_vulkan_init (CoglFramebuffer *framebuffer,
                               CoglError **error)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkAttachmentDescription attachments_description[2] = {
    [0] = {
      .format = _cogl_pixel_format_to_vulkan_format (framebuffer->internal_format,
                                                     NULL),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    },
    [1] = {
      .format = VK_FORMAT_D16_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    },
  };
  VkAttachmentReference color_reference = {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkAttachmentReference depth_reference = {
    .attachment = 1,
    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpass_description = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .flags = 0,
    .inputAttachmentCount = 0,
    .pInputAttachments = NULL,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_reference,
    .pResolveAttachments = NULL,
    .pDepthStencilAttachment = NULL,
    .preserveAttachmentCount = 0,
    .pPreserveAttachments = NULL,
  };
  VkRenderPassCreateInfo render_pass_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext = NULL,
    .attachmentCount = framebuffer->depth_writing_enabled ? 2 : 1,
    .pAttachments = attachments_description,
    .subpassCount = 1,
    .pSubpasses = &subpass_description,
    .dependencyCount = 0,
    .pDependencies = NULL,
  };

  if (framebuffer->depth_writing_enabled)
    {
      vk_fb->depth_format = VK_FORMAT_D16_UNORM;
      if (!_cogl_framebuffer_vulkan_allocate_depth_buffer (framebuffer, error))
        return FALSE;

      subpass_description.pDepthStencilAttachment = &depth_reference;
    }

  VK_RET_VAL_ERROR ( ctx,
                     vkCreateRenderPass (vk_ctx->device, &render_pass_info,
                                         NULL, &vk_fb->render_pass),
                     FALSE,
                     error, COGL_FRAMEBUFFER_ERROR,
                     COGL_FRAMEBUFFER_ERROR_ALLOCATE );

  return TRUE;
}

VkResult
_cogl_framebuffer_vulkan_create_framebuffer (CoglFramebuffer *framebuffer,
                                             VkImageView vk_image_view,
                                             VkFramebuffer *vk_framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkImageView image_views[2] = {
    vk_image_view,
    vk_fb->depth_image_view
  };
  VkFramebufferCreateInfo framebuffer_info = {
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .attachmentCount = framebuffer->depth_writing_enabled ? 2 : 1,
    .pAttachments = image_views,
    .width = framebuffer->width,
    .height = framebuffer->height,
    .layers = 1,
    .renderPass = vk_fb->render_pass,
  };

  return VK (ctx, vkCreateFramebuffer (vk_ctx->device, &framebuffer_info, NULL,
                                       vk_framebuffer) );
}

void
_cogl_framebuffer_vulkan_update_framebuffer (CoglFramebuffer *framebuffer,
                                             VkFramebuffer vk_framebuffer)
{
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  _cogl_framebuffer_vulkan_flush_state (framebuffer, framebuffer, 0);

  vk_fb->framebuffer = vk_framebuffer;
}

static void
_cogl_framebuffer_vulkan_ensure_command_buffer (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkClearValue clear_values[2];
  VkCommandBufferAllocateInfo buffer_allocate_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vk_ctx->cmd_pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1,
  };
  VkCommandBufferBeginInfo buffer_begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = 0
  };
  VkRenderPassBeginInfo render_begin_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = vk_fb->render_pass,
    .framebuffer = vk_fb->framebuffer,
    .renderArea = {
      { 0, 0 },
      { cogl_framebuffer_get_width (framebuffer),
        cogl_framebuffer_get_height (framebuffer) },
    },
    .pClearValues = clear_values,
    .clearValueCount = framebuffer->depth_writing_enabled ? 2 : 1,
  };

  if (vk_fb->cmd_buffer != VK_NULL_HANDLE)
    return;

  memset (clear_values, 0, sizeof (clear_values));

  VK_RET ( ctx,
           vkAllocateCommandBuffers (vk_ctx->device, &buffer_allocate_info,
                                     &vk_fb->cmd_buffer) );

  VK_RET ( ctx,
           vkBeginCommandBuffer (vk_fb->cmd_buffer, &buffer_begin_info) );

  VK ( ctx,
       vkCmdBeginRenderPass (vk_fb->cmd_buffer, &render_begin_info,
                             VK_SUBPASS_CONTENTS_INLINE) );
}

static void
_cogl_framebuffer_vulkan_flush_viewport_state (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkViewport vk_viewport;

  g_assert (framebuffer->viewport_width >=0 &&
            framebuffer->viewport_height >=0);

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer);

  vk_viewport.x = framebuffer->viewport_x;
  vk_viewport.y = framebuffer->viewport_y;
  vk_viewport.width = framebuffer->viewport_width;
  vk_viewport.height = framebuffer->viewport_height;
  vk_viewport.minDepth = 0;
  vk_viewport.maxDepth = 1;

  /* Convert the Cogl viewport y offset to an OpenGL viewport y offset
   * NB: OpenGL defines its window and viewport origins to be bottom
   * left, while Cogl defines them to be top left.
   * NB: We render upside down to offscreen framebuffers so we don't
   * need to convert the y offset in this case. */
  /* if (cogl_is_offscreen (framebuffer)) */
  /*   vk_viewport.y = framebuffer->viewport_y; */
  /* else */
  /*   vk_viewport.y = framebuffer->height - */
  /*     (framebuffer->viewport_y + framebuffer->viewport_height); */

  COGL_NOTE (VULKAN, "Setting viewport to (%f, %f, %f, %f)",
             vk_viewport.x,
             vk_viewport.y,
             vk_viewport.width,
             vk_viewport.height);

  VK ( ctx, vkCmdSetViewport (vk_fb->cmd_buffer, 0, 1, &vk_viewport) );
}

void
_cogl_clip_stack_vulkan_flush (CoglClipStack *stack,
                               CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  int x0, y0, x1, y1;
  VkRect2D vk_rect;

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer);

  _cogl_clip_stack_get_bounds (stack, &x0, &y0, &x1, &y1);

  vk_rect.offset.x = x0;
  vk_rect.offset.y = y0;
  vk_rect.extent.width = x1 - x0;
  vk_rect.extent.height = y1 - y0;

  VK ( ctx, vkCmdSetScissor (vk_fb->cmd_buffer, 0, 1, &vk_rect) );
}

void
_cogl_framebuffer_vulkan_end (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  /* We only want to flush if commands have been emitted. */
  if (vk_fb->cmd_buffer != VK_NULL_HANDLE &&
      vk_fb->cmd_buffer_length > 0)
    {
      VK ( ctx, vkCmdEndRenderPass (vk_fb->cmd_buffer) );
      VK ( ctx, vkEndCommandBuffer (vk_fb->cmd_buffer) );

      VK ( ctx, vkQueueSubmit (vk_ctx->queue, 1,
                               &(VkSubmitInfo) {
                                 .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &vk_fb->cmd_buffer,
                               }, /* vk_ctx->fence */VK_NULL_HANDLE) );

      VK ( ctx, vkQueueWaitIdle (vk_ctx->queue) );
      /* vkWaitForFences (vk_ctx->device, 1, (VkFence[]) { vk_ctx->fence }, */
      /*                  VK_TRUE, INT64_MAX); */

      VK ( ctx, vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                                      1, &vk_fb->cmd_buffer) );
      /* vkResetCommandPool (vk_ctx->device, vk_ctx->cmd_pool, 0); */

      vk_fb->cmd_buffer = VK_NULL_HANDLE;
      vk_fb->cmd_buffer_length = 0;
    }
}

void
_cogl_framebuffer_vulkan_flush_state (CoglFramebuffer *draw_buffer,
                                      CoglFramebuffer *read_buffer,
                                      CoglFramebufferState state)
{
  CoglContext *ctx = draw_buffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglFramebufferVulkan *vk_fb = draw_buffer->winsys;

  if (state & COGL_FRAMEBUFFER_STATE_INDEX_MODELVIEW)
    _cogl_context_set_current_modelview_entry (draw_buffer->context,
                                               _cogl_framebuffer_get_modelview_entry (draw_buffer));

  if (state & COGL_FRAMEBUFFER_STATE_INDEX_PROJECTION)
    _cogl_context_set_current_projection_entry (draw_buffer->context,
                                                _cogl_framebuffer_get_projection_entry (draw_buffer));
}

static CoglTexture *
create_depth_texture (CoglContext *ctx,
                      int width,
                      int height)
{
  CoglTexture2D *depth_texture =
    cogl_texture_2d_new_with_size (ctx, width, height);

  cogl_texture_set_components (COGL_TEXTURE (depth_texture),
                               COGL_TEXTURE_COMPONENTS_DEPTH);

  return COGL_TEXTURE (depth_texture);
}

void
_cogl_framebuffer_vulkan_clear (CoglFramebuffer *framebuffer,
                                unsigned long buffers,
                                float red,
                                float green,
                                float blue,
                                float alpha)
{
  CoglContext *ctx = framebuffer->context;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkClearAttachment clear_attachments[2];
  VkClearRect rect = {
    .rect = {
      .offset = {
        .x = 0,
        .y = 0,
      },
      .extent = {
        .width = framebuffer->width,
        .height = framebuffer->height,
      },
    },
    .baseArrayLayer = 0,
    .layerCount = 1,
  };
  uint32_t count = 0;

  /* TODO: maybe move this into
     _cogl_framebuffer_vulkan_ensure_command_buffer ? */
  if (G_UNLIKELY (!framebuffer->allocated))
    {
      cogl_framebuffer_allocate (framebuffer, NULL);
      vk_fb = framebuffer->winsys;
    }

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer);

  memset (clear_attachments, 0, sizeof (clear_attachments));
  if (buffers & COGL_BUFFER_BIT_COLOR)
    {
      clear_attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      clear_attachments[count].colorAttachment = 0;
      clear_attachments[count].clearValue.color.float32[0] = red;
      clear_attachments[count].clearValue.color.float32[1] = green;
      clear_attachments[count].clearValue.color.float32[2] = blue;
      clear_attachments[count].clearValue.color.float32[3] = alpha;
      count++;
    }
  if (buffers & COGL_BUFFER_BIT_DEPTH)
    {
      clear_attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      clear_attachments[count].clearValue.depthStencil.depth = 1.0;
      clear_attachments[count].clearValue.depthStencil.stencil = 0;
      count++;
    }

  VK ( ctx, vkCmdClearAttachments (vk_fb->cmd_buffer,
                                   count, clear_attachments,
                                   1, &rect) );
}

void
_cogl_framebuffer_vulkan_query_bits (CoglFramebuffer *framebuffer,
                                     CoglFramebufferBits *bits)
{
  uint64_t bit_field =
    _cogl_pixel_format_query_bits (framebuffer->internal_format);

  bits->alpha = _COGL_COLOR_BITS_GET_ALPHA (bit_field);
  bits->red = _COGL_COLOR_BITS_GET_RED (bit_field);
  bits->green = _COGL_COLOR_BITS_GET_GREEN (bit_field);
  bits->blue = _COGL_COLOR_BITS_GET_BLUE (bit_field);

  /* TODO: Hardcoded for now. */
  bits->depth = 16;
  bits->stencil = 0;
}

void
_cogl_framebuffer_vulkan_finish (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;

  VK_RET ( ctx, vkWaitForFences (vk_ctx->device,
                                 1, (VkFence[]) { vk_ctx->fence },
                                 VK_TRUE, INT64_MAX) );
}

void
_cogl_framebuffer_vulkan_discard_buffers (CoglFramebuffer *framebuffer,
                                          unsigned long buffers)
{
}

void
_cogl_framebuffer_vulkan_draw_attributes (CoglFramebuffer *framebuffer,
                                          CoglPipeline *pipeline,
                                          CoglVerticesMode mode,
                                          int first_vertex,
                                          int n_vertices,
                                          CoglAttribute **attributes,
                                          int n_attributes,
                                          CoglDrawFlags flags)
{
  CoglContext *ctx = framebuffer->context;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  vk_fb->vertices_mode = mode;

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer);

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  _cogl_framebuffer_vulkan_flush_viewport_state (framebuffer);

  VK ( ctx, vkCmdDraw (vk_fb->cmd_buffer, n_vertices, 1, first_vertex, 0) );
  vk_fb->cmd_buffer_length++;
}

static size_t
sizeof_indices_type (CoglIndicesType type)
{
  switch (type)
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      return 1;
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      return 2;
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      return 4;
    }
  g_return_val_if_reached (0);
}

void
_cogl_framebuffer_vulkan_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                                  CoglPipeline *pipeline,
                                                  CoglVerticesMode mode,
                                                  int first_vertex,
                                                  int n_vertices,
                                                  CoglIndices *indices,
                                                  CoglAttribute **attributes,
                                                  int n_attributes,
                                                  CoglDrawFlags flags)
{
  CoglBuffer *indices_buffer = COGL_BUFFER (indices->buffer);
  CoglBufferVulkan *vk_buf = indices_buffer->winsys;
  CoglContext *ctx = framebuffer->context;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  vk_fb->vertices_mode = mode;

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer);

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  _cogl_framebuffer_vulkan_flush_viewport_state (framebuffer);

  VK ( ctx, vkCmdBindIndexBuffer (vk_fb->cmd_buffer, vk_buf->buffer,
                                  indices->offset,
                                  _cogl_indices_type_to_vulkan_indices_type (indices->type)) );

  VK ( ctx, vkCmdDrawIndexed (vk_fb->cmd_buffer,
                              n_vertices, 1,
                              indices->offset, first_vertex,
                              1 /* TODO: Figure out why 1... */) );
  vk_fb->cmd_buffer_length++;
}

CoglBool
_cogl_framebuffer_vulkan_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                                  int x,
                                                  int y,
                                                  CoglReadPixelsFlags source,
                                                  CoglBitmap *bitmap,
                                                  CoglError **error)
{
  VK_TODO();
  /* vkCmdCopyImageToBuffer */
  return FALSE;
}


void
_cogl_offscreen_vulkan_free (CoglOffscreen *offscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (offscreen);
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglOffscreenVulkan *vk_off = framebuffer->winsys;

  _cogl_framebuffer_vulkan_deinit (framebuffer);

  if (vk_off->framebuffer != VK_NULL_HANDLE)
    VK (ctx, vkDestroyFramebuffer (vk_ctx->device, vk_off->framebuffer, NULL) );

  g_slice_free (CoglOffscreenVulkan, framebuffer->winsys);
}

CoglBool
_cogl_offscreen_vulkan_allocate (CoglOffscreen *offscreen,
                                 CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (offscreen);
  CoglContext *ctx = framebuffer->context;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  CoglOffscreenVulkan *vk_off = g_slice_new0 (CoglOffscreenVulkan);
  CoglFramebufferVulkan *vk_fb = (CoglFramebufferVulkan *) vk_off;

  /* VkAttachmentDescription attachments_description[2] = { */
  /*   [0] = { */
  /*     .format = _cogl_texture_2d_get_vulkan_format (COGL_TEXTURE_2D (offscreen->texture)), */
  /*     .samples = VK_SAMPLE_COUNT_1_BIT, */
  /*     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, */
  /*     .storeOp = VK_ATTACHMENT_STORE_OP_STORE, */
  /*     .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, */
  /*     .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, */
  /*     .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, */
  /*     .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, */
  /*   }, */
  /*   [1] = { */
  /*     .format = VK_FORMAT_D16_UNORM, */
  /*     .samples = VK_SAMPLE_COUNT_1_BIT, */
  /*     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, */
  /*     .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, */
  /*     .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, */
  /*     .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, */
  /*     .initialLayout = */
  /*     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, */
  /*     .finalLayout = */
  /*     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, */
  /*   }, */
  /* }; */
  /* VkAttachmentReference color_reference = { */
  /*   .attachment = 0, */
  /*   .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, */
  /* }; */
  /* VkAttachmentReference depth_reference = { */
  /*   .attachment = 1, */
  /*   .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, */
  /* }; */
  /* VkSubpassDescription subpass_description = { */
  /*   .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, */
  /*   .flags = 0, */
  /*   .inputAttachmentCount = 0, */
  /*   .pInputAttachments = NULL, */
  /*   .colorAttachmentCount = 1, */
  /*   .pColorAttachments = &color_reference, */
  /*   .pResolveAttachments = NULL, */
  /*   .pDepthStencilAttachment = NULL, */
  /*   .preserveAttachmentCount = 0, */
  /*   .pPreserveAttachments = NULL, */
  /* }; */
  /* VkRenderPassCreateInfo render_pass_info = { */
  /*   .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, */
  /*   .pNext = NULL, */
  /*   .attachmentCount = framebuffer->depth_writing_enabled ? 2 : 1, */
  /*   .pAttachments = attachments_description, */
  /*   .subpassCount = 1, */
  /*   .pSubpasses = &subpass_description, */
  /*   .dependencyCount = 0, */
  /*   .pDependencies = NULL, */
  /* }; */
  VkResult result;
  /* int level_width; */
  /* int level_height; */

  framebuffer->winsys = vk_fb;

  /* _COGL_RETURN_VAL_IF_FAIL (offscreen->texture_level < */
  /*                           _cogl_texture_get_n_levels (offscreen->texture), */
  /*                           FALSE); */

  /* _cogl_texture_get_level_size (offscreen->texture, */
  /*                               offscreen->texture_level, */
  /*                               &level_width, */
  /*                               &level_height, */
  /*                               NULL); */

  /* if (framebuffer->depth_writing_enabled) */
  /*   { */
  /*     /\* TODO: deal with this case *\/ */
  /*     g_assert (offscreen->depth_texture == NULL); */

  /*     vk_fb->depth_format = VK_FORMAT_D16_UNORM; */
  /*     if (!_cogl_framebuffer_vulkan_allocate_depth_buffer (framebuffer, error)) */
  /*       return FALSE; */

  /*     subpass_description.pDepthStencilAttachment = &depth_reference; */
  /*   } */

  /* VK_RET_VAL_ERROR ( ctx, */
  /*                    vkCreateRenderPass (vk_ctx->device, &render_pass_info, */
  /*                                        NULL, &vk_fb->render_pass), */
  /*                    FALSE, */
  /*                    error, COGL_FRAMEBUFFER_ERROR, */
  /*                    COGL_FRAMEBUFFER_ERROR_ALLOCATE ); */

  if (!_cogl_framebuffer_vulkan_init (framebuffer, error))
    return FALSE;

  result =
    _cogl_framebuffer_vulkan_create_framebuffer (framebuffer,
                                                 _cogl_texture_2d_get_vulkan_image_view (COGL_TEXTURE_2D (offscreen->texture)),
                                                 &vk_off->framebuffer);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "%s: VK error (%d): %s\n",
                       G_STRLOC,
                       result,
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  _cogl_framebuffer_vulkan_update_framebuffer (framebuffer, vk_off->framebuffer);

  return TRUE;

 error:
  _cogl_offscreen_vulkan_free (offscreen);
  return FALSE;
}
