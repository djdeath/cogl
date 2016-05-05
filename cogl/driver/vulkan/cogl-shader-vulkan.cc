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
#include "glslang/glslang/MachineIndependent/localintermediate.h"

#include "GlslangToSpv.h"
#include "disassemble.h"

#include <sstream>
#include <vector>

struct _CoglShaderVulkan
{
  CoglContext *context;

  glslang::TProgram *program;

  GHashTable *inputs[COGL_GLSL_SHADER_TYPE_FRAGMENT + 1];
  GHashTable *outputs[COGL_GLSL_SHADER_TYPE_FRAGMENT + 1];

  GHashTable *uniforms[COGL_GLSL_SHADER_TYPE_FRAGMENT + 1];

  GHashTable *bindings[COGL_GLSL_SHADER_TYPE_FRAGMENT + 1];

  int block_size;
};

static CoglShaderVulkanAttribute *
_cogl_shader_vulkan_attribute_new (const char *name)
{
  CoglShaderVulkanAttribute *attribute =
    g_slice_new0 (CoglShaderVulkanAttribute);

  attribute->name = g_strdup (name);

  return attribute;
}

static void
_cogl_shader_vulkan_attribute_free (CoglShaderVulkanAttribute *attribute)
{
  if (attribute->name)
    g_free (attribute->name);
  g_slice_free (CoglShaderVulkanAttribute, attribute);
}

static CoglShaderVulkanUniform *
_cogl_shader_vulkan_uniform_new (const char *name, int offset)
{
  CoglShaderVulkanUniform *uniform =
    g_slice_new0 (CoglShaderVulkanUniform);

  uniform->name = g_strdup (name);
  uniform->offset = offset;

  return uniform;
}

static void
_cogl_shader_vulkan_uniform_free (CoglShaderVulkanUniform *uniform)
{
  if (uniform->name)
    g_free (uniform->name);
  g_slice_free (CoglShaderVulkanUniform, uniform);
}

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
_cogl_shader_vulkan_free (CoglShaderVulkan *shader)
{
  CoglContextVulkan *vk_ctx = (CoglContextVulkan *) shader->context->winsys;

  for (int stage = COGL_GLSL_SHADER_TYPE_VERTEX;
       stage <= COGL_GLSL_SHADER_TYPE_FRAGMENT;
       stage++) {
    g_hash_table_unref (shader->inputs[stage]);
    g_hash_table_unref (shader->outputs[stage]);
    g_hash_table_unref (shader->uniforms[stage]);
    // g_hash_table_unref (shader->bindings[stage]);
  }

  delete shader->program;
  g_slice_free (CoglShaderVulkan, shader);
}

extern "C" CoglShaderVulkan *
_cogl_shader_vulkan_new (CoglContext *context)
{
  CoglShaderVulkan *shader = g_slice_new0 (CoglShaderVulkan);

  glslang::InitializeProcess ();

  shader->context = context;
  shader->program = new glslang::TProgram();

  for (int stage = COGL_GLSL_SHADER_TYPE_VERTEX;
       stage <= COGL_GLSL_SHADER_TYPE_FRAGMENT;
       stage++) {
    shader->inputs[stage] = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   NULL,
                                                   (GDestroyNotify) _cogl_shader_vulkan_attribute_free);
    shader->outputs[stage] = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    NULL,
                                                    (GDestroyNotify) _cogl_shader_vulkan_attribute_free);
    shader->uniforms[stage] = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     NULL,
                                                     (GDestroyNotify) _cogl_shader_vulkan_uniform_free);
    // shader->bindings[stage] = g_hash_table_new_full (g_str_hash,
    //                                                  g_str_equal,
    //                                                  NULL,
    //                                                  (GDestroyNotify) _cogl_shader_vulkan_binding_free);
  }

  return shader;
}

