/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 *   Neil Roberts <neil@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl-context-private.h"
#include "cogl-util-gl-private.h"
#include "cogl-glsl-shader-private.h"
#include "cogl-glsl-shader-boilerplate.h"

#include <string.h>

#include <glib.h>

static CoglBool
add_layer_vertex_boilerplate_cb (CoglPipelineLayer *layer,
                                 void *user_data)
{
  GString *layer_declarations = user_data;
  int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
  g_string_append_printf (layer_declarations,
                          "attribute vec4 cogl_tex_coord%d_in;\n"
                          "#define cogl_texture_matrix%i cogl_texture_matrix[%i]\n"
                          "#define cogl_tex_coord%i_out _cogl_tex_coord[%i]\n",
                          layer->index,
                          layer->index,
                          unit_index,
                          layer->index,
                          unit_index);
  return TRUE;
}

static CoglBool
add_layer_fragment_boilerplate_cb (CoglPipelineLayer *layer,
                                   void *user_data)
{
  GString *layer_declarations = user_data;
  g_string_append_printf (layer_declarations,
                          "#define cogl_tex_coord%i_in _cogl_tex_coord[%i]\n",
                          layer->index,
                          _cogl_pipeline_layer_get_unit_index (layer));
  return TRUE;
}

GString *
_cogl_glsl_shader_get_source_with_boilerplate (CoglContext *ctx,
                                               CoglGlslShaderType shader_type,
                                               CoglPipeline *pipeline,
                                               int count_in,
                                               const char **strings_in,
                                               const int *lengths_in)
{
  const char *vertex_boilerplate;
  const char *fragment_boilerplate;

  const char **strings = g_alloca (sizeof (char *) * (count_in + 4));
  GLint *lengths = g_alloca (sizeof (GLint) * (count_in + 4));
  char *version_string;
  int count = 0;

  int i, n_layers;
  GString *result = g_string_new (NULL);

  vertex_boilerplate = _COGL_VERTEX_SHADER_BOILERPLATE;
  fragment_boilerplate = _COGL_FRAGMENT_SHADER_BOILERPLATE;

  version_string = g_strdup_printf ("#version %i\n\n",
                                    ctx->glsl_version_to_use);
  strings[count] = version_string;
  lengths[count++] = -1;

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_GL_EMBEDDED) &&
      cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_3D))
    {
      static const char texture_3d_extension[] =
        "#extension GL_OES_texture_3D : enable\n";
      strings[count] = texture_3d_extension;
      lengths[count++] = sizeof (texture_3d_extension) - 1;
    }

  if (shader_type == COGL_GLSL_SHADER_TYPE_VERTEX)
    {
      strings[count] = vertex_boilerplate;
      lengths[count++] = strlen (vertex_boilerplate);
    }
  else if (shader_type == COGL_GLSL_SHADER_TYPE_FRAGMENT)
    {
      strings[count] = fragment_boilerplate;
      lengths[count++] = strlen (fragment_boilerplate);
    }

  n_layers = cogl_pipeline_get_n_layers (pipeline);
  if (n_layers)
    {
      GString *layer_declarations = ctx->codegen_boilerplate_buffer;
      g_string_set_size (layer_declarations, 0);

      g_string_append_printf (layer_declarations,
                              "varying vec4 _cogl_tex_coord[%d];\n",
                              n_layers);

      if (shader_type == COGL_GLSL_SHADER_TYPE_VERTEX)
        {
          g_string_append_printf (layer_declarations,
                                  "uniform mat4 cogl_texture_matrix[%d];\n",
                                  n_layers);

          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_vertex_boilerplate_cb,
                                                 layer_declarations);
        }
      else if (shader_type == COGL_GLSL_SHADER_TYPE_FRAGMENT)
        {
          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_fragment_boilerplate_cb,
                                                 layer_declarations);
        }

      strings[count] = layer_declarations->str;
      lengths[count++] = -1; /* null terminated */
    }

  memcpy (strings + count, strings_in, sizeof (char *) * count_in);
  if (lengths_in)
    memcpy (lengths + count, lengths_in, sizeof (GLint) * count_in);
  else
    {
      for (i = 0; i < count_in; i++)
        lengths[count + i] = -1; /* null terminated */
    }
  count += count_in;

  /* Build up the resulting shader */
  for (i = 0; i < count; i++)
    if (lengths[i] != -1)
      g_string_append_len (result, strings[i], lengths[i]);
    else
      g_string_append (result, strings[i]);

  g_free (version_string);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SHOW_SOURCE)))
    g_message ("%s shader:\n%s",
               shader_type == COGL_GLSL_SHADER_TYPE_VERTEX ?
               "vertex" : "fragment",
               result->str);

  return result;
}

