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

#include "cogl-util.h"
#include "cogl-context-private.h"
#include "cogl-util-gl-private.h"
#include "cogl-pipeline-private.h"
#include "cogl-pipeline-vulkan-private.h"
#include "cogl-offscreen.h"

#include "cogl-buffer-vulkan-private.h"
#include "cogl-context-private.h"
#include "cogl-driver-vulkan-private.h"
#include "cogl-object-private.h"
#include "cogl-program-private.h"
#include "cogl-pipeline-fragend-vulkan-private.h"
#include "cogl-pipeline-vertend-vulkan-private.h"
#include "cogl-pipeline-cache.h"
#include "cogl-pipeline-state-private.h"
#include "cogl-attribute-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-pipeline-progend-vulkan-private.h"
#include "cogl-shader-vulkan-private.h"
#include "cogl-util-vulkan-private.h"

/* These are used to generalise updating some uniforms that are
   required when building for drivers missing some fixed function
   state that we use */

typedef void (* UpdateUniformFunc) (CoglPipeline *pipeline,
                                    CoglShaderVulkanUniform *location,
                                    void *getter_func);

static void update_float_uniform (CoglPipeline *pipeline,
                                  CoglShaderVulkanUniform *location,
                                  void *getter_func);

typedef struct _CoglUniformBuffer CoglUniformBuffer;

typedef struct
{
  const char *uniform_name;
  void *getter_func;
  UpdateUniformFunc update_func;
  CoglPipelineState change;

  /* This builtin is only necessary if the following private feature
   * is not implemented in the driver */
  CoglPrivateFeature feature_replacement;
} BuiltinUniformData;

static BuiltinUniformData builtin_uniforms[] =
  {
    { "cogl_point_size_in",
      cogl_pipeline_get_point_size, update_float_uniform,
      COGL_PIPELINE_STATE_POINT_SIZE,
      COGL_PRIVATE_FEATURE_BUILTIN_POINT_SIZE_UNIFORM },
    { "_cogl_alpha_test_ref",
      cogl_pipeline_get_alpha_test_reference, update_float_uniform,
      COGL_PIPELINE_STATE_ALPHA_FUNC_REFERENCE,
      COGL_PRIVATE_FEATURE_ALPHA_TEST }
  };

const CoglPipelineProgend _cogl_pipeline_vulkan_progend;

typedef struct _UnitState
{
  unsigned int dirty_combine_constant:1;
  unsigned int dirty_texture_matrix:1;

  CoglShaderVulkanUniform *combine_constant_uniform;

  CoglShaderVulkanUniform *texture_matrix_uniform;
} UnitState;

typedef struct
{
  unsigned int ref_count;

  /* TODO: Maybe factorize the uniform buffer out of the program state. It
     could be per pipeline, avoiding a lot of flushing code here. */
  CoglBuffer *uniform_buffer;
  void *uniform_data; /* Just a pointer to uniform_buffer memory */

  VkPipelineLayout pipeline_layout;

  VkDescriptorSetLayout descriptor_set_layout;
  VkDescriptorPool descriptor_pool;
  VkDescriptorSet descriptor_set;

  CoglShaderVulkan *shader;

  VkPipelineShaderStageCreateInfo stage_info[2];

  unsigned long dirty_builtin_uniforms;
  CoglShaderVulkanUniform *builtin_uniform_locations[G_N_ELEMENTS (builtin_uniforms)];

  CoglShaderVulkanUniform *modelview_uniform;
  CoglShaderVulkanUniform *projection_uniform;
  CoglShaderVulkanUniform *mvp_uniform;

  CoglMatrixEntryCache projection_cache;
  CoglMatrixEntryCache modelview_cache;

  /* We need to track the last pipeline that the program was used with
   * so know if we need to update all of the uniforms */
  CoglPipeline *last_used_for_pipeline;

  /* Array of GL uniform locations indexed by Cogl's uniform
     location. We are careful only to allocated this array if a custom
     uniform is actually set */
  GArray *uniform_locations;

  /* The 'flip' uniform is used to flip the geometry upside-down when
     the framebuffer requires it only when there are vertex
     snippets. Otherwise this is acheived using the projection
     matrix */
  CoglShaderVulkanUniform *flip_uniform;
  int flushed_flip_state;

  UnitState *unit_state;

  CoglPipelineCacheEntry *cache_entry;
} CoglPipelineProgramState;

typedef struct _CoglUniformBuffer CoglUniformBuffer;
struct _CoglUniformBuffer
{
  CoglBuffer _parent;
};

static void _cogl_uniform_buffer_free (CoglUniformBuffer *uniforms);

COGL_BUFFER_DEFINE (UniformBuffer, uniform_buffer);
COGL_GTYPE_DEFINE_CLASS (UniformBuffer, uniform_buffer);

