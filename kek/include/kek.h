#ifndef KEK_H
#define KEK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_asset.h"
#include "kek_palette.h"
#include "kek_keyboard.h"
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
    KEK_palette_item* palette; /* 256-color palette */
    uint8_t* shading_palette;
    uint8_t* fb; /** framebuffer */
    float* db; /** depth buffer */
    uint16_t w; /** buffer width */
    uint16_t h; /** buffer height */
    uint16_t target_fps;
    KEK_ModelPool model_pool;
    KEK_TexturePool texture_pool;
    KEK_ModelHandle default_cube_model;
    KEK_TextureHandle default_texture;
    KEK_TextureWarpMode texture_warp_mode;
};

KEK_engine kek_init(void);
void kek_pool_init(KEK_engine* engine);

void kek_set_palette(KEK_engine* e, const KEK_palette_item* palette);
void kek_set_shading_palette(KEK_engine* e, const uint8_t* shading_palette);
void kek_invalidate_shading_palette(KEK_engine* e);

void kek_set_scene(KEK_engine* engine, KEK_scene* scene);
void kek_request_scene(KEK_engine* engine, KEK_scene* scene);

void kek_flush_buffers(KEK_engine* engine);

void kek_key_down(KEK_engine* engine, uint16_t key);
void kek_key_up(KEK_engine* engine, uint16_t key);

/* dt is the time that actually passed since the previous call, in
   milliseconds — the platform layer measures it. Clamped to KEK_MAX_FRAME_MS
   before it reaches the scene. */
void kek_update(KEK_engine* engine, float dt);
void kek_render(KEK_engine* engine);

#ifdef __cplusplus
}
#endif

#endif