/**/

struct _CoglVulkanShaderBuilder
{
  GString *attributes;
  GString *uniforms;
};

static CoglBool
add_layer_vulkan_vertex_boilerplate_cb (CoglPipelineLayer *layer,
                                        void *user_data)
{
  struct _CoglVulkanShaderBuilder *builder = user_data;
  int unit_index = _cogl_pipeline_layer_get_unit_index (layer);
  g_string_append_printf (builder->attributes,
                          "in vec4 _cogl_tex_coord%d_in;\n"
                          "#define cogl_tex_coord%d_in _cogl_tex_coord%d_in\n"
                          "#define cogl_texture_matrix%i cogl_texture_matrix[%i]\n"
                          "#define cogl_tex_coord%i_out _cogl_tex_coord[%i]\n",
                          unit_index,
                          layer->index,
                          unit_index,
                          layer->index,
                          unit_index,
                          layer->index,
                          unit_index);
  return TRUE;
}

static CoglBool
add_layer_vulkan_fragment_boilerplate_cb (CoglPipelineLayer *layer,
                                          void *user_data)
{
  struct _CoglVulkanShaderBuilder *builder = user_data;
  g_string_append_printf (builder->attributes,
                          "#define cogl_tex_coord%i_in _cogl_tex_coord[%i]\n",
                          layer->index,
                          _cogl_pipeline_layer_get_unit_index (layer));
  return TRUE;
}

GString *
_cogl_glsl_vulkan_shader_get_source_with_boilerplate (CoglContext *ctx,
                                                      CoglGlslShaderType shader_type,
                                                      CoglPipeline *pipeline,
                                                      GString *block,
                                                      GString *global,
                                                      GString *source)
{
  int i, n_layers = cogl_pipeline_get_n_layers (pipeline);
  struct _CoglVulkanShaderBuilder builder = {
    g_string_new (NULL),
    g_string_new (NULL),
  };

  g_string_append_printf (builder.uniforms, "#version %i core\n\n",
                          ctx->glsl_version_to_use);

  g_string_append (builder.uniforms, _COGL_VULKAN_SHADER_BOILERPLATE_BEGIN);

  /* Build with standard 140 uniform block. */
  g_string_append (builder.uniforms, _COGL_VULKAN_SHADER_UNIFORM_BEGIN);
  g_string_append_len (builder.uniforms, block->str, block->len);

  /* Then add the layers (some uniforms, some attributes). */
  n_layers = cogl_pipeline_get_n_layers (pipeline);
  if (n_layers)
    {
      g_string_append_printf (builder.uniforms,
                              "uniform mat4 cogl_texture_matrix[%d];\n",
                              n_layers);

      if (shader_type == COGL_GLSL_SHADER_TYPE_VERTEX)
        {
          g_string_append_printf (builder.attributes,
                                  "out vec4 _cogl_tex_coord[%d];\n",
                                  n_layers);

          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_vulkan_vertex_boilerplate_cb,
                                                 &builder);
        }
      else if (shader_type == COGL_GLSL_SHADER_TYPE_FRAGMENT)
        {
          g_string_append_printf (builder.attributes,
                                  "in vec4 _cogl_tex_coord[%d];\n",
                                  n_layers);
          _cogl_pipeline_foreach_layer_internal (pipeline,
                                                 add_layer_vulkan_fragment_boilerplate_cb,
                                                 &builder);
        }
    }

  /* End the uniform block. */
  g_string_append (builder.uniforms, _COGL_VULKAN_SHADER_UNIFORM_END);

  if (shader_type == COGL_GLSL_SHADER_TYPE_VERTEX)
    g_string_append (builder.attributes,
                     _COGL_VERTEX_VULKAN_SHADER_BOILERPLATE);
  else
    g_string_append (builder.attributes,
                     _COGL_FRAGMENT_VULKAN_SHADER_BOILERPLATE);

  g_string_append_len (builder.uniforms,
                       builder.attributes->str,
                       builder.attributes->len);
  g_string_free (builder.attributes, TRUE);

  g_string_append_len (builder.uniforms, global->str, global->len);
  g_string_append_len (builder.uniforms, source->str, source->len);

  if (G_UNLIKELY (COGL_DEBUG_ENABLED (COGL_DEBUG_SHOW_SOURCE)))
    COGL_NOTE (SHOW_SOURCE, "%s shader:\n%s",
               shader_type == COGL_GLSL_SHADER_TYPE_VERTEX ?
               "vertex" : "fragment",
               builder.uniforms->str);

  return builder.uniforms;
}