static CoglUniformBuffer *
_cogl_uniform_buffer_new (CoglContext *context, size_t bytes)
{
  CoglUniformBuffer *uniforms = g_slice_new (CoglUniformBuffer);

  /* parent's constructor */
  _cogl_buffer_initialize (COGL_BUFFER (uniforms),
                           context,
                           bytes,
                           COGL_BUFFER_BIND_TARGET_UNIFORM_BUFFER,
                           COGL_BUFFER_USAGE_HINT_UNIFORM_BUFFER,
                           COGL_BUFFER_UPDATE_HINT_STATIC);

  return _cogl_uniform_buffer_object_new (uniforms);
}

static void
_cogl_uniform_buffer_free (CoglUniformBuffer *uniforms)
{
  /* parent's destructor */
  _cogl_buffer_fini (COGL_BUFFER (uniforms));

  g_slice_free (CoglUniformBuffer, uniforms);
}

static CoglUserDataKey program_state_key;

static CoglPipelineProgramState *
get_program_state (CoglPipeline *pipeline)
{
  return cogl_object_get_user_data (COGL_OBJECT (pipeline), &program_state_key);
}

#define UNIFORM_LOCATION_UNKNOWN -2

static void
clear_flushed_matrix_stacks (CoglPipelineProgramState *program_state)
{
  _cogl_matrix_entry_cache_destroy (&program_state->projection_cache);
  _cogl_matrix_entry_cache_init (&program_state->projection_cache);
  _cogl_matrix_entry_cache_destroy (&program_state->modelview_cache);
  _cogl_matrix_entry_cache_init (&program_state->modelview_cache);
}

static CoglPipelineProgramState *
program_state_new (int n_layers,
                   CoglPipelineCacheEntry *cache_entry)
{
  CoglPipelineProgramState *program_state;

  program_state = g_slice_new0 (CoglPipelineProgramState);
  program_state->ref_count = 1;
  program_state->unit_state = g_new (UnitState, n_layers);
  program_state->uniform_locations = NULL;
  program_state->cache_entry = cache_entry;
  _cogl_matrix_entry_cache_init (&program_state->modelview_cache);
  _cogl_matrix_entry_cache_init (&program_state->projection_cache);

  return program_state;
}

