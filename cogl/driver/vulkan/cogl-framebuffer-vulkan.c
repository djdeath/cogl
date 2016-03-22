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

#include "cogl-context-private.h"
#include "cogl-util-vulkan-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer-vulkan-private.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-texture-vulkan-private.h"
#include "cogl-texture-private.h"

#include <glib.h>
#include <string.h>

static CoglVulkanFramebuffer *
_get_vulkan_framebuffer (CoglFramebuffer *framebuffer)
{
  /* TODO: Figure out how to retrieve that structure from both onscreens and
     offscreens. */
  return NULL;
}

static CoglBool
_ensure_command_buffer (CoglFramebuffer *framebuffer,
                        CoglError **error)
{
  CoglContext *ctx = fb->context;
  CoglVulkanFramebuffer *vk_framebuffer = _get_vulkan_framebuffer (framebuffer);
  VkResult result;

  if (vk_framebuffer->emitting_commands)
    return TRUE;

  result = vkAllocateCommandBuffers (ctx->vk_device,
                                     &(VkCommandBufferAllocateInfo) {
                                       .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = ctx->vk_cmd_pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       .commandBufferCount = 1,
                                     },
                                     &vk_framebuffer->vk_cmd_buffer);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to allocate command buffer : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkBeginCommandBuffer (vk_framebuffer->vk_cmd_buffer,
                                 &(VkCommandBufferBeginInfo) {
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

  vkCmdBeginRenderPass (vk_framebuffer->vk_cmd_buffer,
                        &(VkRenderPassBeginInfo) {
                          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                          .renderPass = vk_framebuffer->vk_render_pass,
                          .framebuffer = vk_framebuffer->vk_framebuffer,
                          .renderArea = {
                            { 0, 0 },
                            { cogl_texture_get_width (framebuffer->width),
                              cogl_texture_get_height (framebuffer->height) }
                          },
                          .clearValueCount = 1,
                          .pClearValues = (VkClearValue []) {
                            { .color = { .float32 = { 0.2f, 0.2f, 0.2f, 1.0f } } }
                          }
                        },
                        VK_SUBPASS_CONTENTS_INLINE);

  return TRUE;
}

static void
_cogl_framebuffer_vulkan_flush_viewport_state (CoglFramebuffer *framebuffer)
{
  CoglVulkanFramebuffer *vk_framebuffer = _get_vulkan_framebuffer (framebuffer);
  VkViewport vk_viewport;

  _ensure_command_buffer (framebuffer, NULL);

  g_assert (framebuffer->viewport_width >=0 &&
            framebuffer->viewport_height >=0);

  vk_viewport.x = framebuffer->viewport_x;
  vk_viewport.width = framebuffer->viewport_width;
  vk_viewport.height = framebuffer->viewport_height;
  vk_viewport.minDepth = 0;
  vk_viewport.maxDepth = 1;

  /* Convert the Cogl viewport y offset to an OpenGL viewport y offset
   * NB: OpenGL defines its window and viewport origins to be bottom
   * left, while Cogl defines them to be top left.
   * NB: We render upside down to offscreen framebuffers so we don't
   * need to convert the y offset in this case. */
  if (cogl_is_offscreen (framebuffer))
    vk_viewport.y = framebuffer->viewport_y;
  else
    vk_viewport.y = framebuffer->height -
      (framebuffer->viewport_y + framebuffer->viewport_height);

  COGL_NOTE (VULKAN, "Setting viewport to (%f, %f, %f, %f)",
             vk_viewport.x,
             vk_viewport.y,
             vk_viewport.width,
             vk_viewport.height);

  vkCmdSetViewport (vk_framebuffer->vk_cmd_buffer, 0, 1, &vk_viewport);
}

void
_cogl_clip_stack_vulkan_flush (CoglClipStack *stack,
                               CoglFramebuffer *framebuffer)
{
  CoglVulkanFramebuffer *vk_framebuffer = _get_vulkan_framebuffer (framebuffer);
  int x0, y0, x1, y1;
  VkRect2D vk_rect;

  _cogl_clip_stack_get_bounds (stack, &x0, &y0, &x1, &y1);

  vk_rect.offset.x = x0;
  vk_rect.offset.y = y0;
  vk_rect.extent.width = x1 - x0;
  vk_rect.extent.height = y1 - y0;

  vkCmdSetScissor (vk_framebuffer->vk_cmd_buffer, 0, 1, &vk_rect);
}

void
_cogl_framebuffer_gl_flush_state (CoglFramebuffer *draw_buffer,
                                  CoglFramebuffer *read_buffer,
                                  CoglFramebufferState state)
{
  /* TODO... */
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

static CoglTexture *
attach_depth_texture (CoglContext *ctx,
                      CoglTexture *depth_texture,
                      CoglOffscreenAllocateFlags flags)
{
  GLuint tex_gl_handle;
  GLenum tex_gl_target;

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH_STENCIL)
    {
      /* attach a GL_DEPTH_STENCIL texture to the GL_DEPTH_ATTACHMENT and
       * GL_STENCIL_ATTACHMENT attachement points */
      g_assert (_cogl_texture_get_format (depth_texture) ==
                COGL_PIXEL_FORMAT_DEPTH_24_STENCIL_8);

      cogl_texture_get_gl_texture (depth_texture,
                                   &tex_gl_handle, &tex_gl_target);

      GE (ctx, glFramebufferTexture2D (GL_FRAMEBUFFER,
                                       GL_DEPTH_ATTACHMENT,
                                       tex_gl_target, tex_gl_handle,
                                       0));
      GE (ctx, glFramebufferTexture2D (GL_FRAMEBUFFER,
                                       GL_STENCIL_ATTACHMENT,
                                       tex_gl_target, tex_gl_handle,
                                       0));
    }
  else if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH)
    {
      /* attach a newly created GL_DEPTH_COMPONENT16 texture to the
       * GL_DEPTH_ATTACHMENT attachement point */
      g_assert (_cogl_texture_get_format (depth_texture) ==
                COGL_PIXEL_FORMAT_DEPTH_16);

      cogl_texture_get_gl_texture (COGL_TEXTURE (depth_texture),
                                   &tex_gl_handle, &tex_gl_target);

      GE (ctx, glFramebufferTexture2D (GL_FRAMEBUFFER,
                                       GL_DEPTH_ATTACHMENT,
                                       tex_gl_target, tex_gl_handle,
                                       0));
    }

  return COGL_TEXTURE (depth_texture);
}

static GList *
try_creating_renderbuffers (CoglContext *ctx,
                            int width,
                            int height,
                            CoglOffscreenAllocateFlags flags,
                            int n_samples)
{
  GList *renderbuffers = NULL;
  GLuint gl_depth_stencil_handle;

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH_STENCIL)
    {
      GLenum format;

      /* WebGL adds a GL_DEPTH_STENCIL_ATTACHMENT and requires that we
       * use the GL_DEPTH_STENCIL format. */
#ifdef HAVE_COGL_WEBGL
      format = GL_DEPTH_STENCIL;
#else
      /* Although GL_OES_packed_depth_stencil is mostly equivalent to
       * GL_EXT_packed_depth_stencil, one notable difference is that
       * GL_OES_packed_depth_stencil doesn't allow GL_DEPTH_STENCIL to
       * be passed as an internal format to glRenderbufferStorage.
       */
      if (_cogl_has_private_feature
          (ctx, COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL))
        format = GL_DEPTH_STENCIL;
      else
        {
          _COGL_RETURN_VAL_IF_FAIL (
            _cogl_has_private_feature (ctx,
              COGL_PRIVATE_FEATURE_OES_PACKED_DEPTH_STENCIL),
            NULL);
          format = GL_DEPTH24_STENCIL8;
        }
#endif

      /* Create a renderbuffer for depth and stenciling */
      GE (ctx, glGenRenderbuffers (1, &gl_depth_stencil_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_depth_stencil_handle));
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      format,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, format,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));


