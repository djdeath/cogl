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

extern "C" {

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl-context-private.h"
#include "cogl-debug.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-shader-vulkan-private.h"

}

#include "glslang/InitializeDll.h"
#include "glslang/glslang/Public/ShaderLang.h"
#include "glslang/glslang/MachineIndependent/gl_types.h"

#include "GlslangToSpv.h"
#include "disassemble.h"

#include <sstream>
#include <vector>

struct _CoglShaderVulkan
{
  CoglContext *context;

  CoglGlslShaderType type;

  glslang::TProgram *program;

  VkShaderModule vk_module;
};

static EShLanguage
_cogl_glsl_shader_type_to_es_language (CoglGlslShaderType type)
{
  switch (type)
    {
    case COGL_GLSL_SHADER_TYPE_VERTEX:
      return EShLangVertex;
    case COGL_GLSL_SHADER_TYPE_FRAGMENT:
      return EShLangFragment;
    default:
      g_assert_not_reached();
    }
}

// These numbers come from the OpenGL 4.4 core profile specification Chapter 23
// unless otherwise specified.
const TBuiltInResource kDefaultTBuiltInResource = {
  /*.maxLights = */ 8,         // From OpenGL 3.0 table 6.46.
  /*.maxClipPlanes = */ 6,     // From OpenGL 3.0 table 6.46.
  /*.maxTextureUnits = */ 2,   // From OpenGL 3.0 table 6.50.
  /*.maxTextureCoords = */ 8,  // From OpenGL 3.0 table 6.50.
  /*.maxVertexAttribs = */ 16,
  /*.maxVertexUniformComponents = */ 4096,
  /*.maxVaryingFloats = */ 60,  // From OpenGLES 3.1 table 6.44.
  /*.maxVertexTextureImageUnits = */ 16,
  /*.maxCombinedTextureImageUnits = */ 80,
  /*.maxTextureImageUnits = */ 16,
  /*.maxFragmentUniformComponents = */ 1024,
  /*.maxDrawBuffers = */ 2,
  /*.maxVertexUniformVectors = */ 256,
  /*.maxVaryingVectors = */ 15,  // From OpenGLES 3.1 table 6.44.
  /*.maxFragmentUniformVectors = */ 256,
  /*.maxVertexOutputVectors = */ 16,   // maxVertexOutputComponents / 4
  /*.maxFragmentInputVectors = */ 15,  // maxFragmentInputComponents / 4
  /*.minProgramTexelOffset = */ -8,
  /*.maxProgramTexelOffset = */ 7,
  /*.maxClipDistances = */ 8,
  /*.maxComputeWorkGroupCountX = */ 65535,
  /*.maxComputeWorkGroupCountY = */ 65535,
  /*.maxComputeWorkGroupCountZ = */ 65535,
  /*.maxComputeWorkGroupSizeX = */ 1024,
  /*.maxComputeWorkGroupSizeX = */ 1024,
  /*.maxComputeWorkGroupSizeZ = */ 64,
  /*.maxComputeUniformComponents = */ 512,
  /*.maxComputeTextureImageUnits = */ 16,
  /*.maxComputeImageUniforms = */ 8,
  /*.maxComputeAtomicCounters = */ 8,
  /*.maxComputeAtomicCounterBuffers = */ 1,  // From OpenGLES 3.1 Table 6.43
  /*.maxVaryingComponents = */ 60,
  /*.maxVertexOutputComponents = */ 64,
  /*.maxGeometryInputComponents = */ 64,
  /*.maxGeometryOutputComponents = */ 128,
  /*.maxFragmentInputComponents = */ 128,
  /*.maxImageUnits = */ 8,  // This does not seem to be defined anywhere,
  // set to ImageUnits.
  /*.maxCombinedImageUnitsAndFragmentOutputs = */ 8,
  /*.maxCombinedShaderOutputResources = */ 8,
  /*.maxImageSamples = */ 0,
  /*.maxVertexImageUniforms = */ 0,
  /*.maxTessControlImageUniforms = */ 0,
  /*.maxTessEvaluationImageUniforms = */ 0,
  /*.maxGeometryImageUniforms = */ 0,
  /*.maxFragmentImageUniforms = */ 8,
  /*.maxCombinedImageUniforms = */ 8,
  /*.maxGeometryTextureImageUnits = */ 16,
  /*.maxGeometryOutputVertices = */ 256,
  /*.maxGeometryTotalOutputComponents = */ 1024,
  /*.maxGeometryUniformComponents = */ 512,
  /*.maxGeometryVaryingComponents = */ 60,  // Does not seem to be defined
  // anywhere, set equal to
  // maxVaryingComponents.
  /*.maxTessControlInputComponents = */ 128,
  /*.maxTessControlOutputComponents = */ 128,
  /*.maxTessControlTextureImageUnits = */ 16,
  /*.maxTessControlUniformComponents = */ 1024,
  /*.maxTessControlTotalOutputComponents = */ 4096,
  /*.maxTessEvaluationInputComponents = */ 128,
  /*.maxTessEvaluationOutputComponents = */ 128,
  /*.maxTessEvaluationTextureImageUnits = */ 16,
  /*.maxTessEvaluationUniformComponents = */ 1024,
  /*.maxTessPatchComponents = */ 120,
  /*.maxPatchVertices = */ 32,
  /*.maxTessGenLevel = */ 64,
  /*.maxViewports = */ 16,
  /*.maxVertexAtomicCounters = */ 0,
  /*.maxTessControlAtomicCounters = */ 0,
  /*.maxTessEvaluationAtomicCounters = */ 0,
  /*.maxGeometryAtomicCounters = */ 0,
  /*.maxFragmentAtomicCounters = */ 8,
  /*.maxCombinedAtomicCounters = */ 8,
  /*.maxAtomicCounterBindings = */ 1,
  /*.maxVertexAtomicCounterBuffers = */ 0,  // From OpenGLES 3.1 Table 6.41.

  // ARB_shader_atomic_counters.
  /*.maxTessControlAtomicCounterBuffers = */ 0,
  /*.maxTessEvaluationAtomicCounterBuffers = */ 0,
  /*.maxGeometryAtomicCounterBuffers = */ 0,
  // /ARB_shader_atomic_counters.

  /*.maxFragmentAtomicCounterBuffers = */ 0,  // From OpenGLES 3.1 Table 6.43.
  /*.maxCombinedAtomicCounterBuffers = */ 1,
  /*.maxAtomicCounterBufferSize = */ 32,
  /*.maxTransformFeedbackBuffers = */ 4,
  /*.maxTransformFeedbackInterleavedComponents = */ 64,
  /*.maxCullDistances = */ 8,                 // ARB_cull_distance.
  /*.maxCombinedClipAndCullDistances = */ 8,  // ARB_cull_distance.
  /*.maxSamples = */ 4,
  // This is the glslang TLimits structure.
  // It defines whether or not the following features are enabled.
  // We want them to all be enabled.
  /*.limits = */ {
    /*.nonInductiveForLoops = */ 1,
    /*.whileLoops = */ 1,
    /*.doWhileLoops = */ 1,
    /*.generalUniformIndexing = */ 1,
    /*.generalAttributeMatrixVectorIndexing = */ 1,
    /*.generalVaryingIndexing = */ 1,
    /*.generalSamplerIndexing = */ 1,
    /*.generalVariableIndexing = */ 1,
    /*.generalConstantMatrixVectorIndexing = */ 1,
  }
};

