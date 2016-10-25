#include <cogl/cogl.h>
#include <cogl-pango/cogl-pango.h>

#include <assert.h>

/* The state for this example... */
typedef struct _Data
{
  CoglFramebuffer *off_fb;
  int off_fb_width;
  int off_fb_height;

  CoglFramebuffer *fb;
  int framebuffer_width;
  int framebuffer_height;

  CoglMatrix view;

  CoglIndices *indices;
  CoglPrimitive *prim;
  CoglPipeline *crate_pipeline;

  CoglPipeline *copy_pipeline;

  GTimer *timer;

  CoglBool swap_ready;
} Data;

/* A cube modelled using 4 vertices for each face.
 *
 * We use an index buffer when drawing the cube later so the GPU will
 * actually read each face as 2 separate triangles.
 */
static CoglVertexP3T2 vertices[] =
{
  /* Front face */
  { /* pos = */ -1.0f, -1.0f,  1.0f, /* tex coords = */ 0.0f, 1.0f},
  { /* pos = */  1.0f, -1.0f,  1.0f, /* tex coords = */ 1.0f, 1.0f},
  { /* pos = */  1.0f,  1.0f,  1.0f, /* tex coords = */ 1.0f, 0.0f},
  { /* pos = */ -1.0f,  1.0f,  1.0f, /* tex coords = */ 0.0f, 0.0f},

  /* Back face */
  { /* pos = */ -1.0f, -1.0f, -1.0f, /* tex coords = */ 1.0f, 0.0f},
  { /* pos = */ -1.0f,  1.0f, -1.0f, /* tex coords = */ 1.0f, 1.0f},
  { /* pos = */  1.0f,  1.0f, -1.0f, /* tex coords = */ 0.0f, 1.0f},
  { /* pos = */  1.0f, -1.0f, -1.0f, /* tex coords = */ 0.0f, 0.0f},

  /* Top face */
  { /* pos = */ -1.0f,  1.0f, -1.0f, /* tex coords = */ 0.0f, 1.0f},
  { /* pos = */ -1.0f,  1.0f,  1.0f, /* tex coords = */ 0.0f, 0.0f},
  { /* pos = */  1.0f,  1.0f,  1.0f, /* tex coords = */ 1.0f, 0.0f},
  { /* pos = */  1.0f,  1.0f, -1.0f, /* tex coords = */ 1.0f, 1.0f},

  /* Bottom face */
  { /* pos = */ -1.0f, -1.0f, -1.0f, /* tex coords = */ 1.0f, 1.0f},
  { /* pos = */  1.0f, -1.0f, -1.0f, /* tex coords = */ 0.0f, 1.0f},
  { /* pos = */  1.0f, -1.0f,  1.0f, /* tex coords = */ 0.0f, 0.0f},
  { /* pos = */ -1.0f, -1.0f,  1.0f, /* tex coords = */ 1.0f, 0.0f},

  /* Right face */
  { /* pos = */ 1.0f, -1.0f, -1.0f, /* tex coords = */ 1.0f, 0.0f},
  { /* pos = */ 1.0f,  1.0f, -1.0f, /* tex coords = */ 1.0f, 1.0f},
  { /* pos = */ 1.0f,  1.0f,  1.0f, /* tex coords = */ 0.0f, 1.0f},
  { /* pos = */ 1.0f, -1.0f,  1.0f, /* tex coords = */ 0.0f, 0.0f},

  /* Left face */
  { /* pos = */ -1.0f, -1.0f, -1.0f, /* tex coords = */ 0.0f, 0.0f},
  { /* pos = */ -1.0f, -1.0f,  1.0f, /* tex coords = */ 1.0f, 0.0f},
  { /* pos = */ -1.0f,  1.0f,  1.0f, /* tex coords = */ 1.0f, 1.0f},
  { /* pos = */ -1.0f,  1.0f, -1.0f, /* tex coords = */ 0.0f, 1.0f}
};

static void
paint_cube (Data *data)
{
  CoglFramebuffer *fb = data->off_fb;
  float rotation= 45;

  cogl_framebuffer_clear4f (fb,
                            COGL_BUFFER_BIT_COLOR|COGL_BUFFER_BIT_DEPTH,
                            0, 0, 0, 1);
  
  cogl_framebuffer_push_matrix (fb);

  cogl_framebuffer_translate (fb,
                              cogl_framebuffer_get_width (fb) / 2,
                              cogl_framebuffer_get_height (fb) / 2,
                              0);

  cogl_framebuffer_scale (fb, 300, 300, 300);

  /* Rotate the cube separately around each axis.
   *
   * Note: Cogl matrix manipulation follows the same rules as for
   * OpenGL. We use column-major matrices and - if you consider the
   * transformations happening to the model - then they are combined
   * in reverse order which is why the rotation is done last, since
   * we want it to be a rotation around the origin, before it is
   * scaled and translated.
   */
  cogl_framebuffer_rotate (fb, rotation, 0, 0, 1);
  cogl_framebuffer_rotate (fb, rotation, 0, 1, 0);
  cogl_framebuffer_rotate (fb, rotation, 1, 0, 0);

  cogl_primitive_draw (data->prim, fb, data->crate_pipeline);

  cogl_framebuffer_pop_matrix (fb);
}

