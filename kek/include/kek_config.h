#ifndef KEK_CONFIG_H
#define KEK_CONFIG_H

/* Neither the frame's size nor any limit on models and textures is here: the
   first is KEK_desc's and the second is whatever fits in the arena, both
   chosen by the application at kek_init. */

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

/* Signed, unlike its neighbours, and the same as the -D CMake passes: it bounds
   loops over int and clamps the signed shade in kek_palette_shade, where an
   unsigned default would turn a negative shade into a huge one. */
#ifndef KEK_PALETTE_SHADING_LEVELS
#define KEK_PALETTE_SHADING_LEVELS 4
#endif

#endif // KEK_CONFIG_H