extern "C" void
_cogl_shader_vulkan_free (CoglShaderVulkan *desc)
{
  CoglContextVulkan *vk_ctx = (CoglContextVulkan *) desc->context->winsys;

  if (desc->vk_module)
    vkDestroyShaderModule (vk_ctx->device, desc->vk_module, NULL);
  delete desc->program;
  g_slice_free (CoglShaderVulkan, desc);
}

extern "C" CoglShaderVulkan *
_cogl_shader_vulkan_new (CoglContext *context, CoglGlslShaderType type)
{
  CoglShaderVulkan *desc = g_slice_new0 (CoglShaderVulkan);

  glslang::InitializeProcess ();

  desc->context = context;
  desc->type = type;
  desc->program = new glslang::TProgram();

  return desc;
}

extern "C" void
_cogl_shader_vulkan_set_source (CoglShaderVulkan *shader,
                                const char *string)
{
  glslang::TShader *gl_shader =
    new glslang::TShader (_cogl_glsl_shader_type_to_es_language (shader->type));

  gl_shader->setStrings(&string, 1);
  bool success = gl_shader->parse(&kDefaultTBuiltInResource,
                                  450,
                                  ENoProfile,
                                  false,
                                  false,
                                  static_cast<EShMessages>(EShMsgDefault |
                                                           EShMsgSpvRules |
                                                           EShMsgVulkanRules),
                                  glslang::TShader::ForbidInclude());

  if (!success)
    COGL_NOTE (SPIRV, "Shader compilation failed : %s\n%s\n",
               gl_shader->getInfoLog(), gl_shader->getInfoDebugLog());

  shader->program->addShader (gl_shader);
}