static void
paint_fb (Data *data)
{
  CoglFramebuffer *fb = data->fb;

  cogl_framebuffer_clear4f (fb,
                            COGL_BUFFER_BIT_COLOR|COGL_BUFFER_BIT_DEPTH,
                            0, 0, 0, 1);

  cogl_framebuffer_draw_rectangle (fb, data->copy_pipeline,
				   0, 0,
				   cogl_framebuffer_get_width (fb),
				   cogl_framebuffer_get_height (fb));
}

static void
frame_event_cb (CoglOnscreen *onscreen,
		CoglFrameEvent event,
		CoglFrameInfo *info,
		void *user_data)
{
  Data *data = user_data;

  if (event == COGL_FRAME_EVENT_SYNC)
    data->swap_ready = TRUE;
}

static void
draw_onscreen (CoglContext *ctx, Data *data)
{
  data->swap_ready = TRUE;

  paint_cube (data);
  
  cogl_onscreen_add_frame_callback (COGL_ONSCREEN (data->fb),
				    frame_event_cb,
				    &data,
				    NULL);
  while (1)
    {
      CoglPollFD *poll_fds;
      int n_poll_fds;
      int64_t timeout;
      
      if (data->swap_ready)
	{
	  paint_fb (data);
	  cogl_onscreen_swap_buffers (COGL_ONSCREEN (data->fb));
	}
      
      cogl_poll_renderer_get_info (cogl_context_get_renderer (ctx),
				   &poll_fds, &n_poll_fds, &timeout);
      
      g_poll ((GPollFD *) poll_fds, n_poll_fds,
	      timeout == -1 ? -1 : timeout / 1000);
      
      cogl_poll_renderer_dispatch (cogl_context_get_renderer (ctx),
				   poll_fds, n_poll_fds);
    }
}

static void
draw_offscreen (Data *data)
{
  uint32_t i;

  paint_cube (data);

  data->timer = g_timer_new ();
  g_timer_start (data->timer);
  
  for (i = 0; i < 10000; i++) {
    paint_fb (data);
  }
  cogl_framebuffer_finish (data->fb);
  
  g_timer_stop (data->timer);

  printf ("elapsed=%f\n", g_timer_elapsed (data->timer, NULL));
}

