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

/* A position as a model keeps it: a byte an axis, which the model's scale and
   offset take back to float. Quake's MDL did the same, four times smaller than
   float, and like Quake's models these move by up to half a step as they turn:
   0.2% of the model's size along its longest axis. */
typedef struct KEK_model_vertex {
    uint8_t x, y, z;
} KEK_model_vertex;

/* Three indices into verts. */
typedef struct KEK_model_face {
    uint16_t a, b, c;
} KEK_model_face;

/* Three indices into uvs, one per corner, in the face's order. */
typedef struct KEK_model_face_uv {
    uint16_t a, b, c;
} KEK_model_face_uv;

/* A corner without a UV. A face with one is drawn untextured, in its colour. */
#define KEK_MODEL_UV_NONE 0xFFFFu

/* No normals: flat shading derives a face's normal from its vertices at draw
   time, so a stored one would be 36 bytes per face that nothing reads.

   Every count is 16 bits, as in KMF, since a face addresses its vertices and
   UVs with 16-bit indices. The per-face arrays — face_colors and face_uvs —
   are either absent or faces_count long; colors_count and face_uvs_count say
   how many of their entries a model built by hand has filled. */
struct KEK_model {
    KEK_model_vertex* verts;
    /* Vertex i is at offset + scale * verts[i], axis by axis. */
    KEK_FVec3 scale;
    KEK_FVec3 offset;
    KEK_model_face* faces;
    uint8_t* face_colors;
    /* The distinct UVs, shared between faces as the file shares them. */
    KEK_FVec2* uvs;
    KEK_model_face_uv* face_uvs;
    /* A handle rather than a KEK_texture*: the pool slot behind a raw pointer
       can never be released, and a stale handle is a detectable error where a
       dangling pointer is a corrupted triangle. */
    KEK_TextureHandle texture;
    /* Set only by whoever loaded the texture for this model — kek_model_destroy
       releases it. kek_model_clone copies the handle but never the ownership,
       so destroying a clone cannot pull the texture out from under the original. */
    uint8_t owns_texture;
    uint16_t verts_count;
    uint16_t faces_count;
    uint16_t colors_count;
    uint16_t uvs_count;
    uint16_t face_uvs_count;
};

/* Which per-face arrays a model has beyond vertices and faces. */
#define KEK_MODEL_FACE_COLORS 1u
#define KEK_MODEL_FACE_UVS 2u

/* A model with room for exactly this many vertices, faces and, under
   KEK_MODEL_FACE_UVS, UVs, zeroed, its counts set to the sizes asked for:
   colors_count and face_uvs_count are faces_count when the flag is there and
   0, with a null array, when it is not; uvs_count is 0 without the flag. The
   storage comes from the engine's arena, all of it in one block, so the sizes
   have to be known up front — there is no growing a model later.
   KEK_MODEL_HANDLE_INVALID when the arena or the handle table is full. */
KEK_ModelHandle kek_model_create(KEK_engine* e, uint16_t verts_count, uint16_t faces_count, uint16_t uvs_count,
                                 unsigned flags);
/* A model of the source's size with the source's contents. Counts past
   faces_count for colours or face UVs are refused. */
KEK_ModelHandle kek_model_clone(KEK_engine* e, const KEK_model* source);
/* Sets the model's scale and offset to the box around positions, and its
   verts_count vertices to the nearest step inside it. positions has
   verts_count entries. A box with no depth along an axis gets a scale of 0
   there, and a position that is not a number comes out at the box's corner. */
void kek_model_quantise(KEK_model* model, const KEK_FVec3* positions);
/* Vertex index where the model puts it, back in float. */
KEK_FVec3 kek_model_position(const KEK_model* model, uint16_t index);
KEK_model* kek_model_get(KEK_engine* e, KEK_ModelHandle handle);
void kek_model_destroy(KEK_engine* e, KEK_ModelHandle handle);
KEK_ModelHandle kek_default_cube_model_handle(KEK_engine* e);

extern KEK_model KEK_CUBE_MODEL;

#ifdef __cplusplus
}
#endif

#endif // KEK_MODEL_H