static void
destroy_program_state (void *user_data,
                       void *instance)
{
  CoglPipelineProgramState *program_state = user_data;
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);
  CoglContextVulkan *vk_ctx = ctx->winsys;

  /* If the program state was last used for this pipeline then clear
     it so that if same address gets used again for a new pipeline
     then we won't think it's the same pipeline and avoid updating the
     uniforms */
  if (program_state->last_used_for_pipeline == instance)
    program_state->last_used_for_pipeline = NULL;

  if (program_state->cache_entry &&
      program_state->cache_entry->pipeline != instance)
    program_state->cache_entry->usage_count--;

  if (--program_state->ref_count == 0)
    {
      _cogl_matrix_entry_cache_destroy (&program_state->projection_cache);
      _cogl_matrix_entry_cache_destroy (&program_state->modelview_cache);

      if (program_state->uniform_buffer)
        {
          cogl_buffer_unmap (COGL_BUFFER (program_state->uniform_buffer));
          cogl_object_unref (COGL_OBJECT (program_state->uniform_buffer));
        }

      if (program_state->descriptor_set != VK_NULL_HANDLE)
        vkFreeDescriptorSets (vk_ctx->device,
                              program_state->descriptor_pool,
                              1, &program_state->descriptor_set);

      if (program_state->descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool (vk_ctx->device,
                                 program_state->descriptor_pool,
                                 NULL);

      if (program_state->pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout (vk_ctx->device,
                                 program_state->pipeline_layout, NULL);

      g_free (program_state->unit_state);

      if (program_state->uniform_locations)
        g_array_free (program_state->uniform_locations, TRUE);

      if (program_state->shader)
        _cogl_shader_vulkan_free (program_state->shader);

      g_slice_free (CoglPipelineProgramState, program_state);
    }
}

static void
set_program_state (CoglPipeline *pipeline,
                  CoglPipelineProgramState *program_state)
{
  if (program_state)
    {
      program_state->ref_count++;

      /* If we're not setting the state on the template pipeline then
       * mark it as a usage of the pipeline cache entry */
      if (program_state->cache_entry &&
          program_state->cache_entry->pipeline != pipeline)
        program_state->cache_entry->usage_count++;
    }

  _cogl_object_set_user_data (COGL_OBJECT (pipeline),
                              &program_state_key,
                              program_state,
                              destroy_program_state);
}

static void
dirty_program_state (CoglPipeline *pipeline)
{
  cogl_object_set_user_data (COGL_OBJECT (pipeline),
                             &program_state_key,
                             NULL,
                             NULL);
}

static CoglShaderVulkanUniform *
get_program_state_uniform_location (CoglPipelineProgramState *program_state,
                                    const char *name)
{
  return _cogl_shader_vulkan_get_uniform (program_state->shader,
                                          COGL_GLSL_SHADER_TYPE_VERTEX,
                                          name);
}

static void
set_program_state_uniform (CoglPipelineProgramState *program_state,
                           CoglShaderVulkanUniform *location,
                           const void *data,
                           size_t size)
{
  g_assert (program_state->shader);
  g_assert (program_state->uniform_data);

  COGL_NOTE (VULKAN, "Uniform offset %s = offset=%i size=%i",
             location->name, location->offset, size);

  memcpy (program_state->uniform_data + location->offset, data, size);
}

#define set_program_state_uniform1i(program_state, location, data)      \
  set_program_state_uniform(program_state, location, &data, sizeof(int))
#define set_program_state_uniform1f(program_state, location, data)      \
  set_program_state_uniform(program_state, location, &data, sizeof(float))
#define set_program_state_uniform4fv(program_state, location, count, data) \
  set_program_state_uniform(program_state, location, data,              \
                            (count) * 4 * sizeof(float))
#define set_program_state_uniform_matrix4fv(program_state, location, count, data) \
  set_program_state_uniform(program_state, location, data,              \
                            (count) * 16 * sizeof(float))

VkDescriptorSet
_cogl_pipeline_progend_get_vulkan_descriptor_set (CoglPipeline *pipeline)
{
  CoglPipelineProgramState *program_state = get_program_state (pipeline);

  return program_state->descriptor_set;
}

VkPipelineLayout
_cogl_pipeline_progend_get_vulkan_pipeline_layout (CoglPipeline *pipeline)
{
  CoglPipelineProgramState *program_state = get_program_state (pipeline);

  return program_state->pipeline_layout;
}

VkPipelineShaderStageCreateInfo *
_cogl_pipeline_progend_get_vulkan_stage_info (CoglPipeline *pipeline)
{
  CoglPipelineProgramState *program_state = get_program_state (pipeline);

  return program_state->stage_info;
}

CoglShaderVulkan *
_cogl_pipeline_progend_get_vulkan_shader (CoglPipeline *pipeline)
{
  CoglPipelineProgramState *program_state = get_program_state (pipeline);

  return program_state->shader;
}

typedef struct
{
  int unit;
  CoglBool update_all;
  CoglPipelineProgramState *program_state;
} UpdateUniformsState;

static CoglBool
get_uniform_cb (CoglPipeline *pipeline,
                int layer_index,
                void *user_data)
{
  UpdateUniformsState *state = user_data;
  CoglPipelineProgramState *program_state = state->program_state;
  UnitState *unit_state = &program_state->unit_state[state->unit];
  CoglShaderVulkanUniform *uniform_location;

  _COGL_GET_CONTEXT (ctx, FALSE);

  /* We can reuse the source buffer to create the uniform name because
     the program has now been linked */
  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "cogl_sampler%i", layer_index);

  uniform_location =
    get_program_state_uniform_location (program_state,
                                        ctx->codegen_source_buffer->str);

  /* We can set the uniform immediately because the samplers are the
     unit index not the texture object number so it will never
     change. Unfortunately GL won't let us use a constant instead of a
     uniform */
  if (uniform_location != NULL)
    set_program_state_uniform1i (program_state, uniform_location, state->unit);

  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "_cogl_layer_constant_%i", layer_index);

  unit_state->combine_constant_uniform =
    get_program_state_uniform_location (program_state,
                                        ctx->codegen_source_buffer->str);

  g_string_set_size (ctx->codegen_source_buffer, 0);
  g_string_append_printf (ctx->codegen_source_buffer,
                          "cogl_texture_matrix[%i]", layer_index);

  unit_state->texture_matrix_uniform =
    get_program_state_uniform_location (program_state,
                                        ctx->codegen_source_buffer->str);

  state->unit++;

  return TRUE;
}

static CoglBool
update_constants_cb (CoglPipeline *pipeline,
                     int layer_index,
                     void *user_data)
{
  UpdateUniformsState *state = user_data;
  CoglPipelineProgramState *program_state = state->program_state;
  UnitState *unit_state = &program_state->unit_state[state->unit++];

  _COGL_GET_CONTEXT (ctx, FALSE);

  if (unit_state->combine_constant_uniform != NULL &&
      (state->update_all || unit_state->dirty_combine_constant))
    {
      float constant[4];
      _cogl_pipeline_get_layer_combine_constant (pipeline,
                                                 layer_index,
                                                 constant);
      set_program_state_uniform4fv (program_state,
                                    unit_state->combine_constant_uniform,
                                    1, constant);
      unit_state->dirty_combine_constant = FALSE;
    }

  if (unit_state->texture_matrix_uniform != NULL &&
      (state->update_all || unit_state->dirty_texture_matrix))
    {
      const CoglMatrix *matrix;
      const float *array;

      matrix = _cogl_pipeline_get_layer_matrix (pipeline, layer_index);
      array = cogl_matrix_get_array (matrix);
      set_program_state_uniform_matrix4fv (program_state,
                                           unit_state->texture_matrix_uniform,
                                           1, array);
      unit_state->dirty_texture_matrix = FALSE;
    }

  return TRUE;
}

