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
    /* World orientation, radians, same Z-then-Y-then-X convention as models.
       The view applies its inverse (the transpose), then projects. */
    KEK_FVec3 rotation;
    float fov;
    float near_plane;
    float far_plane;
} KEK_camera;

extern KEK_camera KEK_DEFAULT_CAMERA;

/* The depth buffer holds 1/z times this, in a uint16_t: larger is nearer, and
   0 is the clear, which anything drawn is nearer than. 65535 is z = 0.1, the
   default camera's near plane; anything nearer than that saturates there, and
   anything past ~6,500 comes out as 1, the farthest a pixel can be. Being 1/z,
   the steps grow with the square of the distance: 0.015 of a unit apart at
   z = 10, 1.5 at z = 100. */
#define KEK_3D_DEPTH_SCALE 6553.5f

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
    /* Fog coverage, independent of shade: 0 keeps the lit pixel, 1 uses the
       engine's fog colour. It is interpolated with perspective correction
       and clamped per pixel. Direct triangle callers set this explicitly. */
    float fog;
} KEK_3D_ProjectedVertex;

KEK_FVec3 kek_3d_rotate(KEK_FVec3 p, KEK_FVec3 rotate);
KEK_FVec3 kek_3d_translate(KEK_FVec3 p, KEK_FVec3 delta);
KEK_FVec3 kek_3d_world_to_view(KEK_FVec3 p, KEK_camera* camera);
char kek_3d_is_in_front(KEK_FVec3 p, KEK_camera camera);
char kek_3d_is_in_depth_range(KEK_FVec3 p, KEK_camera* camera);
KEK_FVec2 kek_3d_project(KEK_FVec3 p);
KEK_FVec2 kek_3d_project_camera(KEK_FVec3 p, KEK_camera* camera, float aspect_ratio);
/* Returns 0 without modifying out_vertex for an invalid camera, a point
   outside the depth range, or a non-finite/out-of-range projection.
   The safe screen range is [-1048576, 1048576] on each axis. A successful
   projection sets shade and fog to zero; direct callers may then set them. */
char kek_3d_project_vertex(KEK_engine* engine, KEK_camera* camera, KEK_FVec3 p, KEK_3D_ProjectedVertex *out_vertex);
/* Direct triangle calls (including textured and border) reject screen
   coordinates outside [-1048576, 1048576] before integer edge arithmetic.
   Model drawing instead clips extreme projections to preserve visible faces. */
void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill);
void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture);
void kek_3d_triangle_border(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill, uint8_t color_border);
void kek_3d_blit_vertex(KEK_engine* engine, KEK_3D_ProjectedVertex vertex, uint8_t pixel);
/* The light kek_3d_draw_model shades with — see KEK_light. direction is
   where the light travels, in world space, and is normalised here; a zero
   direction leaves only ambient. ambient is clamped to [0, 1], NaN to 0.
   Fog replaces lit pixels with the palette index set by
   kek_3d_set_fog_color, starting at view depth start and fully covering them
   at end. Model fog is off unless end > start; direct triangle callers supply
   coverage in each vertex. The default fog colour is palette index 0. */
void kek_3d_set_light(KEK_engine* engine, KEK_FVec3 direction, float ambient);
void kek_3d_set_fog(KEK_engine* engine, float start, float end);
void kek_3d_set_fog_color(KEK_engine* engine, uint8_t color);
/* Camera parameters must be finite, 0 < fov < 180, 0 < near < far.
   Invalid cameras/transforms and non-finite transformed vertices draw nothing.
   As before, far-plane rejection drops only faces wholly beyond far. */
void kek_3d_draw_model(KEK_engine* e, KEK_model* model, KEK_camera* camera, KEK_FVec3 pos, KEK_FVec3 rotation);

#ifdef __cplusplus
}
#endif

#endif // KEK_3D_H
