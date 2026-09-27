#ifndef KEK_H
#define KEK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_asset.h"
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
    KEK_light light;
    KEK_KeyboardState keyboard;
};

KEK_engine kek_init(void);
void kek_pool_init(KEK_engine* engine);

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