static void
update_builtin_uniforms (CoglContext *context,
                         CoglPipeline *pipeline,
                         CoglPipelineProgramState *program_state)
{
  int i;

  if (program_state->dirty_builtin_uniforms == 0)
    return;

  for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
    if (!_cogl_has_private_feature (context,
                                    builtin_uniforms[i].feature_replacement) &&
        (program_state->dirty_builtin_uniforms & (1 << i)) &&
        program_state->builtin_uniform_locations[i] != NULL)
      builtin_uniforms[i].update_func (pipeline,
                                       program_state
                                       ->builtin_uniform_locations[i],
                                       builtin_uniforms[i].getter_func);

  program_state->dirty_builtin_uniforms = 0;
}

typedef struct
{
  CoglPipelineProgramState *program_state;
  unsigned long *uniform_differences;
  int n_differences;
  CoglContext *ctx;
  const CoglBoxedValue *values;
  int value_index;
} FlushUniformsClosure;

static CoglBool
flush_uniform_cb (int uniform_num, void *user_data)
{
  FlushUniformsClosure *data = user_data;

  if (COGL_FLAGS_GET (data->uniform_differences, uniform_num))
    {
      GArray *uniform_locations;
      CoglShaderVulkanUniform *uniform_location;

      if (data->program_state->uniform_locations == NULL)
        data->program_state->uniform_locations =
          g_array_new (FALSE, FALSE, sizeof (CoglShaderVulkanUniform *));

      uniform_locations = data->program_state->uniform_locations;

      if (uniform_locations->len <= uniform_num)
        {
          unsigned int old_len = uniform_locations->len;

          g_array_set_size (uniform_locations, uniform_num + 1);

          while (old_len <= uniform_num)
            {
              g_array_index (uniform_locations,
                             CoglShaderVulkanUniform*,
                             old_len) = NULL;
              old_len++;
            }
        }

      uniform_location = g_array_index (uniform_locations,
                                        CoglShaderVulkanUniform *,
                                        uniform_num);

      if (uniform_location == NULL)
        {
          const char *uniform_name =
            g_ptr_array_index (data->ctx->uniform_names, uniform_num);

          uniform_location =
            get_program_state_uniform_location (data->program_state, uniform_name);
          g_array_index (uniform_locations,
                         CoglShaderVulkanUniform *,
                         uniform_num) = uniform_location;
        }

      if (uniform_location != NULL)
        _cogl_boxed_value_set_uniform (data->ctx,
                                       uniform_location,
                                       data->values + data->value_index);

      data->n_differences--;
      COGL_FLAGS_SET (data->uniform_differences, uniform_num, FALSE);
    }

  data->value_index++;

  return data->n_differences > 0;
}

static void
_cogl_pipeline_progend_vulkan_flush_uniforms (CoglPipeline *pipeline,
                                            CoglPipelineProgramState *
                                                                  program_state,
                                            CoglBool program_changed)
{
  CoglPipelineUniformsState *uniforms_state;
  FlushUniformsClosure data;
  int n_uniform_longs;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if (pipeline->differences & COGL_PIPELINE_STATE_UNIFORMS)
    uniforms_state = &pipeline->big_state->uniforms_state;
  else
    uniforms_state = NULL;

  data.program_state = program_state;
  data.ctx = ctx;

  n_uniform_longs = COGL_FLAGS_N_LONGS_FOR_SIZE (ctx->n_uniform_names);

  data.uniform_differences = g_newa (unsigned long, n_uniform_longs);

  /* Try to find a common ancestor for the values that were already
     flushed on the pipeline that this program state was last used for
     so we can avoid flushing those */

  if (program_changed || program_state->last_used_for_pipeline == NULL)
    {
      if (program_changed)
        {
          /* The program has changed so all of the uniform locations
             are invalid */
          if (program_state->uniform_locations)
            g_array_set_size (program_state->uniform_locations, 0);
        }

      /* We need to flush everything so mark all of the uniforms as
         dirty */
      memset (data.uniform_differences, 0xff,
              n_uniform_longs * sizeof (unsigned long));
      data.n_differences = G_MAXINT;
    }
  else if (program_state->last_used_for_pipeline)
    {
      int i;

      memset (data.uniform_differences, 0,
              n_uniform_longs * sizeof (unsigned long));
      _cogl_pipeline_compare_uniform_differences
        (data.uniform_differences,
         program_state->last_used_for_pipeline,
         pipeline);

      /* We need to be sure to flush any uniforms that have changed
         since the last flush */
      if (uniforms_state)
        _cogl_bitmask_set_flags (&uniforms_state->changed_mask,
                                 data.uniform_differences);

      /* Count the number of differences. This is so we can stop early
         when we've flushed all of them */
      data.n_differences = 0;

      for (i = 0; i < n_uniform_longs; i++)
        data.n_differences +=
          _cogl_util_popcountl (data.uniform_differences[i]);
    }

  while (pipeline && data.n_differences > 0)
    {
      if (pipeline->differences & COGL_PIPELINE_STATE_UNIFORMS)
        {
          const CoglPipelineUniformsState *parent_uniforms_state =
            &pipeline->big_state->uniforms_state;

          data.values = parent_uniforms_state->override_values;
          data.value_index = 0;

          _cogl_bitmask_foreach (&parent_uniforms_state->override_mask,
                                 flush_uniform_cb,
                                 &data);
        }

      pipeline = _cogl_pipeline_get_parent (pipeline);
    }

  if (uniforms_state)
    _cogl_bitmask_clear_all (&uniforms_state->changed_mask);
}