extern "C" void
_cogl_shader_vulkan_set_source (CoglShaderVulkan *shader,
                                CoglGlslShaderType type,
                                const char *string)
{
  glslang::TShader *gl_shader =
    new glslang::TShader (_cogl_glsl_shader_type_to_es_language (type));

  gl_shader->setStrings(&string, 1);
  bool success = gl_shader->parse(&kDefaultTBuiltInResource,
                                  420,
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

static void
_cogl_shader_vulkan_add_vertex_input (CoglShaderVulkan *shader,
                                      CoglGlslShaderType stage,
                                      glslang::TIntermSymbol* symbol)
{
  CoglShaderVulkanAttribute *attribute =
    _cogl_shader_vulkan_attribute_new (symbol->getName().c_str());

  if (stage > COGL_GLSL_SHADER_TYPE_VERTEX) {
    CoglShaderVulkanAttribute *previous = (CoglShaderVulkanAttribute *)
      g_hash_table_lookup (shader->outputs[stage - 1], attribute->name);
    if (previous)
      symbol->getQualifier().layoutLocation = previous->location;
  } else {
    symbol->getQualifier().layoutLocation =
      g_hash_table_size (shader->inputs[stage]);
  }

  attribute->location = symbol->getQualifier().layoutLocation;

  g_hash_table_insert (shader->inputs[stage], attribute->name, attribute);
}

static void
_cogl_shader_vulkan_add_vertex_output (CoglShaderVulkan *shader,
                                       CoglGlslShaderType stage,
                                       glslang::TIntermSymbol* symbol)
{
  CoglShaderVulkanAttribute *attribute =
    _cogl_shader_vulkan_attribute_new (symbol->getName().c_str());

  attribute->location =
    symbol->getQualifier().layoutLocation =
    g_hash_table_size (shader->outputs[stage]);

  g_hash_table_insert (shader->outputs[stage], attribute->name, attribute);
}

static void
_cogl_shader_vulkan_add_sampler (CoglShaderVulkan *shader,
                                 CoglGlslShaderType stage,
                                 glslang::TIntermSymbol* symbol)
{
  // VK_TODO();
}

static void
_cogl_shader_vulkan_add_uniform (CoglShaderVulkan *shader,
                                 CoglGlslShaderType stage,
                                 const char *name,
                                 int offset)
{
  CoglShaderVulkanUniform *uniform =
    _cogl_shader_vulkan_uniform_new (name, offset);

  g_hash_table_insert (shader->uniforms[stage], uniform->name, uniform);
}

// Lookup or calculate the offset of a block member, using the recursively
// defined block offset rules.
static int getOffset(glslang::TIntermediate* intermediate,
                     const glslang::TType& type,
                     int index)
{
  const glslang::TTypeList& memberList = *type.getStruct();

  // Don't calculate offset if one is present, it could be user supplied and
  // different than what would be calculated. That is, this is faster, but
  // not just an optimization.
  if (memberList[index].type->getQualifier().hasOffset())
    return memberList[index].type->getQualifier().layoutOffset;

  int memberSize;
  int dummyStride;
  int offset = 0;
  for (int m = 0; m <= index; ++m) {
    // modify just the children's view of matrix layout, if there is one for
    // this member
    glslang::TLayoutMatrix subMatrixLayout =
      memberList[m].type->getQualifier().layoutMatrix;
    int memberAlignment =
      intermediate->getBaseAlignment(*memberList[m].type, memberSize,
                                     dummyStride,
                                     type.getQualifier().layoutPacking == glslang::ElpStd140,
                                     subMatrixLayout != glslang::ElmNone ? subMatrixLayout == glslang::ElmRowMajor : type.getQualifier().layoutMatrix == glslang::ElmRowMajor);
    glslang::RoundToPow2(offset, memberAlignment);
    if (m < index)
      offset += memberSize;
  }

  return offset;
}

static void
_cogl_shader_vulkan_add_block (CoglShaderVulkan *shader,
                               CoglGlslShaderType stage,
                               glslang::TIntermSymbol* symbol)
{
  const glslang::TType& block_type = symbol->getType();
  const glslang::TTypeList member_list = *block_type.getStruct();
  glslang::TIntermediate* intermediate =
    shader->program->getIntermediate (_cogl_glsl_shader_type_to_es_language (stage));

  int member_offset = 0;
  int member_size = 0;
  for (int i = 0; i < member_list.size(); i++) {
    const glslang::TType& member_type = *member_list[i].type;
    int dummy_stride;

    member_offset = getOffset (intermediate, block_type, i);
    intermediate->getBaseAlignment (member_type,
                                    member_size,
                                    dummy_stride,
                                    block_type.getQualifier().layoutPacking == glslang::ElpStd140,
                                    block_type.getQualifier().layoutMatrix == glslang::ElmRowMajor);

    _cogl_shader_vulkan_add_uniform (shader,
                                     stage,
                                     member_type.getFieldName().c_str(),
                                     member_offset);
  }

  shader->block_size = member_offset + member_size;
}

typedef std::map<int, glslang::TIntermSymbol*> SymbolMap;

class CoglTraverser : public glslang::TIntermTraverser {
public:
  CoglTraverser(const SymbolMap& map) : map_(map) {}

private:
  void visitSymbol(glslang::TIntermSymbol* base) override {
    SymbolMap::const_iterator it = map_.find(base->getId());

    if (it != map_.end() && it->second != base) {
      g_message ("replacing instance of %s/%i/%p layout=%i/%i binding=%i",
                 base->getName().c_str(), base->getId(), base,
                 base->getQualifier().layoutLocation,
                 it->second->getQualifier().layoutLocation,
                 base->getQualifier().layoutBinding);
      base->getQualifier().layoutLocation =
        it->second->getQualifier().layoutLocation;
    } else {
      g_message ("Ignoring %s/%i/%p layout=%i binding=%i",
                 base->getName().c_str(), base->getId(), base,
                 base->getQualifier().layoutLocation,
                 base->getQualifier().layoutBinding);
    }
  }

  SymbolMap map_;
};

extern "C" CoglBool
_cogl_shader_vulkan_link (CoglShaderVulkan *shader)
{
  if (!shader->program->link (static_cast<EShMessages>(EShMsgDefault))) {
    g_warning ("Cannot link program");
    return false;
  }

  for (int stage = COGL_GLSL_SHADER_TYPE_VERTEX;
       stage <= COGL_GLSL_SHADER_TYPE_FRAGMENT;
       stage++) {
    g_message ("=========== stage %i ============", stage);

    glslang::TIntermediate* intermediate =
      shader->program->getIntermediate(_cogl_glsl_shader_type_to_es_language ((CoglGlslShaderType) stage));
    const glslang::TIntermSequence& globals =
      intermediate->getTreeRoot()->getAsAggregate()->getSequence();
    glslang::TIntermAggregate* linker_objects = nullptr;
    for (unsigned int f = 0; f < globals.size(); ++f) {
      glslang::TIntermAggregate* candidate = globals[f]->getAsAggregate();
      if (candidate && candidate->getOp() == glslang::EOpLinkerObjects) {
        linker_objects = candidate;
        break;
      }
    }

    if (!linker_objects) {
      g_warning ("Cannot find linker objects");
      return FALSE;
    }

    const glslang::TIntermSequence& global_vars = linker_objects->getSequence();
    SymbolMap updated_symbols;
    for (unsigned int f = 0; f < global_vars.size(); f++) {
      glslang::TIntermSymbol* symbol;
      if ((symbol = global_vars[f]->getAsSymbolNode())) {
        updated_symbols.insert(std::pair<int, glslang::TIntermSymbol*>(symbol->getId(), symbol));

        /* Only replace the first block (which we know is ours) */
        if (symbol->isStruct() && symbol->getQualifier().layoutBinding == 0) {
          _cogl_shader_vulkan_add_block (shader,
                                         (CoglGlslShaderType) stage,
                                         symbol);
        } else if (symbol->getQualifier().storage == glslang::EvqIn ||
                   symbol->getQualifier().storage == glslang::EvqInOut ||
                   symbol->getQualifier().storage == glslang::EvqVaryingIn) {
          _cogl_shader_vulkan_add_vertex_input (shader,
                                                (CoglGlslShaderType) stage,
                                                symbol);
        } else if (symbol->getQualifier().storage == glslang::EvqOut ||
                   symbol->getQualifier().storage == glslang::EvqVaryingOut) {
          _cogl_shader_vulkan_add_vertex_output (shader,
                                                 (CoglGlslShaderType) stage,
                                                 symbol);
        } else {
          g_warning ("Unknown global symbol type : %s",
                     symbol->getName().c_str());
        }
      }
    }

    /* Visit the AST to replace all occurences of nodes we might have
       changed. */
    CoglTraverser traverser(updated_symbols);
    intermediate->getTreeRoot()->traverse(&traverser);
  }

  return true;
}

extern "C" CoglShaderVulkanUniform *
_cogl_shader_vulkan_get_uniform (CoglShaderVulkan *shader,
                                 CoglGlslShaderType stage,
                                 const char *name)
{
  return (CoglShaderVulkanUniform *)
    g_hash_table_lookup (shader->uniforms[stage], name);
}

extern "C" int
_cogl_shader_vulkan_get_uniform_block_size (CoglShaderVulkan *shader,
                                            CoglGlslShaderType stage,
                                            int index)
{
  return shader->block_size;
}

extern "C" int
_cogl_shader_vulkan_get_uniform_index (CoglShaderVulkan *shader,
                                       CoglGlslShaderType stage,
                                       const char *name)
{
  return -1;
}

extern "C" int
_cogl_shader_vulkan_get_uniform_buffer_offset (CoglShaderVulkan *shader,
                                               CoglGlslShaderType stage,
                                               int index)
{
  return 0;
}

extern "C" int
_cogl_shader_vulkan_get_input_attribute_location (CoglShaderVulkan *shader,
                                                  CoglGlslShaderType stage,
                                                  const char *name)
{
  CoglShaderVulkanAttribute *attribute =
    (CoglShaderVulkanAttribute *) g_hash_table_lookup (shader->inputs[stage],
                                                       name);

  if (attribute)
    return attribute->location;

  return -1;
}

extern "C" void *
_cogl_shader_vulkan_stage_to_spirv (CoglShaderVulkan *shader,
                                    CoglGlslShaderType stage,
                                    uint32_t *size)
{
  const glslang::TIntermediate *intermediate =
    shader->program->getIntermediate(_cogl_glsl_shader_type_to_es_language (stage));

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
_cogl_shader_vulkan_get_shader_module (CoglShaderVulkan *shader,
                                       CoglGlslShaderType stage)
{
  CoglContextVulkan *vk_ctx = (CoglContextVulkan *) shader->context->winsys;
  void *spirv;
  uint32_t size;
  VkShaderModule module;

  spirv = _cogl_shader_vulkan_stage_to_spirv (shader,
                                              stage,
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
                        &module);

  return module;
}
