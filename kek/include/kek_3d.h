#ifndef KEK_3D_H
#define KEK_3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek.h"
#include "kek_math.h"
#include "kek_texture.h"

typedef struct KEK_model KEK_model; // forward declaration

typedef struct KEK_camera {
    KEK_FVec3 position;
    KEK_FVec3 rotation;
    float fov;
    float near_plane;
    float far_plane;
} KEK_camera;

extern KEK_camera KEK_DEFAULT_CAMERA;

typedef struct KEK_3D_ProjectedVertex {
    KEK_IVec2 screen;
    float depth;
    float inv_z;
    float u_over_z;
    float v_over_z;
} KEK_3D_ProjectedVertex;

KEK_FVec3 kek_3d_rotate(KEK_FVec3 p, KEK_FVec3 rotate);
KEK_FVec3 kek_3d_translate(KEK_FVec3 p, KEK_FVec3 delta);
KEK_FVec3 kek_3d_world_to_view(KEK_FVec3 p, KEK_camera* camera);
char kek_3d_is_in_front(KEK_FVec3 p, KEK_camera camera);
char kek_3d_is_in_depth_range(KEK_FVec3 p, KEK_camera* camera);
KEK_FVec2 kek_3d_project(KEK_FVec3 p);
KEK_FVec2 kek_3d_project_camera(KEK_FVec3 p, KEK_camera* camera, float aspect_ratio);
char kek_3d_project_vertex(KEK_engine* engine, KEK_camera* camera, KEK_FVec3 p, KEK_3D_ProjectedVertex *out_vertex);
void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill);
void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture);
void kek_3d_triangle_border(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill, uint8_t color_border);
void kek_3d_blit_vertex(KEK_engine* engine, KEK_3D_ProjectedVertex vertex, uint8_t pixel);
void kek_3d_draw_model(KEK_engine* e, KEK_model* model, KEK_camera* camera, KEK_FVec3 pos, KEK_FVec3 rotation);

#ifdef __cplusplus
}
#endif

#endif // KEK_3D_H