typedef struct
{
  CoglPipelineProgramState *program_state;
  int n_bindings;

  VkShaderStageFlags stage_flags;
  VkDescriptorSetLayoutBinding *bindings;
} CreateDescriptorSetLayout;

static CoglBool
add_layer_to_descriptor_set_layout (CoglPipelineLayer *layer,
                                    void *user_data)
{
  CreateDescriptorSetLayout *data = user_data;
  VkDescriptorSetLayoutBinding *binding = &data->bindings[data->n_bindings];

  binding->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding->descriptorCount = 1;
  binding->stageFlags = data->stage_flags;
  binding->pImmutableSamplers = NULL;

  data->n_bindings++;

  return TRUE;
}

static void
_cogl_pipeline_create_descriptor_set_layout (CoglPipeline *pipeline,
                                             CoglPipelineProgramState *program_state,
                                             CoglContextVulkan *vk_ctx)
{
  VkDescriptorSetLayoutCreateInfo info;
  CreateDescriptorSetLayout data;
  VkResult result;

  data.program_state = program_state;
  data.n_bindings = 1;
  data.stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  data.bindings = g_new0 (VkDescriptorSetLayoutBinding,
                          cogl_pipeline_get_n_layers (pipeline) + 1);
  data.n_bindings = 1;

  /* Uniform buffer for all our uniforms. */
  data.bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  data.bindings[0].descriptorCount = 1;
  data.bindings[0].stageFlags = data.stage_flags;
  data.bindings[0].pImmutableSamplers = NULL;

  /* All other potential samplers for each layer. */
  _cogl_pipeline_foreach_layer_internal (pipeline,
                                         add_layer_to_descriptor_set_layout,
                                         &data);

  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = data.n_bindings;
  info.pBindings = data.bindings;
  result = vkCreateDescriptorSetLayout (vk_ctx->device, &info,
                                        NULL,
                                        &program_state->descriptor_set_layout);
  if (result != VK_SUCCESS)
    g_warning ("Cannot create descriptor set layout (%d): %s",
               result, _cogl_vulkan_error_to_string (result));
}

static CoglBool
_cogl_pipeline_progend_vulkan_start (CoglPipeline *pipeline)
{
  return TRUE;
}

