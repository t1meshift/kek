#include <math.h>
#include <memory.h>
#include <stdint.h>
#include <string.h>
#include "kek.h"
#include "kek_palette.h"
#include "kek_config.h"

#define KEK_BUFFER_WIDTH 320
#define KEK_BUFFER_HEIGHT 200
#define KEK_TARGET_FPS 30

static uint8_t KEK_FRAMEBUFFER[KEK_BUFFER_WIDTH * KEK_BUFFER_HEIGHT];
static float KEK_DEPTHBUFFER[KEK_BUFFER_WIDTH * KEK_BUFFER_HEIGHT];
static KEK_palette_item KEK_PALETTE[256];
static uint8_t KEK_SHADING_PALETTE[256 * KEK_PALETTE_SHADING_LEVELS];

KEK_engine kek_init(void) {
    KEK_engine result;
    result.scene = 0;
    result.next_scene = 0;
    result.assets = 0;
    result.fb = KEK_FRAMEBUFFER;
    result.db = KEK_DEPTHBUFFER;
    result.w = KEK_BUFFER_WIDTH;
    result.h = KEK_BUFFER_HEIGHT;
    result.target_fps = KEK_TARGET_FPS;
    result.model_pool.slots = 0;
    result.model_pool.capacity = 0;
    result.texture_pool.slots = 0;
    result.texture_pool.capacity = 0;
    result.default_cube_model = KEK_MODEL_HANDLE_INVALID;
    result.default_texture = KEK_TEXTURE_HANDLE_INVALID;
    result.texture_warp_mode = KEK_TEXTURE_WARP_CLAMP;

    result.palette = KEK_PALETTE;
    memcpy(result.palette, KEK_DEFAULT_PALETTE, 256 * sizeof(KEK_palette_item));
    result.shading_palette = KEK_SHADING_PALETTE;
    kek_invalidate_shading_palette(&result);
    kek_pool_init(&result);

    return result;
}

void kek_set_palette(KEK_engine *e, KEK_palette_item *palette) {
    memcpy(e->palette, palette, 256 * sizeof(KEK_palette_item));
    kek_palette_calculate_shading(e->shading_palette, e->palette);
}

void kek_set_shading_palette(KEK_engine *e, KEK_palette_item *shading_palette) {
    memcpy(e->shading_palette, shading_palette, 256 * KEK_PALETTE_SHADING_LEVELS * sizeof(KEK_palette_item));
}

void kek_invalidate_shading_palette(KEK_engine *e) {
    kek_palette_calculate_shading(e->shading_palette, e->palette);
}

void kek_set_scene(KEK_engine *e, KEK_scene *scene) {
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
    memset(e->fb, 0, e->w * e->h);
    for (uint32_t i = 0; i < (uint32_t)e->w * (uint32_t)e->h; ++i) {
        e->db[i] = 0.f;
    }
}

void kek_blit(KEK_engine* e, uint16_t x, uint16_t y, uint8_t pixel) {
    e->fb[y * e->w + x] = pixel;
}

void kek_line(KEK_engine* e, uint16_t y, uint16_t x0, uint16_t x1, uint8_t pixel) {
    memset(e->fb + (y * e->w) + x0, pixel, x1 - x0 + 1);
}

void kek_key_down(KEK_engine* e, uint16_t key) {
    if (e->scene && e->scene->key_down) {
        e->scene->key_down(e->scene, e, key);
    }
}

void kek_key_up(KEK_engine* e, uint16_t key) {
    if (e->scene && e->scene->key_up) {
        e->scene->key_up(e->scene, e, key);
    }
}

static void _kek_apply_scene_switch(KEK_engine* e) {
    if (!e->next_scene || e->next_scene == e->scene) {
        e->next_scene = 0;
        return;
    }

    if (e->scene->exit) {
        e->scene->exit(e->scene, e);
    }

    e->scene = e->next_scene;
    e->next_scene = 0;

    if (e->scene->enter) {
        e->scene->enter(e->scene, e);
    }
}

void kek_update(KEK_engine* e) {
    KEK_scene* s = e->scene;
    if (s && s->update) {
        s->update(s, e, 1000.f / (float)e->target_fps);
    }

    _kek_apply_scene_switch(e);
}

void kek_render(KEK_engine* e) {
    kek_flush_buffers(e);
    
    KEK_scene* s = e->scene;
    if (s && s->render) {
        s->render(s, e);
    }
}