int
main (int argc, char **argv)
{
  CoglContext *ctx;
  CoglTexture *offscreen_texture, *depth_texture;
  CoglOffscreen *offscreen;
  CoglError *error = NULL;
  Data data;
  float fovy, aspect, z_near, z_2d, z_far;
  CoglDepthState depth_state;
  uint8_t *pixels;
  uint32_t width = 1920, height = 1080;
  CoglBool use_offscreen = TRUE;

  ctx = cogl_context_new (NULL, &error);
  if (!ctx) {
      fprintf (stderr, "Failed to create context: %s\n", error->message);
      return 1;
  }

  offscreen_texture = cogl_texture_2d_new_with_size (ctx, 1920, 1080);
  offscreen = cogl_offscreen_new_with_texture (offscreen_texture);
  cogl_object_unref (offscreen_texture);
  data.off_fb = offscreen;
  cogl_framebuffer_set_depth_texture_enabled (data.off_fb, TRUE);
  cogl_framebuffer_set_depth_write_enabled (data.off_fb, TRUE);
  data.off_fb_width = cogl_framebuffer_get_width (data.off_fb);
  data.off_fb_height = cogl_framebuffer_get_height (data.off_fb);
  cogl_framebuffer_orthographic (data.off_fb, 0, 0,
				 data.off_fb_width, data.off_fb_height,
				 -1, 100);

  assert (cogl_framebuffer_allocate (data.off_fb, NULL));

  depth_texture = cogl_framebuffer_get_depth_texture (data.off_fb);
  
  /**/
  if (use_offscreen)
    {
      offscreen_texture = cogl_texture_2d_new_with_size (ctx, width, height);
      offscreen = cogl_offscreen_new_with_texture (offscreen_texture);
      cogl_object_unref (offscreen_texture);
      data.fb = offscreen;
    }
  else
    {
      data.fb = cogl_onscreen_new (ctx, width, height);
    }

  if (!cogl_is_onscreen (data.fb))
    {
      cogl_framebuffer_set_depth_texture_enabled (data.fb, FALSE);
      cogl_framebuffer_set_depth_write_enabled (data.fb, FALSE);
    }
  else
    {
      //cogl_framebuffer_set_samples_per_pixel (data.fb, 4);
    }
  data.framebuffer_width = cogl_framebuffer_get_width (data.fb);
  data.framebuffer_height = cogl_framebuffer_get_height (data.fb);
  cogl_framebuffer_orthographic (data.fb, 0, 0,
				 data.framebuffer_width, data.framebuffer_height,
				 -1, 100);

  assert (cogl_framebuffer_allocate (data.fb, NULL));
  if (cogl_is_onscreen (data.fb))
    cogl_onscreen_show (COGL_ONSCREEN (data.fb));

  cogl_framebuffer_set_viewport (data.fb, 0, 0,
                                 data.framebuffer_width,
                                 data.framebuffer_height);
  cogl_framebuffer_set_viewport (data.off_fb, 0, 0,
                                 data.off_fb_width,
                                 data.off_fb_height);

  fovy = 60; /* y-axis field of view */
  aspect = (float)data.framebuffer_width/(float)data.framebuffer_height;
  z_near = 0.1; /* distance to near clipping plane */
  z_2d = 1000; /* position to 2d plane */
  z_far = 2000; /* distance to far clipping plane */

  cogl_framebuffer_perspective (data.fb, fovy, aspect, z_near, z_far);
  cogl_framebuffer_perspective (data.off_fb, fovy, aspect, z_near, z_far);

  /* Since the pango renderer emits geometry in pixel/device coordinates
   * and the anti aliasing is implemented with the assumption that the
   * geometry *really* does end up pixel aligned, we setup a modelview
   * matrix so that for geometry in the plane z = 0 we exactly map x
   * coordinates in the range [0,stage_width] and y coordinates in the
   * range [0,stage_height] to the framebuffer extents with (0,0) being
   * the top left.
   *
   * This is roughly what Clutter does for a ClutterStage, but this
   * demonstrates how it is done manually using Cogl.
   */
  cogl_matrix_init_identity (&data.view);
  cogl_matrix_view_2d_in_perspective (&data.view, fovy, aspect, z_near, z_2d,
                                      data.framebuffer_width,
                                      data.framebuffer_height);
  cogl_framebuffer_set_modelview_matrix (data.fb, &data.view);
  cogl_matrix_init_identity (&data.view);
  cogl_matrix_view_2d_in_perspective (&data.view, fovy, aspect, z_near, z_2d,
                                      data.off_fb_width,
                                      data.off_fb_height);
  cogl_framebuffer_set_modelview_matrix (data.off_fb, &data.view);

  /* rectangle indices allow the GPU to interpret a list of quads (the
   * faces of our cube) as a list of triangles.
   *
   * Since this is a very common thing to do
   * cogl_get_rectangle_indices() is a convenience function for
   * accessing internal index buffers that can be shared.
   */
  data.indices = cogl_get_rectangle_indices (ctx, 6 /* n_rectangles */);
  data.prim = cogl_primitive_new_p3t2 (ctx, COGL_VERTICES_MODE_TRIANGLES,
                                       G_N_ELEMENTS (vertices),
                                       vertices);
  /* Each face will have 6 indices so we have 6 * 6 indices in total... */
  cogl_primitive_set_indices (data.prim,
                              data.indices,
                              6 * 6);

  /* a CoglPipeline conceptually describes all the state for vertex
   * processing, fragment processing and blending geometry. When
   * drawing the geometry for the crate this pipeline says to sample a
   * single texture during fragment processing... */
  data.crate_pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_color4f (data.crate_pipeline, 1.0, 0.0, 0.0, 1.0);

  /* Since the box is made of multiple triangles that will overlap
   * when drawn and we don't control the order they are drawn in, we
   * enable depth testing to make sure that triangles that shouldn't
   * be visible get culled by the GPU. */
  cogl_depth_state_init (&depth_state);
  cogl_depth_state_set_test_enabled (&depth_state, TRUE);
  cogl_pipeline_set_depth_state (data.crate_pipeline, &depth_state, NULL);

  data.copy_pipeline = cogl_pipeline_new (ctx);
  cogl_pipeline_set_layer_texture (data.copy_pipeline, 0, depth_texture);

  cogl_pipeline_add_layer_snippet (data.copy_pipeline, 0,
  				   cogl_snippet_new (COGL_SNIPPET_HOOK_LAYER_FRAGMENT,
  						     "float linearize_depth(float depth) {\n"
						     "  float n = 0.1;\n"
						     "  float f = 1000.0;\n"
						     "  return (2.0 * n) / (f + n - depth * (f - n));\n"
						     "}\n",
  						     "  vec4 depth = texture2D (cogl_sampler0, cogl_tex_coord0_in.xy);\n"
  						     "  cogl_layer = vec4(1.0 - linearize_depth(depth.r), 0, 0, 1.0);\n"));


  if (cogl_is_onscreen (data.fb))
    draw_onscreen (ctx, &data);
  else
    draw_offscreen (&data);

  pixels = g_malloc (data.framebuffer_width * data.framebuffer_height * 4);
  cogl_framebuffer_read_pixels (data.fb, 0, 0,
  				data.framebuffer_width,
  				data.framebuffer_height,
  				COGL_PIXEL_FORMAT_RGBA_8888, pixels);

  g_file_set_contents ("/tmp/bmp", (const char *) pixels,
  		       data.framebuffer_width * data.framebuffer_height * 4, NULL);

  return 0;
}

