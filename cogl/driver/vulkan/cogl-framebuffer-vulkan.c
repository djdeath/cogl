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

#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-util-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-vulkan-private.h"

void
_cogl_framebuffer_vulkan_deinit (CoglFramebuffer *framebuffer)
{
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  if (vk_fb->cmd_buffer)
    vkFreeCommandBuffers (vk_ctx->device, vk_ctx->cmd_pool,
                          1, &vk_fb->cmd_buffer);
  if (vk_fb->render_pass != VK_NULL_HANDLE)
    vkDestroyRenderPass (vk_ctx->device, vk_fb->render_pass, NULL);
}

CoglBool
_cogl_framebuffer_vulkan_init (CoglFramebuffer *framebuffer,
                               CoglError **error)
{
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkResult result;

  g_message ("Creating render pass");

  result = vkCreateRenderPass (vk_ctx->device, &(VkRenderPassCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = (VkAttachmentDescription[]) {
        {
          .format = _cogl_pixel_format_to_vulkan_format (framebuffer->internal_format,
                                                         NULL),
          .samples = 1,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        }
      },
      .subpassCount = 1,
      .pSubpasses = (VkSubpassDescription []) {
        {
          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
          .inputAttachmentCount = 0,
          .colorAttachmentCount = 1,
          .pColorAttachments = (VkAttachmentReference []) {
            {
              .attachment = 0,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            }
          },
          .pResolveAttachments = (VkAttachmentReference []) {
            {
              .attachment = VK_ATTACHMENT_UNUSED,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            }
          },
          .pDepthStencilAttachment = NULL,
          .preserveAttachmentCount = 1,
          .pPreserveAttachments = (uint32_t []) { 0 },
        }
      },
      .dependencyCount = 0
    },
    NULL,
    &vk_fb->render_pass);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to create render pass : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  return TRUE;
}

void
_cogl_framebuffer_vulkan_update_framebuffer (CoglFramebuffer *framebuffer,
                                             VkFramebuffer vk_framebuffer)
{
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  /* TODO: Need to ensure to end the previous command buffer if any. */

  vk_fb->framebuffer = vk_framebuffer;
}

static CoglBool
_cogl_framebuffer_vulkan_ensure_command_buffer (CoglFramebuffer *framebuffer,
                                                CoglError **error)
{
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  VkResult result;

  if (vk_fb->cmd_buffer != VK_NULL_HANDLE)
    return TRUE;

  g_message ("Creating command buffer");

  result = vkAllocateCommandBuffers (vk_ctx->device, &(VkCommandBufferAllocateInfo) {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = vk_ctx->cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    },
    &vk_fb->cmd_buffer);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to allocate command buffer : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkBeginCommandBuffer (vk_fb->cmd_buffer, &(VkCommandBufferBeginInfo) {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = 0
    });
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to begin command buffer : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  vkCmdBeginRenderPass (vk_fb->cmd_buffer, &(VkRenderPassBeginInfo) {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = vk_fb->render_pass,
      .framebuffer = vk_fb->framebuffer,
      .renderArea = {
        { 0, 0 },
        { cogl_framebuffer_get_width (framebuffer),
          cogl_framebuffer_get_height (framebuffer) },
      },
      .clearValueCount = 1,
      .pClearValues = (VkClearValue []) {
        { .color = { .float32 = { 0.2f, 0.2f, 0.2f, 1.0f } } }
      },
    },
    VK_SUBPASS_CONTENTS_INLINE);

  return TRUE;
}

/* static void */
/* _cogl_framebuffer_vulkan_flush_viewport_state (CoglFramebuffer *framebuffer) */
/* { */
/*   CoglFramebufferVulkan *vk_fb = framebuffer->winsys; */
/*   VkViewport vk_viewport; */

/*   _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer, NULL); */

/*   g_assert (framebuffer->viewport_width >=0 && */
/*             framebuffer->viewport_height >=0); */

/*   vk_viewport.x = framebuffer->viewport_x; */
/*   vk_viewport.width = framebuffer->viewport_width; */
/*   vk_viewport.height = framebuffer->viewport_height; */
/*   vk_viewport.minDepth = 0; */
/*   vk_viewport.maxDepth = 1; */

/*   /\* Convert the Cogl viewport y offset to an OpenGL viewport y offset */
/*    * NB: OpenGL defines its window and viewport origins to be bottom */
/*    * left, while Cogl defines them to be top left. */
/*    * NB: We render upside down to offscreen framebuffers so we don't */
/*    * need to convert the y offset in this case. *\/ */
/*   if (cogl_is_offscreen (framebuffer)) */
/*     vk_viewport.y = framebuffer->viewport_y; */
/*   else */
/*     vk_viewport.y = framebuffer->height - */
/*       (framebuffer->viewport_y + framebuffer->viewport_height); */

/*   COGL_NOTE (VULKAN, "Setting viewport to (%f, %f, %f, %f)", */
/*              vk_viewport.x, */
/*              vk_viewport.y, */
/*              vk_viewport.width, */
/*              vk_viewport.height); */

/*   vkCmdSetViewport (vk_fb->cmd_buffer, 0, 1, &vk_viewport); */
/* } */

void
_cogl_clip_stack_vulkan_flush (CoglClipStack *stack,
                               CoglFramebuffer *framebuffer)
{
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;
  int x0, y0, x1, y1;
  VkRect2D vk_rect;

  _cogl_clip_stack_get_bounds (stack, &x0, &y0, &x1, &y1);

  vk_rect.offset.x = x0;
  vk_rect.offset.y = y0;
  vk_rect.extent.width = x1 - x0;
  vk_rect.extent.height = y1 - y0;

  vkCmdSetScissor (vk_fb->cmd_buffer, 0, 1, &vk_rect);
}

