#ifndef KEK_CONFIG_H
#define KEK_CONFIG_H

/* The framebuffer is 320x200 by default because that is VGA mode 13h: 256
   colours, one byte per pixel, exactly what KEK_engine.fb already is. A target
   with a different screen overrides these; everything downstream reads
   KEK_engine.w/.h rather than the macros. */
#ifndef KEK_BUFFER_WIDTH
#define KEK_BUFFER_WIDTH 320u
#endif

#ifndef KEK_BUFFER_HEIGHT
#define KEK_BUFFER_HEIGHT 200u
#endif

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

#ifndef KEK_POOL_MODEL_UVS_MAX
#define KEK_POOL_MODEL_UVS_MAX 1024u
#endif

#ifndef KEK_POOL_TEXTURE_PIXELS_MAX
#define KEK_POOL_TEXTURE_PIXELS_MAX (256u * 256u)
#endif

#ifndef KEK_PALETTE_SHADING_LEVELS
#define KEK_PALETTE_SHADING_LEVELS 4u
#endif

#endif // KEK_CONFIG_H
