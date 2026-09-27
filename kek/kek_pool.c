#include <stdint.h>
#include <string.h>
#include "kek.h"
#include "kek_internal.h"
#include "kek_pool.h"
#include "kek_model.h"
#include "kek_texture.h"

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

/* A model's arrays, one after another in one block, each on an alignment
   boundary. Offsets into the block; absent arrays take no room. */
typedef struct KEK_ModelLayout_ {
    size_t faces;
    size_t normals;
    size_t colors;
    size_t uvs;
    size_t total;
} KEK_ModelLayout_;

/* No array gets past an eighth of SIZE_MAX, so the five of them, rounded, add
   up without wrapping — a limit that only a 32-bit target can reach. */
static int kek_pool_array_size_(uint32_t count, size_t element, size_t* out) {
    if (count > (SIZE_MAX / 8u) / element) {
        return 0;
    }
    *out = KEK_MEMORY_ROUND_((size_t)count * element);
    return 1;
}

static int kek_pool_model_layout_(uint32_t verts_count, uint32_t faces_count, unsigned flags,
                                  KEK_ModelLayout_* out) {
    size_t verts, faces, normals, colors = 0, uvs = 0;

    if (!kek_pool_array_size_(verts_count, sizeof(KEK_FVec3), &verts) ||
        !kek_pool_array_size_(faces_count, sizeof(KEK_model_face), &faces) ||
        !kek_pool_array_size_(faces_count, sizeof(KEK_model_face_normal), &normals) ||
        ((flags & KEK_MODEL_FACE_COLORS) && !kek_pool_array_size_(faces_count, 1u, &colors)) ||
        ((flags & KEK_MODEL_FACE_UVS) && !kek_pool_array_size_(faces_count, sizeof(KEK_model_face_uv), &uvs))) {
        return 0;
    }
    out->faces = verts;
    out->normals = out->faces + faces;
    out->colors = out->normals + normals;
    out->uvs = out->colors + colors;
    out->total = out->uvs + uvs;
    return 1;
}