static void
_cogl_pipeline_progend_vulkan_end (CoglPipeline *pipeline,
                                 unsigned long pipelines_difference)
{
   /* TODO: No way to get the context linked to a pipeline?? */
  CoglContextVulkan *vk_ctx = _cogl_context_get_default ()->winsys;

  CoglPipelineProgramState *program_state;
  CoglBool program_changed = FALSE;
  UpdateUniformsState state;
  CoglProgram *user_program;
  CoglPipelineCacheEntry *cache_entry = NULL;
  VkResult result;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  program_state = get_program_state (pipeline);

  if (G_UNLIKELY (cogl_pipeline_get_user_program (pipeline)))
    g_warning ("The Vulkan backend doesn't support legacy user program");

  if (program_state == NULL)
    {
      CoglPipeline *authority;

      /* Get the authority for anything affecting program state. This
         should include both fragment codegen state and vertex codegen
         state */
      authority = _cogl_pipeline_find_equivalent_parent
        (pipeline,
         (_cogl_pipeline_get_state_for_vertex_codegen (ctx) |
          _cogl_pipeline_get_state_for_fragment_codegen (ctx)) &
         ~COGL_PIPELINE_STATE_LAYERS,
         _cogl_pipeline_get_layer_state_for_fragment_codegen (ctx) |
         COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN);

      program_state = get_program_state (authority);

      if (program_state == NULL)
        {
          /* Check if there is already a similar cached pipeline whose
             program state we can share */
          if (G_LIKELY (!(COGL_DEBUG_ENABLED
                          (COGL_DEBUG_DISABLE_PROGRAM_CACHES))))
            {
              cache_entry =
                _cogl_pipeline_cache_get_combined_template (ctx->pipeline_cache,
                                                            authority);

              program_state = get_program_state (cache_entry->pipeline);
            }

          if (program_state)
            program_state->ref_count++;
          else
            program_state
              = program_state_new (cogl_pipeline_get_n_layers (authority),
                                   cache_entry);

          set_program_state (authority, program_state);

          program_state->ref_count--;

          if (cache_entry)
            set_program_state (cache_entry->pipeline, program_state);
        }

      if (authority != pipeline)
        set_program_state (pipeline, program_state);
    }

  if (program_state->shader == NULL)
    {
      GString *source;

      program_state->shader = _cogl_shader_vulkan_new (ctx);

      source = _cogl_pipeline_vertend_vulkan_get_shader (pipeline);
      _cogl_shader_vulkan_set_source (program_state->shader,
                                      COGL_GLSL_SHADER_TYPE_VERTEX,
                                      source->str);

      source = _cogl_pipeline_fragend_vulkan_get_shader (pipeline);
      _cogl_shader_vulkan_set_source (program_state->shader,
                                      COGL_GLSL_SHADER_TYPE_FRAGMENT,
                                      source->str);

      if (!_cogl_shader_vulkan_link (program_state->shader))
        {
          g_warning ("Vertex shader compilation failed");
          _cogl_shader_vulkan_free (program_state->shader);
          program_state->shader = NULL;
        }
    }

  /* Allocate uniform buffers for vertex & fragment shaders. */
  if (program_state->uniform_buffer == NULL)
    {
      int block_size;

      block_size =
        _cogl_shader_vulkan_get_uniform_block_size (program_state->shader,
                                                    COGL_GLSL_SHADER_TYPE_VERTEX,
                                                    0);

      program_state->uniform_buffer =
        COGL_BUFFER (_cogl_uniform_buffer_new (ctx, block_size));
      program_state->uniform_data =
        _cogl_buffer_map (program_state->uniform_buffer,
                          COGL_BUFFER_ACCESS_WRITE,
                          COGL_BUFFER_MAP_HINT_DISCARD,
                          NULL);
    }

  if (program_state->pipeline_layout == VK_NULL_HANDLE)
    {
      _cogl_pipeline_create_descriptor_set_layout (pipeline,
                                                   program_state,
                                                   vk_ctx);

      result = vkCreatePipelineLayout (vk_ctx->device, &(VkPipelineLayoutCreateInfo) {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 1,
          .pSetLayouts = &program_state->descriptor_set_layout,
        },
        NULL,
        &program_state->pipeline_layout);
      if (result != VK_SUCCESS)
        g_warning ("Cannot create pipeline layout (%d): %s",
                   result, _cogl_vulkan_error_to_string (result));

      program_state->stage_info[0] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = _cogl_shader_vulkan_get_shader_module (program_state->shader,
                                                         COGL_GLSL_SHADER_TYPE_VERTEX),
        .pName = "main",
      };
      program_state->stage_info[1] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = _cogl_shader_vulkan_get_shader_module (program_state->shader,
                                                         COGL_GLSL_SHADER_TYPE_FRAGMENT),
        .pName = "main",
      };

      program_changed = TRUE;
    }

  if (program_state->descriptor_set == VK_NULL_HANDLE)
    {
      CoglBuffer *buf = program_state->uniform_buffer;
      CoglBufferVulkan *vk_buf = buf->winsys;
      const VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = (VkDescriptorPoolSize[]) {
          {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1
          },
        }
      };

      result = vkCreateDescriptorPool (vk_ctx->device, &create_info,
                                       NULL, &program_state->descriptor_pool);
      if (result != VK_SUCCESS)
        g_warning ("Cannot create descriptor pool (%d): %s",
                   result, _cogl_vulkan_error_to_string (result));

      result = vkAllocateDescriptorSets (vk_ctx->device, &(VkDescriptorSetAllocateInfo) {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorPool = program_state->descriptor_pool,
          .descriptorSetCount = 1,
          .pSetLayouts = &program_state->descriptor_set_layout,
        },
        &program_state->descriptor_set);
      if (result != VK_SUCCESS)
        g_warning ("Cannot allocate descriptor set (%d): %s",
                   result, _cogl_vulkan_error_to_string (result));

    }

  state.unit = 0;
  state.program_state = program_state;

  if (program_changed)
    {
      cogl_pipeline_foreach_layer (pipeline,
                                   get_uniform_cb,
                                   &state);

      program_state->flip_uniform =
        get_program_state_uniform_location (program_state, "_cogl_flip_vector");
      program_state->flushed_flip_state = -1;
    }

  state.unit = 0;
  state.update_all = (program_changed ||
                      program_state->last_used_for_pipeline != pipeline);

  cogl_pipeline_foreach_layer (pipeline,
                               update_constants_cb,
                               &state);

  if (program_changed)
    {
      int i;

      clear_flushed_matrix_stacks (program_state);

      for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
        if (!_cogl_has_private_feature
            (ctx, builtin_uniforms[i].feature_replacement))
          program_state->builtin_uniform_locations[i] =
            get_program_state_uniform_location (program_state,
                                                builtin_uniforms[i].uniform_name);

      program_state->modelview_uniform =
        get_program_state_uniform_location (program_state, "cogl_modelview_matrix");

      program_state->projection_uniform =
        get_program_state_uniform_location (program_state, "cogl_projection_matrix");

      program_state->mvp_uniform =
        get_program_state_uniform_location (program_state,
                                            "cogl_modelview_projection_matrix");
    }

  if (program_changed ||
      program_state->last_used_for_pipeline != pipeline)
    program_state->dirty_builtin_uniforms = ~(unsigned long) 0;

  update_builtin_uniforms (ctx, pipeline, program_state);

  _cogl_pipeline_progend_vulkan_flush_uniforms (pipeline,
                                                program_state,
                                                program_changed);

  /* We need to track the last pipeline that the program was used with
   * so know if we need to update all of the uniforms */
  program_state->last_used_for_pipeline = pipeline;
}

