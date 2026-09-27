#ifndef KEK_MODEL_H
#define KEK_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_math.h"
#include "kek_texture.h"

#ifndef KEK_ENGINE_DEFINED
#define KEK_ENGINE_DEFINED
typedef struct KEK_engine KEK_engine;
#endif
#ifndef KEK_MODEL_HANDLE_DEFINED
#define KEK_MODEL_HANDLE_DEFINED
typedef uint32_t KEK_ModelHandle;
#endif
#define KEK_MODEL_HANDLE_INVALID ((KEK_ModelHandle)0u)
#ifndef KEK_MODEL_DEFINED
#define KEK_MODEL_DEFINED
typedef struct KEK_model KEK_model;
#endif

typedef struct KEK_model_face {
    uint32_t a, b, c;
} KEK_model_face;

typedef struct KEK_model_face_uv {
    KEK_FVec2 a, b, c;
} KEK_model_face_uv;

typedef struct KEK_model_face_normal {
    KEK_FVec3 a, b, c;
} KEK_model_face_normal;

struct KEK_model {
    KEK_FVec3* verts;
    KEK_model_face* faces;
    KEK_model_face_normal* face_normals;
    uint8_t* face_colors;
    KEK_model_face_uv* face_textures;
    /* A handle rather than a KEK_texture*: the pool slot behind a raw pointer
       can never be released, and a stale handle is a detectable error where a
       dangling pointer is a corrupted triangle. */
    KEK_TextureHandle texture;
    /* Set only by whoever loaded the texture for this model — kek_model_destroy
       releases it. kek_model_clone copies the handle but never the ownership,
       so destroying a clone cannot pull the texture out from under the original. */
    uint8_t owns_texture;
    uint32_t verts_count;
    uint32_t faces_count;
    uint32_t face_normals_count;
    uint32_t colors_count;
    uint32_t textures_count;
};

/* Which per-face arrays a model has beyond vertices, faces and normals. */
#define KEK_MODEL_FACE_COLORS 1u
#define KEK_MODEL_FACE_UVS 2u

/* A model with room for exactly this many vertices and faces, zeroed, its
   counts set to the sizes asked for: colors_count and textures_count are
   faces_count when the flag is there and 0, with a null array, when it is
   not. The storage comes from the engine's arena, all of it in one block, so
   the sizes have to be known up front — there is no growing a model later.
   KEK_MODEL_HANDLE_INVALID when the arena or the handle table is full. */
KEK_ModelHandle kek_model_create(KEK_engine* e, uint32_t verts_count, uint32_t faces_count, unsigned flags);
/* A model of the source's size with the source's contents. Counts past
   faces_count for normals, colours or UVs are refused. */
KEK_ModelHandle kek_model_clone(KEK_engine* e, const KEK_model* source);
KEK_model* kek_model_get(KEK_engine* e, KEK_ModelHandle handle);
void kek_model_destroy(KEK_engine* e, KEK_ModelHandle handle);
KEK_ModelHandle kek_default_cube_model_handle(KEK_engine* e);

extern KEK_model KEK_CUBE_MODEL;

#ifdef __cplusplus
}
#endif

#endif // KEK_MODEL_H
