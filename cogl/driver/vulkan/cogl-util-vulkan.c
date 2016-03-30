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

#include "cogl-util-vulkan-private.h"

VkFormat
_cogl_pixel_format_to_vulkan_format (CoglPixelFormat format,
                                     CoglBool *premultiplied)
{

  if (premultiplied)
    *premultiplied = (COGL_PREMULT_BIT & format) != 0;

  switch (format)
    {
    case COGL_PIXEL_FORMAT_RGBA_8888:
    case COGL_PIXEL_FORMAT_RGBA_8888_PRE:
      return VK_FORMAT_B8G8R8A8_SRGB;

      /* TODO(dixit Mesa): Figure out what all the formats mean and make
       * this table correct.
       */
    /* case COGL_PIXEL_FORMAT_RGB_565: */
    /*   return VK_FORMAT_R5G6B5_UNORM_PACK16; */
    /* case COGL_PIXEL_FORMAT_RGBA_4444: */
    /*   return VK_FORMAT_R4G4B4A4_UNORM_PACK16; */
    /* case COGL_PIXEL_FORMAT_RGBA_5551: */
    /*   return VK_FORMAT_R5G5B5A1_UNORM_PACK16; */
    /* case COGL_PIXEL_FORMAT_G_8: */
    /*     return VK_FORMAT_R8_SRGB; */
    /* case COGL_PIXEL_FORMAT_RG_88: */
    /*   return VK_FORMAT_R8G8_SRGB; */
    /* case COGL_PIXEL_FORMAT_RGB_888: */
    /*   return VK_FORMAT_R8G8B8_SRGB; */
    /* case COGL_PIXEL_FORMAT_BGR_888: */
    /*   return VK_FORMAT_B8G8R8_SRGB; */

    /* case COGL_PIXEL_FORMAT_RGBA_8888: */
    /* case COGL_PIXEL_FORMAT_RGBA_8888_PRE: */
    /*   return VK_FORMAT_R8G8B8A8_SRGB; */
    /* case COGL_PIXEL_FORMAT_BGRA_8888: */
    /* case COGL_PIXEL_FORMAT_BGRA_8888_PRE: */
    /*   return VK_FORMAT_B8G8R8A8_SRGB; */
    /* case COGL_PIXEL_FORMAT_ABGR_8888: */
    /* case COGL_PIXEL_FORMAT_ABGR_8888_PRE: */
    /*   return VK_FORMAT_A8B8G8R8_SRGB_PACK32; */

    default:
      return VK_FORMAT_UNDEFINED;
    }
}

static VkFormat _attributes_to_formats[5][4] = {
  /* COGL_ATTRIBUTE_TYPE_BYTE */
  { VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT },
  /* COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE */
  { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT },
  /* COGL_ATTRIBUTE_TYPE_SHORT */
  { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT },
  /* COGL_ATTRIBUTE_TYPE_UNSIGNED_SHORT */
  { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT },
  /* COGL_ATTRIBUTE_TYPE_FLOAT */
  { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT },
};

VkFormat
_cogl_attribute_type_to_vulkan_format (CoglAttributeType type, int n_components)
{
  g_assert (n_components <= 4);
  g_assert (type <= COGL_ATTRIBUTE_TYPE_FLOAT);

  return _attributes_to_formats[type][n_components - 1];
}

const char *
_cogl_vulkan_error_to_string (VkResult error)
{
  switch (error)
    {
    case VK_NOT_READY:
      return "not ready";
    case VK_TIMEOUT:
      return "timeout";
    case VK_INCOMPLETE:
      return "incomplete";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "out of host memory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "out of device memory";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "initialization failed";
    case VK_ERROR_DEVICE_LOST:
      return "device lost";
    case VK_ERROR_MEMORY_MAP_FAILED:
      return "memory map failed";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "layer not present";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "extension not present";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "feature not present";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "incompatible driver";
    case VK_ERROR_TOO_MANY_OBJECTS:
      return "too many objects";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "format not supported";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "surface lost";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
       return "native window in use";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "out of date khr";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
      return "incompatible display khr";
    case VK_ERROR_VALIDATION_FAILED_EXT:
      return "validation failed ext";
    default:
      return "unknown";
    }
}

void
_cogl_pipeline_filter_to_vulkan_filter (CoglPipelineFilter filter,
                                        VkFilter *vkfilter,
                                        VkSamplerMipmapMode *vksamplermode)
{
  VkFilter _vkfilter = VK_FILTER_NEAREST;
  VkSamplerMipmapMode _vksamplermode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

  switch (filter)
    {
    case COGL_PIPELINE_FILTER_NEAREST:
      _vkfilter = VK_FILTER_NEAREST;
      break;
    case COGL_PIPELINE_FILTER_LINEAR:
      _vkfilter = VK_FILTER_LINEAR;
      break;
    case COGL_PIPELINE_FILTER_NEAREST_MIPMAP_NEAREST:
      _vkfilter = VK_FILTER_NEAREST;
      _vksamplermode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case COGL_PIPELINE_FILTER_LINEAR_MIPMAP_NEAREST:
      _vkfilter = VK_FILTER_LINEAR;
      _vksamplermode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case COGL_PIPELINE_FILTER_NEAREST_MIPMAP_LINEAR:
      _vkfilter = VK_FILTER_NEAREST;
      _vksamplermode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case COGL_PIPELINE_FILTER_LINEAR_MIPMAP_LINEAR:
      _vkfilter = VK_FILTER_LINEAR;
      _vksamplermode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    default:
      g_assert_not_reached();
    }

  if (vkfilter)
    *vkfilter = _vkfilter;
  if (vksamplermode)
    *vksamplermode = _vksamplermode;
}

VkSamplerAddressMode
_cogl_pipeline_wrap_mode_to_vulkan_address_mode (CoglPipelineWrapMode mode)
{
  switch (mode)
    {
    case COGL_PIPELINE_WRAP_MODE_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case COGL_PIPELINE_WRAP_MODE_MIRRORED_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case COGL_PIPELINE_WRAP_MODE_AUTOMATIC:
      /* TODO: this isn't accurate with regards to Cogl's API. */
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    default:
      g_assert_not_reached();
    }
}