static void
_cogl_pipeline_progend_vulkan_pre_change_notify (CoglPipeline *pipeline,
                                               CoglPipelineState change,
                                               const CoglColor *new_color)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if ((change & (_cogl_pipeline_get_state_for_vertex_codegen (ctx) |
                 _cogl_pipeline_get_state_for_fragment_codegen (ctx))))
    {
      dirty_program_state (pipeline);
    }
  else
    {
      int i;

      for (i = 0; i < G_N_ELEMENTS (builtin_uniforms); i++)
        if (!_cogl_has_private_feature
            (ctx, builtin_uniforms[i].feature_replacement) &&
            (change & builtin_uniforms[i].change))
          {
            CoglPipelineProgramState *program_state
              = get_program_state (pipeline);
            if (program_state)
              program_state->dirty_builtin_uniforms |= 1 << i;
            return;
          }
    }
}

/* NB: layers are considered immutable once they have any dependants
 * so although multiple pipelines can end up depending on a single
 * static layer, we can guarantee that if a layer is being *changed*
 * then it can only have one pipeline depending on it.
 *
 * XXX: Don't forget this is *pre* change, we can't read the new value
 * yet!
 */
static void
_cogl_pipeline_progend_vulkan_layer_pre_change_notify (
                                                CoglPipeline *owner,
                                                CoglPipelineLayer *layer,
                                                CoglPipelineLayerState change)
{
  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  if ((change & (_cogl_pipeline_get_layer_state_for_fragment_codegen (ctx) |
                 COGL_PIPELINE_LAYER_STATE_AFFECTS_VERTEX_CODEGEN)))
    {
      dirty_program_state (owner);
    }
  else if (change & COGL_PIPELINE_LAYER_STATE_COMBINE_CONSTANT)
    {
      CoglPipelineProgramState *program_state = get_program_state (owner);
      if (program_state)
        {
          int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
          program_state->unit_state[unit_index].dirty_combine_constant = TRUE;
        }
    }
  else if (change & COGL_PIPELINE_LAYER_STATE_USER_MATRIX)
    {
      CoglPipelineProgramState *program_state = get_program_state (owner);
      if (program_state)
        {
          int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
          program_state->unit_state[unit_index].dirty_texture_matrix = TRUE;
        }
    }
}

