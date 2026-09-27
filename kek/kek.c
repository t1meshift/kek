#include <math.h>
#include <memory.h>
#include <stdint.h>
#include <string.h>
#include "kek.h"
#include "kek_palette.h"
#include "kek_config.h"
#include "kek_math.h"
#include "kek_internal.h"

static uint16_t kek_desc_models_(const KEK_desc* desc) {
    return desc->models ? desc->models : (uint16_t)KEK_DEFAULT_MODELS;
}

static uint16_t kek_desc_textures_(const KEK_desc* desc) {
    return desc->textures ? desc->textures : (uint16_t)KEK_DEFAULT_TEXTURES;
}

size_t kek_memory_size(const KEK_desc* desc) {
    return KEK_MEMORY_SIZE_FOR_(desc->width, desc->height, kek_desc_models_(desc), kek_desc_textures_(desc));
}

/* Hands out the block front to back, each piece on a KEK_MEMORY_ALIGN
   boundary. The order and the rounding are KEK_MEMORY_SIZE's, which is what
   makes the size it promises enough. */
static void* kek_memory_take_(uintptr_t* cursor, size_t size) {
    void* result = (void*)*cursor;
    *cursor += KEK_MEMORY_ROUND_(size);
    return result;
}

int kek_init(KEK_engine* e, const KEK_desc* desc, void* memory, size_t size) {
    KEK_engine result;
    uintptr_t cursor;
    size_t pixels;

    if (!e || !desc || !memory || desc->width == 0 || desc->height == 0
        || size < kek_memory_size(desc)) {
        return 0;
    }
    pixels = (size_t)desc->width * desc->height;
    cursor = ((uintptr_t)memory + (KEK_MEMORY_ALIGN - 1u)) & ~(uintptr_t)(KEK_MEMORY_ALIGN - 1u);

    result.scene = 0;
    result.next_scene = 0;
    result.assets = 0;
    result.fb = (uint8_t*)kek_memory_take_(&cursor, pixels);
    result.db = (float*)kek_memory_take_(&cursor, pixels * sizeof(float));
    result.palette = (KEK_palette_item*)kek_memory_take_(&cursor, 256u * sizeof(KEK_palette_item));
    result.shading_palette = (uint8_t*)kek_memory_take_(&cursor, (size_t)256u * KEK_PALETTE_SHADING_LEVELS);
    result.w = desc->width;
    result.h = desc->height;
    result.target_fps = KEK_TARGET_FPS;
    /* Everything after the palettes, to the last alignment boundary in the
       block. The tables and the defaults come first, from kek_pool_init. */
    result.arena.base = (unsigned char*)cursor;
    result.arena.low = 0;
    result.arena.high = ((uintptr_t)memory + size - cursor) & ~(uintptr_t)(KEK_MEMORY_ALIGN - 1u);
    result.arena.floor = 0;
    result.model_pool.slots = 0;
    result.model_pool.capacity = 0;
    result.texture_pool.slots = 0;
    result.texture_pool.capacity = 0;
    result.default_cube_model = KEK_MODEL_HANDLE_INVALID;
    result.default_texture = KEK_TEXTURE_HANDLE_INVALID;
    result.texture_warp_mode = KEK_TEXTURE_WARP_CLAMP;
    /* From above, from the left and from behind the default camera, which
       looks down +z with y up: the faces a viewer sees first are the lit ones,
       and the top and left read differently from the sides. */
    result.light.direction = kek_normalize_fvec3_copy((KEK_FVec3){ 1.f, -2.f, 2.f });
    result.light.ambient = 0.25f;
    result.light.fog_start = 0.f;
    result.light.fog_end = 0.f;
    memset(&result.keyboard, 0, sizeof(result.keyboard));

    memcpy(result.palette, KEK_DEFAULT_PALETTE, 256 * sizeof(KEK_palette_item));
    kek_invalidate_shading_palette(&result);
    /* The block is whatever the application had lying there, not the zeroes a
       static buffer used to start with. */
    kek_flush_buffers(&result);
    /* kek_memory_size left room for exactly this, so it does not fail. */
    if (!kek_pool_init(&result, kek_desc_models_(desc), kek_desc_textures_(desc))) {
        return 0;
    }

    *e = result;
    return 1;
}

void kek_set_palette(KEK_engine *e, const KEK_palette_item *palette) {
    memcpy(e->palette, palette, 256 * sizeof(KEK_palette_item));
    kek_palette_calculate_shading(e->shading_palette, e->palette);
}

/* A shading palette is a table of palette *indices*, one row of 256 per shading
   level — not a table of colours. */
void kek_set_shading_palette(KEK_engine *e, const uint8_t *shading_palette) {
    memcpy(e->shading_palette, shading_palette, (size_t)256 * KEK_PALETTE_SHADING_LEVELS);
}

