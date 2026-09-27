#ifndef KEK_CONFIG_H
#define KEK_CONFIG_H

/* The frame's size is not here: it is KEK_desc's, chosen by the application
   at kek_init. */

#ifndef KEK_TARGET_FPS
#define KEK_TARGET_FPS 30u
#endif

/* Upper clamp on the dt handed to a scene, in milliseconds. A frame that really
   did take a second — a breakpoint, a backgrounded tab, a level load — must not
   arrive as one giant step that walks the camera through a wall. The simulation
   runs slow for that frame instead, which is the lesser of the two. */
#ifndef KEK_MAX_FRAME_MS
#define KEK_MAX_FRAME_MS 100.f
#endif

#ifndef KEK_MODEL_POOL_CAPACITY
#define KEK_MODEL_POOL_CAPACITY 8u
#endif

#ifndef KEK_TEXTURE_POOL_CAPACITY
#define KEK_TEXTURE_POOL_CAPACITY 8u
#endif

#ifndef KEK_POOL_MODEL_VERTS_MAX
#define KEK_POOL_MODEL_VERTS_MAX 1024u
#endif

#ifndef KEK_POOL_MODEL_FACES_MAX
#define KEK_POOL_MODEL_FACES_MAX 1024u
#endif

#ifndef KEK_POOL_MODEL_COLORS_MAX
#define KEK_POOL_MODEL_COLORS_MAX 1024u
#endif

/* Bounds two things that happen to share a limit: the distinct UVs a KMF may
   declare (the scratch buffer they are staged into in kek_file_model.c) and,
   once a model is textured, its faces_count too — the pool's face_textures
   array holds one UV triple per face, not per UV. Both are checked in
   kek_file_model_load. */
#ifndef KEK_POOL_MODEL_UVS_MAX
#define KEK_POOL_MODEL_UVS_MAX 1024u
#endif

#ifndef KEK_POOL_TEXTURE_PIXELS_MAX
#define KEK_POOL_TEXTURE_PIXELS_MAX (256u * 256u)
#endif

/* Signed, unlike its neighbours, and the same as the -D CMake passes: it bounds
   loops over int and clamps the signed shade in kek_palette_shade, where an
   unsigned default would turn a negative shade into a huge one. */
#ifndef KEK_PALETTE_SHADING_LEVELS
#define KEK_PALETTE_SHADING_LEVELS 4
#endif

#endif // KEK_CONFIG_H