#ifdef HAVE_COGL_WEBGL
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          gl_depth_stencil_handle));
#else
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          gl_depth_stencil_handle));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          gl_depth_stencil_handle));
#endif
      renderbuffers =
        g_list_prepend (renderbuffers,
                        GUINT_TO_POINTER (gl_depth_stencil_handle));
    }

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH)
    {
      GLuint gl_depth_handle;

      GE (ctx, glGenRenderbuffers (1, &gl_depth_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_depth_handle));
      /* For now we just ask for GL_DEPTH_COMPONENT16 since this is all that's
       * available under GLES */
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      GL_DEPTH_COMPONENT16,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, gl_depth_handle));
      renderbuffers =
        g_list_prepend (renderbuffers, GUINT_TO_POINTER (gl_depth_handle));
    }

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_STENCIL)
    {
      GLuint gl_stencil_handle;

      GE (ctx, glGenRenderbuffers (1, &gl_stencil_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_stencil_handle));
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      GL_STENCIL_INDEX8,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, gl_stencil_handle));
      renderbuffers =
        g_list_prepend (renderbuffers, GUINT_TO_POINTER (gl_stencil_handle));
    }

  return renderbuffers;
}

static void
delete_renderbuffers (CoglContext *ctx, GList *renderbuffers)
{
  GList *l;

  for (l = renderbuffers; l; l = l->next)
    {
      GLuint renderbuffer = GPOINTER_TO_UINT (l->data);
      GE (ctx, glDeleteRenderbuffers (1, &renderbuffer));
    }

  g_list_free (renderbuffers);
}

