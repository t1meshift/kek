#include <math.h>
#include "kek.h"
#include "kek_macro.h"
#include "kek_texture.h"

static float _kek_texture_clamp01(float value) {
    if (value < 0.f) {
        return 0.f;
    }
    if (value > 1.f) {
        return 1.f;
    }
    return value;
}

static float _kek_texture_wrap_repeat(float value) {
    value = value - floorf(value);
    if (value < 0.f) {
        value += 1.f;
    }
    return value;
}

static float _kek_texture_resolve_uv(const KEK_engine* e, float value) {
    KEK_TextureWarpMode mode = KEK_TEXTURE_WARP_CLAMP;

    if (e) {
        mode = e->texture_warp_mode;
    }

    if (mode == KEK_TEXTURE_WARP_REPEAT) {
        return _kek_texture_wrap_repeat(value);
    }

    return _kek_texture_clamp01(value);
}

uint8_t _kek_bricks_texture_data[16*16] = {
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,

    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,

    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,4,4,4,4,4,4,6,7,4,4,4,4,4,4,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,

    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
    6,4,4,4,4,4,4,4,6,7,4,4,4,4,4,4,
};

uint8_t _kek_default_texture_data[16*16] = {
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15, 1, 1, 1, 1, 1, 1,14,14, 2, 2, 2, 2, 2, 2,15,
    15,14,14,14,14,14,14, 0, 0,14,14,14,14,14,14,15,
    15,14,14,14,14,14,14, 0, 0,14,14,14,14,14,14,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15, 3, 3, 3, 3, 3, 3,14,14, 4, 4, 4, 4, 4, 4,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
};

uint8_t kek_texture_sample(const KEK_engine* e, const KEK_texture* texture, float u, float v) {
    uint16_t x;
    uint16_t y;

    u = _kek_texture_resolve_uv(e, u);
    v = _kek_texture_resolve_uv(e, v);

    x = KEK_MIN((uint16_t)(u * (float)texture->width), texture->width - 1);
    y = KEK_MIN((uint16_t)(v * (float)texture->height), texture->height - 1);

    return texture->data[(uint32_t)y * (uint32_t)texture->width + (uint32_t)x];
}

void kek_texture_set_warp_mode(KEK_engine* e, KEK_TextureWarpMode mode) {
    if (!e) {
        return;
    }

    e->texture_warp_mode = mode;
}

KEK_TextureWarpMode kek_texture_get_warp_mode(const KEK_engine* e) {
    if (!e) {
        return KEK_TEXTURE_WARP_CLAMP;
    }

    return e->texture_warp_mode;
}

KEK_texture KEK_DEFAULT_TEXTURE = {
    .data = _kek_bricks_texture_data,
    16,
    16
};
