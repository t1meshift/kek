#ifndef KEK_POOL_H
#define KEK_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_config.h"
#include "kek_model.h"
#include "kek_texture.h"

#define KEK_MODEL_HANDLE_INVALID ((KEK_ModelHandle)0u)
#define KEK_TEXTURE_HANDLE_INVALID ((KEK_TextureHandle)0u)

#ifndef KEK_MODEL_HANDLE_DEFINED
#define KEK_MODEL_HANDLE_DEFINED
typedef uint32_t KEK_ModelHandle;
#endif
#ifndef KEK_TEXTURE_HANDLE_DEFINED
#define KEK_TEXTURE_HANDLE_DEFINED
typedef uint32_t KEK_TextureHandle;
#endif

typedef struct KEK_ModelPoolSlot {
    KEK_model model;
    uint8_t used;
    uint16_t generation;
    uint32_t verts_capacity;
    uint32_t faces_capacity;
    uint32_t face_normals_capacity;
    uint32_t colors_capacity;
    uint32_t textures_capacity;
    KEK_FVec3 verts[KEK_POOL_MODEL_VERTS_MAX];
    KEK_model_face faces[KEK_POOL_MODEL_FACES_MAX];
    KEK_model_face_normal face_normals[KEK_POOL_MODEL_FACES_MAX];
    uint8_t face_colors[KEK_POOL_MODEL_COLORS_MAX];
    KEK_model_face_uv face_textures[KEK_POOL_MODEL_UVS_MAX];
} KEK_ModelPoolSlot;

typedef struct KEK_TexturePoolSlot {
    KEK_texture texture;
    uint8_t used;
    uint16_t generation;
    uint32_t pixel_capacity;
    uint8_t data[KEK_POOL_TEXTURE_PIXELS_MAX];
} KEK_TexturePoolSlot;

typedef struct KEK_ModelPool {
    KEK_ModelPoolSlot* slots;
    uint16_t capacity;
} KEK_ModelPool;

typedef struct KEK_TexturePool {
    KEK_TexturePoolSlot* slots;
    uint16_t capacity;
} KEK_TexturePool;

#ifdef __cplusplus
}
#endif

#endif // KEK_POOL_H