static int kek_pool_free_model_slot_(const KEK_engine* e, uint16_t* out_index) {
    for (uint16_t i = 0; i < e->model_pool.capacity; ++i) {
        if (!e->model_pool.slots[i].used) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int kek_pool_free_texture_slot_(const KEK_engine* e, uint16_t* out_index) {
    for (uint16_t i = 0; i < e->texture_pool.capacity; ++i) {
        if (!e->texture_pool.slots[i].used) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

/* The slot goes back without its storage: that is the arena's to take, by
   kek_arena_free or by a release. The generation moves on either way, which is
   what makes every handle to what was here stale. */
static void kek_pool_retire_model_slot_(KEK_ModelPoolSlot* slot) {
    memset(&slot->model, 0, sizeof(slot->model));
    slot->model.texture = KEK_TEXTURE_HANDLE_INVALID;
    slot->used = 0;
    slot->generation += 1;
}

static void kek_pool_retire_texture_slot_(KEK_TexturePoolSlot* slot) {
    memset(&slot->texture, 0, sizeof(slot->texture));
    slot->used = 0;
    slot->generation += 1;
}

int kek_pool_init(KEK_engine* e, uint16_t models, uint16_t textures) {
    KEK_TextureHandle default_texture;
    KEK_ModelHandle default_model;
    KEK_model* mdl;

    e->model_pool.slots = (KEK_ModelPoolSlot*)kek_arena_take(&e->arena, (size_t)models * sizeof(KEK_ModelPoolSlot));
    e->texture_pool.slots = (KEK_TexturePoolSlot*)kek_arena_take(&e->arena, (size_t)textures * sizeof(KEK_TexturePoolSlot));
    e->model_pool.capacity = models;
    e->texture_pool.capacity = textures;
    e->default_texture = KEK_TEXTURE_HANDLE_INVALID;
    e->default_cube_model = KEK_MODEL_HANDLE_INVALID;
    if (!e->model_pool.slots || !e->texture_pool.slots) {
        return 0;
    }

    for (uint16_t i = 0; i < models; ++i) {
        memset(&e->model_pool.slots[i], 0, sizeof(e->model_pool.slots[i]));
        e->model_pool.slots[i].model.texture = KEK_TEXTURE_HANDLE_INVALID;
        e->model_pool.slots[i].generation = 1;
    }
    for (uint16_t i = 0; i < textures; ++i) {
        memset(&e->texture_pool.slots[i], 0, sizeof(e->texture_pool.slots[i]));
        e->texture_pool.slots[i].generation = 1;
    }

    /* Blocks start here, and the defaults are the first of them. Once they
       are in, the floor moves over them too, so no release takes them. */
    e->arena.floor = e->arena.low;
    default_texture = kek_texture_clone(e, &KEK_DEFAULT_TEXTURE);
    default_model = kek_model_clone(e, &KEK_CUBE_MODEL);
    mdl = kek_model_get(e, default_model);
    if (default_texture == KEK_TEXTURE_HANDLE_INVALID || !mdl) {
        return 0;
    }
    /* Not owned: kek_texture_destroy refuses the default texture anyway, and
       the default cube is never destroyed either. */
    mdl->texture = default_texture;
    e->default_texture = default_texture;
    e->default_cube_model = default_model;
    e->arena.floor = e->arena.low;
    return 1;
}

void kek_pool_release(KEK_engine* e, KEK_ArenaMark mark) {
    for (uint16_t i = 0; i < e->model_pool.capacity; ++i) {
        KEK_ModelPoolSlot* slot = &e->model_pool.slots[i];
        if (!slot->used || !kek_arena_block_after(slot->block, mark)) {
            continue;
        }
        /* A texture this model owns goes with it wherever it lives; one from
           before the mark would otherwise stay behind with nobody to free it. */
        if (slot->model.owns_texture) {
            kek_texture_destroy(e, slot->model.texture);
        }
        kek_pool_retire_model_slot_(slot);
    }

    for (uint16_t i = 0; i < e->texture_pool.capacity; ++i) {
        KEK_TexturePoolSlot* slot = &e->texture_pool.slots[i];
        if (slot->used && kek_arena_block_after(slot->block, mark)) {
            kek_pool_retire_texture_slot_(slot);
        }
    }
}

KEK_TextureHandle kek_texture_create(KEK_engine* e, uint16_t width, uint16_t height) {
    KEK_TexturePoolSlot* slot;
    uint16_t index;
    size_t pixels = (size_t)width * height;
    size_t block;
    uint8_t* data;

    if (!e || !kek_pool_free_texture_slot_(e, &index)) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }
    data = (uint8_t*)kek_arena_alloc(&e->arena, pixels, &block);
    if (!data) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }
    memset(data, 0, pixels);

    slot = &e->texture_pool.slots[index];
    slot->texture.data = data;
    slot->texture.width = width;
    slot->texture.height = height;
    slot->block = block;
    slot->used = 1;
    return kek_pool_make_texture_handle(index, slot->generation);
}

KEK_TextureHandle kek_texture_clone(KEK_engine* e, const KEK_texture* source) {
    KEK_TextureHandle handle;
    KEK_texture* texture;

    if (!source || !source->data) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }
    handle = kek_texture_create(e, source->width, source->height);
    texture = kek_texture_get(e, handle);
    if (!texture) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }
    memcpy(texture->data, source->data, (size_t)source->width * source->height);
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

    if (!e || handle == e->default_texture || !kek_texture_get(e, handle)) {
        return;
    }

    slot = &e->texture_pool.slots[kek_pool_texture_index(handle)];
    kek_arena_free(&e->arena, slot->block);
    kek_pool_retire_texture_slot_(slot);
}