CoglBool
_cogl_offscreen_vulkan_allocate (CoglOffscreen *offscreen,
                                 CoglError **error)
{
  CoglFramebuffer *fb = COGL_FRAMEBUFFER (offscreen);
  CoglContext *ctx = fb->context;
  int level_width;
  int level_height;

  _COGL_RETURN_VAL_IF_FAIL (offscreen->texture_level <
                            _cogl_texture_get_n_levels (offscreen->texture),
                            FALSE);

  _cogl_texture_get_level_size (offscreen->texture,
                                offscreen->texture_level,
                                &level_width,
                                &level_height,
                                NULL);

  if (fb->config.depth_texture_enabled &&
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

      _cogl_texture_associate_framebuffer (offscreen->depth_texture, fb);
    }

  result = vkCreateImageView (ctx->vk_device,
                              &(VkImageViewCreateInfo) {
                                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = _cogl_texture_2d_get_vulkan_texture (COGL_TEXTURE_2d (offscreen->texture)),
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
                              &vk_framebuffer->vk_image_view);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to create framebuffer image view : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkCreateFramebuffer (ctx->vk_device,
                                &(VkFramebufferCreateInfo) {
                                  .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .attachmentCount = 1,
                                  .pAttachments = &vk_framebuffer->vk_image_view,
                                  .width = cogl_texture_get_width (offscreen->texture),
                                  .height = cogl_texture_get_height (offscreen->texture),
                                  .layers = 1
                                },
                                NULL,
                                &vk_framebuffer->vk_framebuffer);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_FRAMEBUFFER_ERROR,
                       COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                       "Failed to create framebuffer : %s",
                       _cogl_vulkan_error_to_string (result));
      return FALSE;
    }

  result = vkCreateRenderPass (ctx->vk_device,
                               &(VkRenderPassCreateInfo) {
                                 .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount = offscreen->depth_texture ? 2 : 1,
                                 .pAttachments = (VkAttachmentDescription[]) {
                                   {
                                     .format = _cogl_pixel_format_to_vulkan_format (fb->internal_format),
                                     .samples = 1,
                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                     .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   },
                                   {
                                     .format = offscreen->depth_texture ?
                                       _cogl_pixel_format_to_vulkan_format (_cogl_texture_get_format (offscreen->depth_texture)) : 0,
                                     .samples = 1,
                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                     .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
                               &vk_framebuffer->vk_render_pass);

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
_cogl_offscreen_vulkan_free (CoglOffscreen *offscreen)
{
  CoglContext *ctx = COGL_FRAMEBUFFER (offscreen)->context;
  CoglVulkanFramebuffer *vk_framebuffer = &offscreen->vulkan_framebuffer;

  vkFreeCommandBuffers (ctx->vk_device, ctx->vk_cmd_pool,
                        1, &vk_framebuffer->vk_cmd_buffer);
  vkDestroyRenderPass (ctx->vk_device, vk_framebuffer->vk_render_pass, NULL);
  vkDestroyFramebuffer (ctx->vk_device, vk_framebuffer->vk_framebuffer, NULL);
  vkDestroyImageView (ctx->vk_device, vk_framebuffer->vk_image_view, NULL);
}

void
_cogl_framebuffer_gl_clear (CoglFramebuffer *framebuffer,
                            unsigned long buffers,
                            float red,
                            float green,
                            float blue,
                            float alpha)
{
  CoglContext *ctx = framebuffer->context;
  CoglVulkanFramebuffer *vk_framebuffer = &offscreen->vulkan_framebuffer;

  _ensure_command_buffer (framebuffer);

  vkCmdClearColorImage (vk_framebuffer->vk_cmd_buffer,
                        vk_framebuffer->vk_image,
                        0,
                        &(VkClearColorValue) {
                          .float32 = { red, green, blue, alpha },
                        },
                        1,
                        &(VkImageSubresourceRange) {
                          .aspectMask =
                            ((buffers & COGL_BUFFER_BIT_COLOR) != 0 ?
                             VK_IMAGE_ASPECT_COLOR_BIT : 0) |
                            ((buffers & COGL_BUFFER_BIT_DEPTH) != 0 ?
                             VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                            ((buffers & COGL_BUFFER_BIT_STENCIL) != 0 ?
                             VK_IMAGE_ASPECT_STENCIL_BIT : 0)
                          .baseMipLevel = 0,
                          .levelCount = 1,
                          .baseArrayLayer = 0,
                          .layerCount = 1,
                        });
}

void
_cogl_framebuffer_vulkan_finish (CoglFramebuffer *framebuffer)
{
  /* TODO... */
}

void
_cogl_framebuffer_vulkan_discard_buffers (CoglFramebuffer *framebuffer,
                                      unsigned long buffers)
{
  /* TODO... */
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
  CoglVulkanFramebuffer *vk_framebuffer = &offscreen->vulkan_framebuffer;

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  vkCmdBindVertexBuffers(cmd_buffer, 0, 3,
                         (VkBuffer[]) {
                           vc->buffer,
                             vc->buffer,
                                                          vc->buffer
                             },
                         (VkDeviceSize[]) {
                           vc->vertex_offset,
                             vc->colors_offset,
                                                          vc->normals_offset
                             });


  GE (framebuffer->context,
      glDrawArrays ((GLenum)mode, first_vertex, n_vertices));
}

void
_cogl_framebuffer_gl_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                              CoglPipeline *pipeline,
                                              CoglVerticesMode mode,
                                              int first_vertex,
                                              int n_vertices,
                                              CoglIndices *indices,
                                              CoglAttribute **attributes,
                                              int n_attributes,
                                              CoglDrawFlags flags)
{
  /* TODO... */
}

CoglBool
_cogl_framebuffer_vulkan_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                                  int x,
                                                  int y,
                                                  CoglReadPixelsFlags source,
                                                  CoglBitmap *bitmap,
                                                  CoglError **error)
{
  /* TODO... */
  return FALSE;
}
