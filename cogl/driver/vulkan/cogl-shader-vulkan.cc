#include "cogl-shader-vulkan-private.h"

#include "glslang/glslang/Public/ShaderLang.h"

class DummyIncluder : public glslang::TShader::Includer {
public:
  DummyIncluder() {}

  std::pair<std::string, std::string> include(const char* filename) const final {
    return std::pair<std::string, std::string>("", "");
  }
};

static EShLanguage _shader_type_es_language (CoglShaderVulkanType type)
{
  switch (type)
    {
    case COGL_SHADER_VULKAN_TYPE_VERTEX:
      return EShLangVertex;
    case COGL_SHADER_VULKAN_TYPE_FRAGMENT:
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
  }};


extern "C" CoglShaderDescVulkan *
_cogl_shader_vulkan_create (CoglShaderVulkanType type, const char *string)
{
  glslang::TShader shader (_shader_type_es_language (type));

  std::string preprocessed_shader;
  DummyIncluder includer;
  const bool success = shader.preprocess (&kDefaultTBuiltInResource,
                                          130, ECoreProfile, true, false,
                                          static_cast<EShMessages>(EShMsgOnlyPreprocessor |
                                                                   EShMsgSpvRules |
                                                                   EShMsgVulkanRules),
                                          &preprocessed_shader,
                                          includer);


}