void
_cogl_framebuffer_vulkan_flush_state (CoglFramebuffer *draw_buffer,
                                      CoglFramebuffer *read_buffer,
                                      CoglFramebufferState state)
{
  CoglContextVulkan *vk_ctx = draw_buffer->context->winsys;
  CoglFramebufferVulkan *vk_fb = draw_buffer->winsys;

  if (vk_fb->cmd_buffer == VK_NULL_HANDLE)
    return;

  vkCmdEndRenderPass (vk_fb->cmd_buffer);
  vkEndCommandBuffer (vk_fb->cmd_buffer);

  vkQueueSubmit (vk_ctx->queue, 1, &(VkSubmitInfo) {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      &vk_fb->cmd_buffer,
    },
    vk_ctx->fence);

  vk_fb->cmd_buffer = VK_NULL_HANDLE;
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
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer, NULL);

  vkCmdClearAttachments (vk_fb->cmd_buffer, 1, &(VkClearAttachment) {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue = {
        .color = {
          .float32 = { red, green, blue, alpha },
        },
      }
    }, 1, &(VkClearRect) {
      .rect = {
        .offset = { .x = 0, .y = 0, },
        .extent = { .width = framebuffer->width,
                    .height = framebuffer->height, },
      },
      .baseArrayLayer = 0,
      .layerCount = 1,
    });
}

void
_cogl_framebuffer_vulkan_query_bits (CoglFramebuffer *framebuffer,
                                     CoglFramebufferBits *bits)
{
  VK_TODO();
}

void
_cogl_framebuffer_vulkan_finish (CoglFramebuffer *framebuffer)
{
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;

  vkWaitForFences (vk_ctx->device, 1, (VkFence[]) { vk_ctx->fence },
                   VK_TRUE, INT64_MAX);
}

void
_cogl_framebuffer_vulkan_discard_buffers (CoglFramebuffer *framebuffer,
                                          unsigned long buffers)
{
  VK_TODO();
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
  CoglFramebufferVulkan *vk_fb = framebuffer->winsys;

  _cogl_framebuffer_vulkan_ensure_command_buffer (framebuffer, NULL);

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  /* Draw call here */
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
  VK_TODO();
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
  return FALSE;
}


void
_cogl_offscreen_vulkan_free (CoglOffscreen *offscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (offscreen);
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglOffscreenVulkan *vk_off = framebuffer->winsys;

  _cogl_framebuffer_vulkan_deinit (framebuffer);

  if (vk_off->framebuffer != VK_NULL_HANDLE)
    vkDestroyFramebuffer (vk_ctx->device, vk_off->framebuffer, NULL);
  if (vk_off->image != VK_NULL_HANDLE)
    vkDestroyImage (vk_ctx->device, vk_off->image, NULL);
  if (vk_off->image_view != VK_NULL_HANDLE)
    vkDestroyImageView (vk_ctx->device, vk_off->image_view, NULL);

  g_slice_free (CoglOffscreenVulkan, framebuffer->winsys);
}

CoglBool
_cogl_offscreen_vulkan_allocate (CoglOffscreen *offscreen,
                                 CoglError **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (offscreen);
  CoglContext *ctx = framebuffer->context;
  CoglOffscreenVulkan *vk_off = g_slice_new0 (CoglOffscreenVulkan);
  CoglFramebufferVulkan *vk_fb = (CoglFramebufferVulkan *) vk_off;
  CoglContextVulkan *vk_ctx = ctx->winsys;
  int level_width;
  int level_height;
  VkResult result;

  framebuffer->winsys = vk_fb;

  _COGL_RETURN_VAL_IF_FAIL (offscreen->texture_level <
                            _cogl_texture_get_n_levels (offscreen->texture),
                            FALSE);

  _cogl_texture_get_level_size (offscreen->texture,
                                offscreen->texture_level,
                                &level_width,
                                &level_height,
                                NULL);

  if (framebuffer->config.depth_texture_enabled &&
      offscreen->depth_texture == NULL)
    {
      offscreen->depth_texture =
        create_depth_texture (ctx,
                              level_width,
                              level_height);

      if (!cogl_texture_allocate (offscreen->depth_texture, error))
        {
          cogl_object_unref (offscreen->depth_texture);
          offscreen->depth_texture = NULL;
          return FALSE;
        }

      _cogl_texture_associate_framebuffer (offscreen->depth_texture, framebuffer);
    }

  result = vkCreateImageView (vk_ctx->device, &(VkImageViewCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = _cogl_texture_2d_get_vulkan_image (COGL_TEXTURE_2D (offscreen->texture)),
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = _cogl_texture_2d_get_vulkan_format (COGL_TEXTURE_2D (offscreen->texture)),
      .components = COGL_VULKAN_COMPONENT_MAPPING_IDENTIFY,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    },
    NULL,
    &vk_off->image_view);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to create framebuffer image view : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  result = vkCreateFramebuffer (vk_ctx->device, &(VkFramebufferCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &vk_off->image_view,
      .width = cogl_texture_get_width (offscreen->texture),
      .height = cogl_texture_get_height (offscreen->texture),
      .layers = 1
    },
    NULL,
    &vk_off->framebuffer);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to create framebuffer : %s",
                       _cogl_vulkan_error_to_string (result));
      goto error;
    }

  if (!_cogl_framebuffer_vulkan_init (framebuffer, error))
    goto error;

  _cogl_framebuffer_vulkan_update_framebuffer (framebuffer, vk_off->framebuffer);

  return TRUE;

 error:
  _cogl_offscreen_vulkan_free (offscreen);
  return FALSE;
}
