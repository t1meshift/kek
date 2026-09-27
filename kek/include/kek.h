#ifndef KEK_H
#define KEK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "kek_arena.h"
#include "kek_asset.h"
#include "kek_config.h"
#include "kek_palette.h"
#include "kek_keyboard.h"
#include "kek_light.h"
#include "kek_pool.h"

typedef struct KEK_scene KEK_scene;
#ifndef KEK_ENGINE_DEFINED
#define KEK_ENGINE_DEFINED
typedef struct KEK_engine KEK_engine;
#endif

struct KEK_scene {
    void (*enter)(KEK_scene* scene, KEK_engine* e);
    void (*exit)(KEK_scene* scene, KEK_engine* e);
    void (*update)(KEK_scene* scene, KEK_engine* e, float dt);
    void (*render)(KEK_scene* scene, KEK_engine* e);
    void (*key_up)(KEK_scene* scene, KEK_engine* e, KEK_scancode key);
    void (*key_down)(KEK_scene* scene, KEK_engine* e, KEK_scancode key);
    uint32_t tag;
};

struct KEK_engine {
    KEK_scene* scene;
    KEK_scene* next_scene;
    KEK_AssetProvider* assets;
    /* All four point into the block handed to kek_init. */
    KEK_palette_item* palette; /* 256-color palette */
    uint8_t* shading_palette;
    uint8_t* fb; /** framebuffer */
    float* db; /** depth buffer */
    uint16_t w; /** buffer width */
    uint16_t h; /** buffer height */
    uint16_t target_fps;
    /* The rest of the block: the pools' tables and everything in them. */
    KEK_arena arena;
    KEK_ModelPool model_pool;
    KEK_TexturePool texture_pool;
    KEK_ModelHandle default_cube_model;
    KEK_TextureHandle default_texture;
    KEK_TextureWarpMode texture_warp_mode;
    KEK_light light;
    KEK_KeyboardState keyboard;
};

/* What an engine is made with. Fields may be added; a caller that fills it with
   a designated initializer keeps compiling and gets zero for anything new. */
typedef struct KEK_desc {
    uint16_t width;  /* of the frame, in pixels */
    uint16_t height;
    /* How many models and textures can be alive at once: the size of the
       handle tables, not of the arena. 0 means KEK_DEFAULT_MODELS and
       KEK_DEFAULT_TEXTURES. The defaults take one of each. */
    uint16_t models;
    uint16_t textures;
} KEK_desc;

#define KEK_DEFAULT_MODELS 64u
#define KEK_DEFAULT_TEXTURES 64u

/* The application hands kek_init one block and the engine lays out its frame,
   depth buffer, palettes and handle tables inside it; where the block comes
   from — a static array, one malloc at startup, a fixed region on a machine
   without either — is the application's business. The block need not be
   aligned: the layout starts at the first KEK_MEMORY_ALIGN boundary inside
   it, and the size accounts for that.

   kek_memory_size is the least block kek_init takes, and all it holds beyond
   the frame is the tables and the default cube and texture. Models and
   textures come from whatever the application adds on top: that is the
   arena, and its size is the budget for assets.

       static unsigned char memory[KEK_MEMORY_SIZE(320, 200) + 512 * 1024];

   KEK_MEMORY_SIZE is kek_memory_size of a desc with the default table sizes,
   as a constant expression for sizing a static array. Its arguments are
   evaluated more than once. */
#define KEK_MEMORY_ALIGN 16u
#define KEK_MEMORY_ROUND_(n) \
    (((size_t)(n) + (KEK_MEMORY_ALIGN - 1u)) & ~(size_t)(KEK_MEMORY_ALIGN - 1u))
/* Room for the default cube and texture, which kek_init creates in the arena.
   test_memory proves it is enough. */
#define KEK_MEMORY_BUILTIN_ 2048u
#define KEK_MEMORY_SIZE_FOR_(width, height, models, textures) \
    ((size_t)(KEK_MEMORY_ALIGN - 1u) \
     + KEK_MEMORY_ROUND_((size_t)(width) * (size_t)(height)) \
     + KEK_MEMORY_ROUND_((size_t)(width) * (size_t)(height) * sizeof(float)) \
     + KEK_MEMORY_ROUND_(256u * sizeof(KEK_palette_item)) \
     + KEK_MEMORY_ROUND_((size_t)256u * (size_t)KEK_PALETTE_SHADING_LEVELS) \
     + KEK_MEMORY_ROUND_((size_t)(models) * sizeof(KEK_ModelPoolSlot)) \
     + KEK_MEMORY_ROUND_((size_t)(textures) * sizeof(KEK_TexturePoolSlot)) \
     + (size_t)KEK_MEMORY_BUILTIN_)
#define KEK_MEMORY_SIZE(width, height) \
    KEK_MEMORY_SIZE_FOR_(width, height, KEK_DEFAULT_MODELS, KEK_DEFAULT_TEXTURES)

size_t kek_memory_size(const KEK_desc* desc);

/* Returns 1 with the engine ready, or 0 without touching the engine or the
   block when desc has a zero dimension or the block is null or smaller than
   kek_memory_size.
   The engine points into the block from then on, so the block has to outlive
   it. The frame starts cleared and the palette is the default one. */
int kek_init(KEK_engine* engine, const KEK_desc* desc, void* memory, size_t size);

void kek_set_palette(KEK_engine* e, const KEK_palette_item* palette);
void kek_set_shading_palette(KEK_engine* e, const uint8_t* shading_palette);
void kek_invalidate_shading_palette(KEK_engine* e);

void kek_set_scene(KEK_engine* engine, KEK_scene* scene);
void kek_request_scene(KEK_engine* engine, KEK_scene* scene);

void kek_flush_buffers(KEK_engine* engine);

/* Keyboard events from the platform layer. Each updates the engine's keyboard
   state before it reaches the scene's key_down/key_up callback, so a callback
   already sees its own key as held and pressed. Scancodes from
   KEK_SCANCODE_SIZE up still reach the callback but are not tracked. */
void kek_key_down(KEK_engine* engine, uint16_t key);
void kek_key_up(KEK_engine* engine, uint16_t key);

/* Keyboard state, for a scene to poll instead of keeping a bitmask of its own.
   Out-of-range scancodes are never held, pressed or released.

   held is the key as of the last event. pressed and released are edges
   collected from every event since the previous kek_update(), and kek_update()
   clears them right after the scene's update has run. So each edge is seen by
   exactly one update, however many events or skipped frames led up to it; by
   kek_render() it is gone. A tap shorter than a frame shows up as pressed and
   released in the same update without ever being held, and OS key repeat is
   not a second press.

   A scene switch does not carry presses over: kek_set_scene() and the switch
   at the end of kek_update() both leave the new scene, from its enter() on,
   with no pending edges, so the key that caused the switch does not fire
   again on the other side. A key held across it is still held, and its
   release is reported. */
int kek_key_held(const KEK_engine* engine, uint16_t key);
int kek_key_pressed(const KEK_engine* engine, uint16_t key);
int kek_key_released(const KEK_engine* engine, uint16_t key);

/* dt is the time that actually passed since the previous call, in
   milliseconds — the platform layer measures it. Clamped to KEK_MAX_FRAME_MS
   before it reaches the scene. */
void kek_update(KEK_engine* engine, float dt);
void kek_render(KEK_engine* engine);

#ifdef __cplusplus
}
#endif

#endif
