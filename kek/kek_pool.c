#include <string.h>
#include "kek.h"
#include "kek_pool.h"
#include "kek_model.h"
#include "kek_texture.h"

static KEK_ModelPoolSlot KEK_MODEL_POOL_STORAGE[KEK_MODEL_POOL_CAPACITY];
static KEK_TexturePoolSlot KEK_TEXTURE_POOL_STORAGE[KEK_TEXTURE_POOL_CAPACITY];

static KEK_ModelHandle kek_pool_make_model_handle(uint16_t index, uint16_t generation) {
    return ((uint32_t)generation << 16) | ((uint32_t)index + 1u);
}

static KEK_TextureHandle kek_pool_make_texture_handle(uint16_t index, uint16_t generation) {
    return ((uint32_t)generation << 16) | ((uint32_t)index + 1u);
}

static uint16_t kek_pool_model_index(KEK_ModelHandle handle) {
    return (uint16_t)((handle & 0xFFFFu) - 1u);
}

static uint16_t kek_pool_texture_index(KEK_TextureHandle handle) {
    return (uint16_t)((handle & 0xFFFFu) - 1u);
}

static uint16_t kek_pool_generation(uint32_t handle) {
    return (uint16_t)(handle >> 16);
}

static void kek_pool_init_texture_slot(KEK_TexturePoolSlot* slot) {
    memset(slot->data, 0, sizeof(slot->data));
    slot->texture.data = slot->data;
    slot->texture.width = 0;
    slot->texture.height = 0;
    slot->pixel_capacity = KEK_POOL_TEXTURE_PIXELS_MAX;
}

static void kek_pool_init_model_slot(KEK_ModelPoolSlot* slot) {
    memset(slot->verts, 0, sizeof(slot->verts));
    memset(slot->faces, 0, sizeof(slot->faces));
    memset(slot->face_normals, 0, sizeof(slot->face_normals));
    memset(slot->face_colors, 0, sizeof(slot->face_colors));
    memset(slot->face_textures, 0, sizeof(slot->face_textures));
    slot->model.verts = slot->verts;
    slot->model.faces = slot->faces;
    slot->model.face_normals = slot->face_normals;
    slot->model.face_colors = slot->face_colors;
    slot->model.texture = 0;
    slot->model.face_textures = slot->face_textures;
    slot->model.verts_count = 0;
    slot->model.faces_count = 0;
    slot->model.face_normals_count = 0;
    slot->model.colors_count = 0;
    slot->model.textures_count = 0;
    slot->verts_capacity = KEK_POOL_MODEL_VERTS_MAX;
    slot->faces_capacity = KEK_POOL_MODEL_FACES_MAX;
    slot->face_normals_capacity = KEK_POOL_MODEL_FACES_MAX;
    slot->colors_capacity = KEK_POOL_MODEL_COLORS_MAX;
    slot->textures_capacity = KEK_POOL_MODEL_UVS_MAX;
}

static int kek_pool_copy_texture(KEK_TexturePoolSlot* slot, const KEK_texture* source) {
    uint32_t pixel_count;

    if (!source || !source->data) {
        return 0;
    }

    pixel_count = (uint32_t)source->width * (uint32_t)source->height;
    if (pixel_count > slot->pixel_capacity) {
        return 0;
    }

    memcpy(slot->data, source->data, pixel_count);
    slot->texture.width = source->width;
    slot->texture.height = source->height;
    return 1;
}

