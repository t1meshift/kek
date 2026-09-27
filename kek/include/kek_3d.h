#ifndef KEK_3D_H
#define KEK_3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek.h"
#include "kek_math.h"
#include "kek_texture.h"

#ifndef KEK_MODEL_DEFINED
#define KEK_MODEL_DEFINED
typedef struct KEK_model KEK_model;
#endif

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
    /* How far down the shading palette this vertex sits, in levels: 0 is the
       colour as it is, KEK_PALETTE_SHADING_LEVELS - 1 the darkest row. The
       rasterisers interpolate it, dither between neighbouring rows and clamp
       it to that range. kek_3d_draw_model fills it from the engine's light;
       anyone building vertices by hand sets 0 to draw unshaded. */
    float shade;
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
/* The light kek_3d_draw_model shades with — see KEK_light. direction is
   where the light travels, in world space, and is normalised here; a zero
   direction leaves only ambient. ambient is clamped to [0, 1], NaN to 0.
   Fog runs from full brightness at view depth start to the darkest level at
   end, and is off unless end > start. */
void kek_3d_set_light(KEK_engine* engine, KEK_FVec3 direction, float ambient);
void kek_3d_set_fog(KEK_engine* engine, float start, float end);
void kek_3d_draw_model(KEK_engine* e, KEK_model* model, KEK_camera* camera, KEK_FVec3 pos, KEK_FVec3 rotation);

#ifdef __cplusplus
}
#endif

#endif // KEK_3D_H
