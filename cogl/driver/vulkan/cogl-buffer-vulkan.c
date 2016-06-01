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

#include <string.h>

#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-buffer-vulkan-private.h"
#include "cogl-error-private.h"
#include "cogl-util-vulkan-private.h"

#define BUFFER_MEMORY_PROPERTIES (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | \
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)

static VkBufferUsageFlags
_cogl_buffer_usage_to_vulkan_buffer_usage (CoglBufferUsageHint usage)
{
  switch (usage)
    {
    case COGL_BUFFER_USAGE_HINT_TEXTURE:
      return VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    case COGL_BUFFER_USAGE_HINT_ATTRIBUTE_BUFFER:
      return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case COGL_BUFFER_USAGE_HINT_INDEX_BUFFER:
      return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case COGL_BUFFER_USAGE_HINT_UNIFORM_BUFFER:
      return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    default:
      g_assert_not_reached();
    }
}

void
_cogl_buffer_vulkan_create (CoglBuffer *buffer)
{
  CoglContextVulkan *vk_ctx = buffer->context->winsys;
  CoglBufferVulkan *vk_buffer = g_slice_new0 (CoglBufferVulkan);
  VkMemoryRequirements mem_reqs;
  VkResult result;

  buffer->winsys = vk_buffer;

  result = vkCreateBuffer (vk_ctx->device, &(VkBufferCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = buffer->size,
      .usage = _cogl_buffer_usage_to_vulkan_buffer_usage (buffer->usage_hint),
      .flags = VK_SHARING_MODE_EXCLUSIVE,
    },
    NULL,
    &vk_buffer->buffer);
  if (result != VK_SUCCESS)
    {
      g_warning ("%s: Cannot create buffer (%d): %s\n", G_STRLOC, result,
                 _cogl_vulkan_error_to_string (result));
      return;
    }

  vkGetBufferMemoryRequirements (vk_ctx->device, vk_buffer->buffer, &mem_reqs);

  result = vkAllocateMemory (vk_ctx->device, &(VkMemoryAllocateInfo) {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_reqs.size,
      .memoryTypeIndex =
        _cogl_vulkan_context_get_memory_heap (buffer->context,
                                              mem_reqs.memoryTypeBits),
    },
    NULL,
    &vk_buffer->memory);
  if (result != VK_SUCCESS)
    {
      g_warning ("%s: Cannot allocate buffer memory (%d): %s\n", G_STRLOC, result,
                 _cogl_vulkan_error_to_string (result));
      return;
    }

  result = vkBindBufferMemory (vk_ctx->device,
                               vk_buffer->buffer,
                               vk_buffer->memory, 0);
  if (result != VK_SUCCESS)
    {
      g_warning ("%s: Cannot bind buffer memory (%d): %s\n", G_STRLOC, result,
                 _cogl_vulkan_error_to_string (result));
      return;
    }
}

void
_cogl_buffer_vulkan_destroy (CoglBuffer *buffer)
{
  CoglContextVulkan *vk_ctx = buffer->context->winsys;
  CoglBufferVulkan *vk_buffer = buffer->winsys;

  if (vk_buffer->buffer != VK_NULL_HANDLE)
    vkDestroyBuffer (vk_ctx->device, vk_buffer->buffer, NULL);
  if (vk_buffer->memory != VK_NULL_HANDLE)
    vkFreeMemory (vk_ctx->device, vk_buffer->memory, NULL);

  g_slice_free (CoglBufferVulkan, vk_buffer);
}

void *
_cogl_buffer_vulkan_map_range (CoglBuffer *buffer,
                               size_t offset,
                               size_t size,
                               CoglBufferAccess access,
                               CoglBufferMapHint hints,
                               CoglError **error)
{
  CoglContextVulkan *vk_ctx = buffer->context->winsys;
  CoglBufferVulkan *vk_buffer = buffer->winsys;
  void *data;
  VkResult result;

  if (vk_buffer->buffer == VK_NULL_HANDLE)
    {
      _cogl_set_error (error, COGL_BUFFER_ERROR,
                       COGL_BUFFER_ERROR_MAP,
                       "Buffer not allocated");
      return NULL;
    }

  result = vkMapMemory (vk_ctx->device,
                        vk_buffer->memory,
                        offset,
                        size,
                        0,
                        &data);
  if (result != VK_SUCCESS)
    {
      _cogl_set_error (error, COGL_BUFFER_ERROR,
                       COGL_BUFFER_ERROR_MAP,
                       "Failed to map buffer : %s",
                       _cogl_vulkan_error_to_string (result));
      return NULL;
    }

  buffer->flags |= COGL_BUFFER_FLAG_MAPPED;

  vk_buffer->memory_need_flush = (access & COGL_BUFFER_ACCESS_WRITE);
  vk_buffer->memory_map_offset = offset;
  vk_buffer->memory_map_size = size;

  return data;
}

void
_cogl_buffer_vulkan_unmap (CoglBuffer *buffer)
{
  CoglContextVulkan *vk_ctx = buffer->context->winsys;
  CoglBufferVulkan *vk_buffer = buffer->winsys;
  VkResult result;

  if (vk_buffer->memory_need_flush)
    {
      vk_buffer->memory_need_flush = FALSE;

      result = vkFlushMappedMemoryRanges (vk_ctx->device, 1, &(VkMappedMemoryRange) {
          .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
          .memory = vk_buffer->memory,
          .offset = vk_buffer->memory_map_offset,
          .size = vk_buffer->memory_map_size,
        });
      if (result != VK_SUCCESS)
        {
          g_warning ("%s: Cannot flush memory (%d): %s", G_STRLOC, result,
                     _cogl_vulkan_error_to_string (result));
          return;
        }
    }

  vkUnmapMemory (vk_ctx->device, vk_buffer->memory);

  buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED;
}

CoglBool
_cogl_buffer_vulkan_set_data (CoglBuffer *buffer,
                              unsigned int offset,
                              const void *data,
                              unsigned int size,
                              CoglError **error)
{
  CoglContextVulkan *vk_ctx = buffer->context->winsys;
  void *data_map;
  VkResult result;

  if (buffer->flags & COGL_BUFFER_FLAG_MAPPED)
    {
      _cogl_set_error (error, COGL_BUFFER_ERROR,
                       COGL_BUFFER_ERROR_MAP,
                       "Cannot set data while the buffer is mapped");
      return FALSE;
    }

  data_map = _cogl_buffer_vulkan_map_range (buffer, offset, size,
                                            COGL_BUFFER_ACCESS_WRITE,
                                            COGL_BUFFER_MAP_HINT_DISCARD_RANGE,
                                            error);
  if (!data_map)
    return FALSE;

  memcpy (data_map, data, size);

  _cogl_buffer_vulkan_unmap (buffer);

  return TRUE;
}