static int kek_pool_copy_model(KEK_ModelPoolSlot* slot, const KEK_model* source) {
    if (!source || !source->verts || !source->faces) {
        return 0;
    }
    if (source->verts_count > slot->verts_capacity ||
        source->faces_count > slot->faces_capacity ||
        source->face_normals_count > slot->face_normals_capacity ||
        source->colors_count > slot->colors_capacity ||
        source->textures_count > slot->textures_capacity) {
        return 0;
    }

    memcpy(slot->verts, source->verts, sizeof(slot->verts[0]) * source->verts_count);
    memcpy(slot->faces, source->faces, sizeof(slot->faces[0]) * source->faces_count);
    if (source->face_normals && source->face_normals_count > 0) {
        memcpy(slot->face_normals, source->face_normals, sizeof(slot->face_normals[0]) * source->face_normals_count);
    }
    if (source->face_colors && source->colors_count > 0) {
        memcpy(slot->face_colors, source->face_colors, sizeof(slot->face_colors[0]) * source->colors_count);
    }
    if (source->face_textures && source->textures_count > 0) {
        memcpy(slot->face_textures, source->face_textures, sizeof(slot->face_textures[0]) * source->textures_count);
    }

    slot->model.verts_count = source->verts_count;
    slot->model.faces_count = source->faces_count;
    slot->model.face_normals_count = source->face_normals ? source->face_normals_count : 0;
    slot->model.colors_count = source->face_colors ? source->colors_count : 0;
    slot->model.textures_count = source->face_textures ? source->textures_count : 0;
    slot->model.texture = source->texture;
    return 1;
}

static void kek_pool_reset(KEK_engine* e) {
    for (uint16_t i = 0; i < e->model_pool.capacity; ++i) {
        KEK_ModelPoolSlot* slot = &e->model_pool.slots[i];
        memset(slot, 0, sizeof(*slot));
        slot->generation = 1;
        kek_pool_init_model_slot(slot);
    }

    for (uint16_t i = 0; i < e->texture_pool.capacity; ++i) {
        KEK_TexturePoolSlot* slot = &e->texture_pool.slots[i];
        memset(slot, 0, sizeof(*slot));
        slot->generation = 1;
        kek_pool_init_texture_slot(slot);
    }
}

void kek_pool_init(KEK_engine* e) {
    KEK_TextureHandle default_texture;
    KEK_ModelHandle default_model;

    e->model_pool.slots = KEK_MODEL_POOL_STORAGE;
    e->model_pool.capacity = KEK_MODEL_POOL_CAPACITY;
    e->texture_pool.slots = KEK_TEXTURE_POOL_STORAGE;
    e->texture_pool.capacity = KEK_TEXTURE_POOL_CAPACITY;
    e->default_texture = KEK_TEXTURE_HANDLE_INVALID;
    e->default_cube_model = KEK_MODEL_HANDLE_INVALID;

    kek_pool_reset(e);

    default_texture = kek_texture_clone(e, &KEK_DEFAULT_TEXTURE);
    default_model = kek_model_clone(e, &KEK_CUBE_MODEL);

    e->default_texture = default_texture;
    e->default_cube_model = default_model;

    if (default_model != KEK_MODEL_HANDLE_INVALID) {
        KEK_model* mdl = kek_model_get(e, default_model);
        KEK_texture* tex = kek_texture_get(e, default_texture);
        if (mdl) {
            mdl->texture = tex;
        }
    }
}

KEK_TextureHandle kek_texture_create(KEK_engine* e) {
    if (!e) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    for (uint16_t i = 0; i < e->texture_pool.capacity; ++i) {
        KEK_TexturePoolSlot* slot = &e->texture_pool.slots[i];
        if (slot->used) {
            continue;
        }

        slot->used = 1;
        kek_pool_init_texture_slot(slot);
        return kek_pool_make_texture_handle(i, slot->generation);
    }

    return KEK_TEXTURE_HANDLE_INVALID;
}