void kek_invalidate_shading_palette(KEK_engine *e) {
    kek_palette_calculate_shading(e->shading_palette, e->palette);
}

static int kek_keyboard_bit(const uint8_t* set, uint16_t key) {
    return key < KEK_SCANCODE_SIZE && ((set[key >> 3] >> (key & 7u)) & 1u);
}

/* Once the scene's update has seen the edges, and on every switch; kek.h has
   the contract. held is left alone — it is not an edge. */
static void kek_keyboard_consume_edges(KEK_engine* e) {
    memset(e->keyboard.pressed, 0, sizeof(e->keyboard.pressed));
    memset(e->keyboard.released, 0, sizeof(e->keyboard.released));
}

void kek_set_scene(KEK_engine *e, KEK_scene *scene) {
    /* Not only for switches made from outside the frame: a scene that switches
       from its own key_down callback would otherwise hand the triggering press
       straight to the new scene's first update. */
    kek_keyboard_consume_edges(e);

    if (e->scene && e->scene->exit) {
        e->scene->exit(e->scene, e);
    }
    e->scene = scene;
    e->next_scene = 0;

    if (e->scene && e->scene->enter) {
        e->scene->enter(e->scene, e);
    }
}

void kek_request_scene(KEK_engine *e, KEK_scene *scene) {
    e->next_scene = scene;
}

void kek_flush_buffers(KEK_engine* e) {
    memset(e->fb, 0, (size_t)e->w * e->h);
    for (uint32_t i = 0; i < (uint32_t)e->w * (uint32_t)e->h; ++i) {
        e->db[i] = 0.f;
    }
}

void kek_key_down(KEK_engine* e, uint16_t key) {
    KEK_KeyboardState* k = &e->keyboard;

    /* Only a transition is an edge: OS key repeat sends key_down again for a
       key that never went up. */
    if (key < KEK_SCANCODE_SIZE && !kek_keyboard_bit(k->held, key)) {
        k->held[key >> 3] |= (uint8_t)(1u << (key & 7u));
        k->pressed[key >> 3] |= (uint8_t)(1u << (key & 7u));
    }

    if (e->scene && e->scene->key_down) {
        e->scene->key_down(e->scene, e, key);
    }
}

void kek_key_up(KEK_engine* e, uint16_t key) {
    KEK_KeyboardState* k = &e->keyboard;

    /* A key_up for a key the engine never saw go down — held since before the
       window had focus, say — ends a press nobody was told about, so it is not
       reported as a release either. */
    if (kek_keyboard_bit(k->held, key)) {
        k->held[key >> 3] &= (uint8_t)~(1u << (key & 7u));
        k->released[key >> 3] |= (uint8_t)(1u << (key & 7u));
    }

    if (e->scene && e->scene->key_up) {
        e->scene->key_up(e->scene, e, key);
    }
}

int kek_key_held(const KEK_engine* e, uint16_t key) {
    return kek_keyboard_bit(e->keyboard.held, key);
}

int kek_key_pressed(const KEK_engine* e, uint16_t key) {
    return kek_keyboard_bit(e->keyboard.pressed, key);
}

int kek_key_released(const KEK_engine* e, uint16_t key) {
    return kek_keyboard_bit(e->keyboard.released, key);
}

static void kek_apply_scene_switch_(KEK_engine* e) {
    if (!e->next_scene || e->next_scene == e->scene) {
        e->next_scene = 0;
        return;
    }

    /* There may be no current scene to leave: kek_init() starts with none, so
       a kek_request_scene() before the first kek_set_scene() lands here with
       e->scene still null. kek_set_scene guards this; this did not. */
    if (e->scene && e->scene->exit) {
        e->scene->exit(e->scene, e);
    }

    e->scene = e->next_scene;
    e->next_scene = 0;

    if (e->scene->enter) {
        e->scene->enter(e->scene, e);
    }
}

void kek_update(KEK_engine* e, float dt) {
    KEK_scene* s;

    /* Written as a rejection rather than `dt < 0.f` so a NaN — which compares
       false against everything — lands on 0 instead of passing straight through
       into the scene's positions. */
    if (!(dt > 0.f)) {
        dt = 0.f;
    } else if (dt > KEK_MAX_FRAME_MS) {
        dt = KEK_MAX_FRAME_MS;
    }

    s = e->scene;
    if (s && s->update) {
        s->update(s, e, dt);
    }

    /* Before the switch, so that neither exit() nor the new scene's enter()
       sees edges the old scene's update has already had. Every event from here
       to the next kek_update() belongs to the next update. */
    kek_keyboard_consume_edges(e);

    kek_apply_scene_switch_(e);
}

void kek_render(KEK_engine* e) {
    kek_flush_buffers(e);
    
    KEK_scene* s = e->scene;
    if (s && s->render) {
        s->render(s, e);
    }
}