KEK_ModelHandle kek_model_create(KEK_engine* e, uint32_t verts_count, uint32_t faces_count, unsigned flags) {
    KEK_ModelPoolSlot* slot;
    KEK_ModelLayout_ layout;
    KEK_model* mdl;
    uint16_t index;
    size_t block;
    unsigned char* data;

    if (!e || !kek_pool_free_model_slot_(e, &index) ||
        !kek_pool_model_layout_(verts_count, faces_count, flags, &layout)) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    data = (unsigned char*)kek_arena_alloc(&e->arena, layout.total, &block);
    if (!data) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    memset(data, 0, layout.total);

    slot = &e->model_pool.slots[index];
    mdl = &slot->model;
    /* Every offset in the layout is on an alignment boundary, and so is the
       block, which is what makes these casts sound. */
    mdl->verts = (KEK_FVec3*)data;
    mdl->faces = (KEK_model_face*)(data + layout.faces);
    mdl->face_normals = (KEK_model_face_normal*)(data + layout.normals);
    mdl->face_colors = (flags & KEK_MODEL_FACE_COLORS) ? data + layout.colors : 0;
    mdl->face_textures = (flags & KEK_MODEL_FACE_UVS) ? (KEK_model_face_uv*)(data + layout.uvs) : 0;
    mdl->texture = KEK_TEXTURE_HANDLE_INVALID;
    mdl->owns_texture = 0;
    mdl->verts_count = verts_count;
    mdl->faces_count = faces_count;
    mdl->face_normals_count = faces_count;
    mdl->colors_count = (flags & KEK_MODEL_FACE_COLORS) ? faces_count : 0;
    mdl->textures_count = (flags & KEK_MODEL_FACE_UVS) ? faces_count : 0;
    slot->block = block;
    slot->used = 1;
    return kek_pool_make_model_handle(index, slot->generation);
}

KEK_ModelHandle kek_model_clone(KEK_engine* e, const KEK_model* source) {
    KEK_ModelHandle handle;
    KEK_model* mdl;
    unsigned flags = 0;
    uint32_t normals_count, colors_count, textures_count;

    if (!source || !source->verts || !source->faces) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    normals_count = source->face_normals ? source->face_normals_count : 0;
    colors_count = source->face_colors ? source->colors_count : 0;
    textures_count = source->face_textures ? source->textures_count : 0;
    /* Every per-face array is sized by faces_count, here and in the source. */
    if (normals_count > source->faces_count || colors_count > source->faces_count ||
        textures_count > source->faces_count) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    if (colors_count > 0) {
        flags |= KEK_MODEL_FACE_COLORS;
    }
    if (textures_count > 0) {
        flags |= KEK_MODEL_FACE_UVS;
    }

    handle = kek_model_create(e, source->verts_count, source->faces_count, flags);
    mdl = kek_model_get(e, handle);
    if (!mdl) {
        return KEK_MODEL_HANDLE_INVALID;
    }

    memcpy(mdl->verts, source->verts, sizeof(mdl->verts[0]) * source->verts_count);
    memcpy(mdl->faces, source->faces, sizeof(mdl->faces[0]) * source->faces_count);
    if (normals_count > 0) {
        memcpy(mdl->face_normals, source->face_normals, sizeof(mdl->face_normals[0]) * normals_count);
    }
    if (colors_count > 0) {
        memcpy(mdl->face_colors, source->face_colors, colors_count);
    }
    if (textures_count > 0) {
        memcpy(mdl->face_textures, source->face_textures, sizeof(mdl->face_textures[0]) * textures_count);
    }
    mdl->face_normals_count = normals_count;
    mdl->colors_count = colors_count;
    mdl->textures_count = textures_count;
    /* The clone points at the same texture but never owns it — only the
       original gets to release that slot. */
    mdl->texture = source->texture;
    mdl->owns_texture = 0;
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

    if (!e || handle == e->default_cube_model || !kek_model_get(e, handle)) {
        return;
    }

    slot = &e->model_pool.slots[kek_pool_model_index(handle)];
    if (slot->model.owns_texture) {
        kek_texture_destroy(e, slot->model.texture);
    }
    kek_arena_free(&e->arena, slot->block);
    kek_pool_retire_model_slot_(slot);
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