KEK_TextureHandle kek_texture_clone(KEK_engine* e, const KEK_texture* source) {
    KEK_TextureHandle handle = kek_texture_create(e);
    KEK_TexturePoolSlot* slot;

    if (handle == KEK_TEXTURE_HANDLE_INVALID) {
        return handle;
    }

    slot = &e->texture_pool.slots[kek_pool_texture_index(handle)];
    if (!kek_pool_copy_texture(slot, source)) {
        slot->used = 0;
        slot->generation += 1;
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    return handle;
}

KEK_texture* kek_texture_get(KEK_engine* e, KEK_TextureHandle handle) {
    KEK_TexturePoolSlot* slot;
    uint16_t index;

    if (!e || handle == KEK_TEXTURE_HANDLE_INVALID) {
        return 0;
    }

    index = kek_pool_texture_index(handle);
    if (index >= e->texture_pool.capacity) {
        return 0;
    }

    slot = &e->texture_pool.slots[index];
    if (!slot->used || slot->generation != kek_pool_generation(handle)) {
        return 0;
    }

    return &slot->texture;
}

void kek_texture_destroy(KEK_engine* e, KEK_TextureHandle handle) {
    KEK_TexturePoolSlot* slot;
    uint16_t index;

    if (!e || handle == KEK_TEXTURE_HANDLE_INVALID || handle == e->default_texture) {
        return;
    }

    index = kek_pool_texture_index(handle);
    if (index >= e->texture_pool.capacity) {
        return;
    }

    slot = &e->texture_pool.slots[index];
    if (!slot->used || slot->generation != kek_pool_generation(handle)) {
        return;
    }

    slot->used = 0;
    slot->generation += 1;
    kek_pool_init_texture_slot(slot);
}

KEK_ModelHandle kek_model_create(KEK_engine* e) {
    if (!e) {
        return KEK_MODEL_HANDLE_INVALID;
    }

    for (uint16_t i = 0; i < e->model_pool.capacity; ++i) {
        KEK_ModelPoolSlot* slot = &e->model_pool.slots[i];
        if (slot->used) {
            continue;
        }

        slot->used = 1;
        kek_pool_init_model_slot(slot);
        return kek_pool_make_model_handle(i, slot->generation);
    }

    return KEK_MODEL_HANDLE_INVALID;
}

KEK_ModelHandle kek_model_clone(KEK_engine* e, const KEK_model* source) {
    KEK_ModelHandle handle = kek_model_create(e);
    KEK_ModelPoolSlot* slot;

    if (handle == KEK_MODEL_HANDLE_INVALID) {
        return handle;
    }

    slot = &e->model_pool.slots[kek_pool_model_index(handle)];
    if (!kek_pool_copy_model(slot, source)) {
        slot->used = 0;
        slot->generation += 1;
        return KEK_MODEL_HANDLE_INVALID;
    }

    return handle;
}

KEK_model* kek_model_get(KEK_engine* e, KEK_ModelHandle handle) {
    KEK_ModelPoolSlot* slot;
    uint16_t index;

    if (!e || handle == KEK_MODEL_HANDLE_INVALID) {
        return 0;
    }

    index = kek_pool_model_index(handle);
    if (index >= e->model_pool.capacity) {
        return 0;
    }

    slot = &e->model_pool.slots[index];
    if (!slot->used || slot->generation != kek_pool_generation(handle)) {
        return 0;
    }

    return &slot->model;
}

void kek_model_destroy(KEK_engine* e, KEK_ModelHandle handle) {
    KEK_ModelPoolSlot* slot;
    uint16_t index;

    if (!e || handle == KEK_MODEL_HANDLE_INVALID || handle == e->default_cube_model) {
        return;
    }

    index = kek_pool_model_index(handle);
    if (index >= e->model_pool.capacity) {
        return;
    }

    slot = &e->model_pool.slots[index];
    if (!slot->used || slot->generation != kek_pool_generation(handle)) {
        return;
    }

    slot->used = 0;
    slot->generation += 1;
    kek_pool_init_model_slot(slot);
}

KEK_TextureHandle kek_default_texture_handle(KEK_engine* e) {
    if (!e) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }
    return e->default_texture;
}

KEK_ModelHandle kek_default_cube_model_handle(KEK_engine* e) {
    if (!e) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    return e->default_cube_model;
}