static void
_cogl_pipeline_progend_vulkan_pre_paint (CoglPipeline *pipeline,
                                         CoglFramebuffer *framebuffer)
{
  CoglBool needs_flip;
  CoglMatrixEntry *projection_entry;
  CoglMatrixEntry *modelview_entry;
  CoglPipelineProgramState *program_state;
  CoglBool modelview_changed;
  CoglBool projection_changed;
  CoglBool need_modelview;
  CoglBool need_projection;
  CoglMatrix modelview, projection;
  CoglContextVulkan *vk_ctx = framebuffer->context->winsys;
  CoglBuffer *uniform_buffer;
  CoglBufferVulkan *vk_uniform_buffer;

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  program_state = get_program_state (pipeline);

  g_assert (program_state->uniform_buffer);
  vk_uniform_buffer = program_state->uniform_buffer->winsys;

  projection_entry = _cogl_framebuffer_get_projection_entry (framebuffer);
  modelview_entry = _cogl_framebuffer_get_modelview_entry (framebuffer);

  needs_flip = TRUE; /* TODO: to rework */

  projection_changed =
    _cogl_matrix_entry_cache_maybe_update (&program_state->projection_cache,
                                           projection_entry,
                                           (needs_flip &&
                                            program_state->flip_uniform ==
                                            NULL));

  modelview_changed =
    _cogl_matrix_entry_cache_maybe_update (&program_state->modelview_cache,
                                           modelview_entry,
                                           /* never flip modelview */
                                           FALSE);

  if (modelview_changed || projection_changed)
    {
      if (program_state->mvp_uniform != NULL)
        need_modelview = need_projection = TRUE;
      else
        {
          need_projection = (program_state->projection_uniform != NULL &&
                             projection_changed);
          need_modelview = (program_state->modelview_uniform != NULL &&
                            modelview_changed);
        }

      if (need_modelview)
        cogl_matrix_entry_get (modelview_entry, &modelview);
      if (need_projection)
        {
          if (needs_flip && program_state->flip_uniform == NULL)
            {
              CoglMatrix tmp_matrix;
              cogl_matrix_entry_get (projection_entry, &tmp_matrix);
              cogl_matrix_multiply (&projection,
                                    &ctx->y_flip_matrix,
                                    &tmp_matrix);
            }
          else
            cogl_matrix_entry_get (projection_entry, &projection);
        }

      if (projection_changed && program_state->projection_uniform != NULL)
        set_program_state_uniform_matrix4fv (program_state,
                                             program_state->projection_uniform,
                                             1, /* count */
                                             cogl_matrix_get_array (&projection));

      if (modelview_changed && program_state->modelview_uniform != NULL)
        set_program_state_uniform_matrix4fv (program_state,
                                             program_state->modelview_uniform,
                                             1, /* count */
                                             cogl_matrix_get_array (&modelview));

      if (program_state->mvp_uniform != NULL)
        {
          /* The journal usually uses an identity matrix for the
             modelview so we can optimise this common case by
             avoiding the matrix multiplication */
          if (cogl_matrix_entry_is_identity (modelview_entry))
            {
              set_program_state_uniform_matrix4fv (program_state,
                                                   program_state->mvp_uniform,
                                                   1, /* count */
                                                   cogl_matrix_get_array (&projection));
            }
          else
            {
              CoglMatrix combined;

              cogl_matrix_multiply (&combined,
                                    &projection,
                                    &modelview);

              set_program_state_uniform_matrix4fv (program_state,
                                                   program_state->mvp_uniform,
                                                   1, /* count */
                                                   cogl_matrix_get_array (&combined));
            }
        }
    }

  if (program_state->flip_uniform != NULL
      && program_state->flushed_flip_state != needs_flip)
    {
      static const float do_flip[4] = { 1.0f, -1.0f, 1.0f, 1.0f };
      static const float dont_flip[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
      set_program_state_uniform4fv (program_state,
                                    program_state->flip_uniform,
                                    1, /* count */
                                    needs_flip ? do_flip : dont_flip);
      program_state->flushed_flip_state = needs_flip;
    }

  vkUpdateDescriptorSets (vk_ctx->device, 1,
                          (VkWriteDescriptorSet []) {
                            {
                              .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstSet = program_state->descriptor_set,
                              .dstBinding = 0,
                              .dstArrayElement = 0,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              .pBufferInfo = &(VkDescriptorBufferInfo) {
                                .buffer = vk_uniform_buffer->buffer,
                                .offset = 0,
                                .range = program_state->uniform_buffer->size,
                              }
                            }
                          },
                          0, NULL);
}

static void
update_float_uniform (CoglPipeline *pipeline,
                      CoglShaderVulkanUniform *location,
                      void *getter_func)
{

  float (* float_getter_func) (CoglPipeline *) = getter_func;
  float value;
  CoglPipelineProgramState *program_state = get_program_state (pipeline);

  _COGL_GET_CONTEXT (ctx, NO_RETVAL);

  value = float_getter_func (pipeline);
  set_program_state_uniform1f (program_state, location, value);
}

const CoglPipelineProgend _cogl_pipeline_vulkan_progend =
  {
    COGL_PIPELINE_VERTEND_VULKAN,
    COGL_PIPELINE_FRAGEND_VULKAN,
    _cogl_pipeline_progend_vulkan_start,
    _cogl_pipeline_progend_vulkan_end,
    _cogl_pipeline_progend_vulkan_pre_change_notify,
    _cogl_pipeline_progend_vulkan_layer_pre_change_notify,
    _cogl_pipeline_progend_vulkan_pre_paint
  };