extern "C" CoglBool
_cogl_shader_vulkan_link (CoglShaderVulkan *shader)
{
  return shader->program->link (static_cast<EShMessages>(EShMsgDefault)) &&
    shader->program->buildReflection ();
}

#define ACCESSOR_0(type, default_value, cogl_name, glslang_lang)        \
  extern "C" type                                                       \
  _cogl_shader_vulkan_##cogl_name (CoglShaderVulkan *shader)            \
  {                                                                     \
    return shader->program->glslang_lang();                             \
  }
#define ACCESSOR(type, default_value, cogl_name, glslang_call, args...) \
  extern "C" type                                                       \
  _cogl_shader_vulkan_##cogl_name (CoglShaderVulkan *shader, args)      \
  {                                                                     \
    return shader->program->glslang_call;                               \
  }

ACCESSOR_0(int, -1, get_num_live_uniform_variables, getNumLiveUniformVariables)
ACCESSOR_0(int, -1, get_num_live_uniform_blocks, getNumLiveUniformBlocks)
ACCESSOR(const char *, NULL, get_uniform_name, getUniformName(index), int index)
ACCESSOR(const char *, NULL, get_uniform_block_name, getUniformBlockName(index), int index)
ACCESSOR(int, -1, get_uniform_block_size, getUniformBlockSize(index), int index)
ACCESSOR(int, -1, get_uniform_index, getUniformIndex(name), const char *name)
ACCESSOR(int, -1, get_uniform_block_index, getUniformBlockIndex(index), int index)
ACCESSOR(int, -1, get_uniform_type, getUniformType(index), int index)
ACCESSOR(int, -1, get_uniform_buffer_offset, getUniformBufferOffset(index), int index)
ACCESSOR(int, -1, get_uniform_array_size, getUniformArraySize(index), int index)

ACCESSOR_0(int, -1, get_num_live_input_attributes, getNumLiveInputAttributes);
ACCESSOR_0(int, -1, get_num_live_output_attributes, getNumLiveOutputAttributes);
ACCESSOR(int, -1, get_input_attribute_location,
         getInputAttributeLocation(name), const char *name)
ACCESSOR(int, -1, get_output_attribute_location,
         getOutputAttributeLocation(name), const char *name)

extern "C" void *
_cogl_shader_vulkan_stage_to_spirv (CoglShaderVulkan *shader,
                                    CoglGlslShaderType type,
                                    uint32_t *size)
{
  const glslang::TIntermediate *intermediate =
    shader->program->getIntermediate(_cogl_glsl_shader_type_to_es_language (type));

  std::vector<unsigned int> spirv_data;
  glslang::GlslangToSpv(*intermediate, spirv_data);

  uint32_t spirv_size = spirv_data.size() * sizeof(spirv_data[0]);
  if (spirv_size < 1)
    return NULL;

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SPIRV))) {
    std::stringstream spirv_dbg;
    spv::Disassemble(spirv_dbg, spirv_data);

    COGL_NOTE (SPIRV, "Spirv output size=%u bytes : \n%s",
               spirv_size, spirv_dbg.str().c_str());
  }

  /* TODO: cache this */
  void *ret = g_malloc (spirv_size);
  memcpy (ret, &spirv_data[0], spirv_size);

  if (size)
    *size = spirv_size;

  return ret;
}

extern "C" VkShaderModule
_cogl_shader_vulkan_get_shader_module (CoglShaderVulkan *shader)
{
  CoglContextVulkan *vk_ctx = (CoglContextVulkan *) shader->context->winsys;
  void *spirv;
  uint32_t size;

  if (shader->vk_module)
    return shader->vk_module;

  spirv = _cogl_shader_vulkan_stage_to_spirv (shader,
                                              shader->type,
                                              &size);
  if (!spirv)
    return VK_NULL_HANDLE;

  VkShaderModuleCreateInfo info;
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = size;
  info.pCode = (uint32_t *)spirv;
  vkCreateShaderModule (vk_ctx->device,
                        &info,
                        NULL,
                        &shader->vk_module);

  return shader->vk_module;
}
