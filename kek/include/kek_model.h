#ifndef KEK_MODEL_H
#define KEK_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_math.h"
#include "kek_texture.h"

typedef struct KEK_engine KEK_engine;
#ifndef KEK_MODEL_HANDLE_DEFINED
#define KEK_MODEL_HANDLE_DEFINED
typedef uint32_t KEK_ModelHandle;
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

typedef struct KEK_model {
    KEK_FVec3* verts;
    KEK_model_face* faces;
    KEK_model_face_normal* face_normals;
    uint8_t* face_colors;
    KEK_texture* texture;
    KEK_model_face_uv* face_textures;
    uint32_t verts_count;
    uint32_t faces_count;
    uint32_t face_normals_count;
    uint32_t colors_count;
    uint32_t textures_count;
} KEK_model;

KEK_ModelHandle kek_model_create(KEK_engine* e);
KEK_ModelHandle kek_model_clone(KEK_engine* e, const KEK_model* source);
KEK_model* kek_model_get(KEK_engine* e, KEK_ModelHandle handle);
void kek_model_destroy(KEK_engine* e, KEK_ModelHandle handle);
KEK_ModelHandle kek_default_cube_model_handle(KEK_engine* e);

extern KEK_model KEK_CUBE_MODEL;

#ifdef __cplusplus
}
#endif

#endif // KEK_MODEL_H
