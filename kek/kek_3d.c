#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "kek.h"
#include "kek_3d.h"
#include "kek_2d.h"
#include "kek_config.h"
#include "kek_math.h"
#include "kek_model.h"
#include "kek_internal.h"

#define KEK_PI 3.14159265358979323846f
#define KEK_EPSILON 0.000001f
#define KEK_IS_NAN(value) ((value) != (value))

KEK_camera KEK_DEFAULT_CAMERA = {
    .position = {0.f, 0.f, 0.f},
    .rotation = {0.f, 0.f, 0.f},
    .fov = 75.f,
    .near_plane = 0.1f,
    .far_plane = 1000.f
};

static char kek_3d_depth_test(KEK_engine* engine, uint16_t x, uint16_t y, float depth) {
    uint32_t index = (uint32_t)y * (uint32_t)engine->w + (uint32_t)x;

    if (depth <= engine->db[index]) {
        return 0;
    }

    engine->db[index] = depth;
    return 1;
}

/* Shades are carried in sixteenths of a level from here on: the whole part
   picks a row of the shading palette, the fraction is how often the next,
   darker row is used instead. */
#define KEK_3D_SHADE_ONE 16
#define KEK_3D_SHADE_MAX ((KEK_PALETTE_SHADING_LEVELS - 1) * KEK_3D_SHADE_ONE)

/* Ordered dither thresholds, 0..15. A fraction f of a level puts the darker
   row on exactly f of every 16 pixels in each 4x4 tile, spread as evenly as
   a tile allows, so four rows read as 49 steps and a slow gradient does not
   band. */
static const uint8_t KEK_3D_BAYER4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

/* NaN and anything below zero come out unshaded rather than reaching the
   int conversion. */
static int kek_3d_shade_fixed(float shade) {
    int result;

    if (!(shade > 0.f)) {
        return 0;
    }
    if (shade >= (float)(KEK_PALETTE_SHADING_LEVELS - 1)) {
        return KEK_3D_SHADE_MAX;
    }
    result = (int)(shade * (float)KEK_3D_SHADE_ONE);
    return result > KEK_3D_SHADE_MAX ? KEK_3D_SHADE_MAX : result;
}

/* The shading palette row for one pixel. Never past the last row: a nonzero
   fraction means the whole part is at most LEVELS - 2, since the shade is
   clamped to KEK_3D_SHADE_MAX, which has none. */
static const uint8_t* kek_3d_shade_row(const KEK_engine* engine, int shade_fixed, int x, int y) {
    int level = shade_fixed / KEK_3D_SHADE_ONE +
        (shade_fixed % KEK_3D_SHADE_ONE > KEK_3D_BAYER4[y & 3][x & 3]);
    return engine->shading_palette + (size_t)256 * (size_t)level;
}

/* One conversion for the whole triangle when its vertices agree, which they
   do whenever there is no fog: the face's light is one value. */
static int kek_3d_shade_uniform(const KEK_3D_ProjectedVertex vertices[3], int* out_shade_fixed) {
    int s0 = kek_3d_shade_fixed(vertices[0].shade);

    *out_shade_fixed = s0;
    return s0 == kek_3d_shade_fixed(vertices[1].shade) && s0 == kek_3d_shade_fixed(vertices[2].shade);
}

static void kek_3d_blit_depth(KEK_engine* engine, uint16_t x, uint16_t y, float depth, uint8_t pixel) {
    if (kek_3d_depth_test(engine, x, y, depth)) {
        kek_blit(engine, x, y, pixel);
    }
}

static float kek_3d_edge_function(KEK_IVec2 a, KEK_IVec2 b, int x, int y) {
    return (float)(x - a.x) * (float)(b.y - a.y) - (float)(y - a.y) * (float)(b.x - a.x);
}

KEK_FVec3 kek_3d_rotate(KEK_FVec3 p, KEK_FVec3 r) {

    float d1 = p.y * sinf(r.z) + p.x * cosf(r.z);
    float d2 = p.y * cosf(r.z) - p.x * sinf(r.z);
    float d3 = p.z * cosf(r.y) + d1 * sinf(r.y);

    return (KEK_FVec3) {
        .x = cosf(r.y) * d1 - p.z * sinf(r.y),
        .y = sinf(r.x) * d3 + d2 * cosf(r.x),
        .z = cosf(r.x) * d3 - d2 * sinf(r.x)
    };
}

KEK_FVec3 kek_3d_translate(KEK_FVec3 p, KEK_FVec3 delta) {
    return (KEK_FVec3) {
        .x = p.x + delta.x,
        .y = p.y + delta.y,
        .z = p.z + delta.z
    };
}

KEK_FVec3 kek_3d_world_to_view(KEK_FVec3 p, KEK_camera* camera) {
    KEK_FVec3 translated = {
        .x = p.x - camera->position.x,
        .y = p.y - camera->position.y,
        .z = p.z - camera->position.z
    };

    return kek_3d_rotate(translated, (KEK_FVec3) {
        .x = -camera->rotation.x,
        .y = -camera->rotation.y,
        .z = -camera->rotation.z
    });
}

char kek_3d_is_in_front(KEK_FVec3 p, KEK_camera camera) {
    return p.z > camera.near_plane;
}

char kek_3d_is_in_depth_range(KEK_FVec3 p, KEK_camera* camera) {
    return p.z > camera->near_plane && p.z < camera->far_plane;
}

KEK_FVec2 kek_3d_project(KEK_FVec3 p) {
    return (KEK_FVec2) { p.x / p.z, p.y / p.z };
}

KEK_FVec2 kek_3d_project_camera(KEK_FVec3 p, KEK_camera* camera, float aspect_ratio) {
    float focal_length = 1.f / tanf(camera->fov * KEK_PI / 360.f);

    return (KEK_FVec2) {
        .x = (p.x * focal_length) / (p.z * aspect_ratio),
        .y = (p.y * focal_length) / p.z
    };
}

char kek_3d_project_vertex(KEK_engine* engine, KEK_camera* camera, KEK_FVec3 p, KEK_3D_ProjectedVertex *out_vertex) {
    KEK_FVec3 view = kek_3d_world_to_view(p, camera);
    float aspect_ratio = (float)engine->w / (float)engine->h;

    if (!kek_3d_is_in_depth_range(view, camera)) {
        return 0;
    }

    out_vertex->screen = kek_2d_to_screen(engine, kek_3d_project_camera(view, camera, aspect_ratio));
    out_vertex->depth = view.z;
    out_vertex->inv_z = 1.f / view.z;
    out_vertex->u_over_z = 0.f;
    out_vertex->v_over_z = 0.f;
    out_vertex->shade = 0.f;
    return 1;
}

void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill) {
    int min_x = engine->w - 1;
    int min_y = engine->h - 1;
    int max_x = 0;
    int max_y = 0;
    KEK_IVec2 screen_vertices[3];
    float area;
    int shade_fixed;
    int shade_uniform = kek_3d_shade_uniform(vertices, &shade_fixed);

    for (int i = 0; i < 3; ++i) {
        screen_vertices[i] = vertices[i].screen;
        if (screen_vertices[i].x < min_x) min_x = screen_vertices[i].x;
        if (screen_vertices[i].y < min_y) min_y = screen_vertices[i].y;
        if (screen_vertices[i].x > max_x) max_x = screen_vertices[i].x;
        if (screen_vertices[i].y > max_y) max_y = screen_vertices[i].y;
    }

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= engine->w) max_x = engine->w - 1;
    if (max_y >= engine->h) max_y = engine->h - 1;

    area = kek_3d_edge_function(screen_vertices[0], screen_vertices[1], screen_vertices[2].x, screen_vertices[2].y);
    if (area == 0.f) {
        return;
    }

    /* Precompute per-edge step increments: d(edge)/dx and d(edge)/dy are constants */
    {
        float step_w0_x = (float)(screen_vertices[2].y - screen_vertices[1].y);
        float step_w0_y = (float)(screen_vertices[1].x - screen_vertices[2].x);
        float step_w1_x = (float)(screen_vertices[0].y - screen_vertices[2].y);
        float step_w1_y = (float)(screen_vertices[2].x - screen_vertices[0].x);
        float step_w2_x = (float)(screen_vertices[1].y - screen_vertices[0].y);
        float step_w2_y = (float)(screen_vertices[0].x - screen_vertices[1].x);
        float row_w0 = kek_3d_edge_function(screen_vertices[1], screen_vertices[2], min_x, min_y);
        float row_w1 = kek_3d_edge_function(screen_vertices[2], screen_vertices[0], min_x, min_y);
        float row_w2 = kek_3d_edge_function(screen_vertices[0], screen_vertices[1], min_x, min_y);

        for (int y = min_y; y <= max_y; ++y) {
            float w0 = row_w0, w1 = row_w1, w2 = row_w2;
            for (int x = min_x; x <= max_x; ++x) {
                if (!((w0 < 0.f || w1 < 0.f || w2 < 0.f) &&
                      (w0 > 0.f || w1 > 0.f || w2 > 0.f))) {
                    float nw0 = w0 / area, nw1 = w1 / area, nw2 = w2 / area;
                    float inv_z = nw0 * vertices[0].inv_z + nw1 * vertices[1].inv_z + nw2 * vertices[2].inv_z;
                    if (kek_3d_depth_test(engine, (uint16_t)x, (uint16_t)y, inv_z)) {
                        int shade = shade_uniform ? shade_fixed : kek_3d_shade_fixed(
                            nw0 * vertices[0].shade + nw1 * vertices[1].shade + nw2 * vertices[2].shade);
                        kek_blit(engine, (uint16_t)x, (uint16_t)y, kek_3d_shade_row(engine, shade, x, y)[color_fill]);
                    }
                }
                w0 += step_w0_x; w1 += step_w1_x; w2 += step_w2_x;
            }
            row_w0 += step_w0_y; row_w1 += step_w1_y; row_w2 += step_w2_y;
        }
    }
}

void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    int min_x = engine->w - 1;
    int min_y = engine->h - 1;
    int max_x = 0;
    int max_y = 0;
    KEK_IVec2 screen_vertices[3];
    float area;
    int shade_fixed;
    int shade_uniform = kek_3d_shade_uniform(vertices, &shade_fixed);

    for (int i = 0; i < 3; ++i) {
        screen_vertices[i] = vertices[i].screen;
        if (screen_vertices[i].x < min_x) min_x = screen_vertices[i].x;
        if (screen_vertices[i].y < min_y) min_y = screen_vertices[i].y;
        if (screen_vertices[i].x > max_x) max_x = screen_vertices[i].x;
        if (screen_vertices[i].y > max_y) max_y = screen_vertices[i].y;
    }

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= engine->w) max_x = engine->w - 1;
    if (max_y >= engine->h) max_y = engine->h - 1;

    area = kek_3d_edge_function(screen_vertices[0], screen_vertices[1], screen_vertices[2].x, screen_vertices[2].y);
    if (area == 0.f) {
        return;
    }

    {
        float step_w0_x = (float)(screen_vertices[2].y - screen_vertices[1].y);
        float step_w0_y = (float)(screen_vertices[1].x - screen_vertices[2].x);
        float step_w1_x = (float)(screen_vertices[0].y - screen_vertices[2].y);
        float step_w1_y = (float)(screen_vertices[2].x - screen_vertices[0].x);
        float step_w2_x = (float)(screen_vertices[1].y - screen_vertices[0].y);
        float step_w2_y = (float)(screen_vertices[0].x - screen_vertices[1].x);
        float row_w0 = kek_3d_edge_function(screen_vertices[1], screen_vertices[2], min_x, min_y);
        float row_w1 = kek_3d_edge_function(screen_vertices[2], screen_vertices[0], min_x, min_y);
        float row_w2 = kek_3d_edge_function(screen_vertices[0], screen_vertices[1], min_x, min_y);

        for (int y = min_y; y <= max_y; ++y) {
            float w0 = row_w0, w1 = row_w1, w2 = row_w2;
            for (int x = min_x; x <= max_x; ++x) {
                if (!((w0 < 0.f || w1 < 0.f || w2 < 0.f) &&
                      (w0 > 0.f || w1 > 0.f || w2 > 0.f))) {
                    float nw0 = w0 / area, nw1 = w1 / area, nw2 = w2 / area;
                    float inv_z = nw0 * vertices[0].inv_z + nw1 * vertices[1].inv_z + nw2 * vertices[2].inv_z;
                    /* Depth first: a hidden pixel costs neither the two
                       divides of the sample nor the shade. */
                    if (fabsf(inv_z) >= KEK_EPSILON &&
                        kek_3d_depth_test(engine, (uint16_t)x, (uint16_t)y, inv_z)) {
                        float u_over_z = nw0 * vertices[0].u_over_z + nw1 * vertices[1].u_over_z + nw2 * vertices[2].u_over_z;
                        float v_over_z = nw0 * vertices[0].v_over_z + nw1 * vertices[1].v_over_z + nw2 * vertices[2].v_over_z;
                        uint8_t texel = kek_texture_sample(engine, texture, u_over_z / inv_z, v_over_z / inv_z);
                        int shade = shade_uniform ? shade_fixed : kek_3d_shade_fixed(
                            nw0 * vertices[0].shade + nw1 * vertices[1].shade + nw2 * vertices[2].shade);
                        kek_blit(engine, (uint16_t)x, (uint16_t)y, kek_3d_shade_row(engine, shade, x, y)[texel]);
                    }
                }
                w0 += step_w0_x; w1 += step_w1_x; w2 += step_w2_x;
            }
            row_w0 += step_w0_y; row_w1 += step_w1_y; row_w2 += step_w2_y;
        }
    }
}

void kek_3d_triangle_border(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill, uint8_t color_border) {
    KEK_IVec2 screen_vertices[3];

    kek_3d_triangle(engine, vertices, color_fill);
    for (int i = 0; i < 3; ++i) {
        screen_vertices[i] = vertices[i].screen;
    }
    for (int i = 0; i < 3; ++i) {
        kek_2d_line(engine, screen_vertices[i], screen_vertices[(i + 1) % 3], color_border);
    }
}

void kek_3d_blit_vertex(KEK_engine* engine, KEK_3D_ProjectedVertex vertex, uint8_t pixel) {
    if (vertex.screen.x < 0 || vertex.screen.x >= engine->w ||
        vertex.screen.y < 0 || vertex.screen.y >= engine->h) {
        return;
    }

    kek_3d_blit_depth(engine, (uint16_t)vertex.screen.x, (uint16_t)vertex.screen.y, vertex.inv_z, pixel);
}

/* Clip a triangle in view space against z = near_plane (Sutherland-Hodgman).
 * Returns the number of output vertices: 0 (all clipped), 3, or 4.
 *
 * Four is arithmetic, not a guess: a convex polygon crosses a plane at most
 * twice, so the worst case is two kept vertices plus two intersections. But
 * the loop writes up to twice per edge and nothing in it enforces that bound,
 * while out_v is a four-element array on the caller's stack — so the bound is
 * checked rather than assumed. */
#define KEK_3D_CLIP_NEAR_MAX_VERTS 4

static int kek_3d_clip_near(
    KEK_FVec3 v[3], KEK_FVec2 uv[3], float near,
    KEK_FVec3 out_v[KEK_3D_CLIP_NEAR_MAX_VERTS],
    KEK_FVec2 out_uv[KEK_3D_CLIP_NEAR_MAX_VERTS])
{
    int n = 0;
    int i;
    for (i = 0; i < 3 && n < KEK_3D_CLIP_NEAR_MAX_VERTS; ++i) {
        KEK_FVec3 a = v[i], b = v[(i + 1) % 3];
        KEK_FVec2 ua = uv[i], ub = uv[(i + 1) % 3];
        int a_in = a.z > near, b_in = b.z > near;
        if (a_in) {
            out_v[n] = a;
            out_uv[n++] = ua;
        }
        if (a_in != b_in && n < KEK_3D_CLIP_NEAR_MAX_VERTS) {
            float t = (near - a.z) / (b.z - a.z);
            out_v[n].x  = a.x  + t * (b.x  - a.x);
            out_v[n].y  = a.y  + t * (b.y  - a.y);
            out_v[n].z  = near;
            out_uv[n].x = ua.x + t * (ub.x - ua.x);
            out_uv[n].y = ua.y + t * (ub.y - ua.y);
            ++n;
        }
    }
    return n;
}

void kek_3d_set_light(KEK_engine* engine, KEK_FVec3 direction, float ambient) {
    engine->light.direction = kek_normalize_fvec3_copy(direction);
    engine->light.ambient = ambient > 0.f ? (ambient < 1.f ? ambient : 1.f) : 0.f;
}

void kek_3d_set_fog(KEK_engine* engine, float start, float end) {
    engine->light.fog_start = start;
    engine->light.fog_end = end;
}

/* The face's own shade in levels, from a normal derived from the face rather
   than read from the model: every model has vertices, not every model has
   normals (a hand-built one may leave face_normals null), and the ones it
   has are per corner, which flat shading has no use for.

   Both vectors are in view space. The camera turns the light and the face
   alike, so this is the same dot product as in the world. cross(b - a, c - a)
   points out of the face — the winding kek_file_model_calculate_face_normal
   assumes, and the one the backface cull keeps. */
static float kek_3d_face_shade(const KEK_FVec3 v[3], KEK_FVec3 light_view, float ambient) {
    float abx = v[1].x - v[0].x, aby = v[1].y - v[0].y, abz = v[1].z - v[0].z;
    float acx = v[2].x - v[0].x, acy = v[2].y - v[0].y, acz = v[2].z - v[0].z;
    float nx = aby * acz - abz * acy;
    float ny = abz * acx - abx * acz;
    float nz = abx * acy - aby * acx;
    float len_sq = nx * nx + ny * ny + nz * nz;
    float lambert;

    /* A degenerate face has no side to light; the cull drops it anyway. */
    if (!(len_sq > 0.f)) {
        return 0.f;
    }

    lambert = -(nx * light_view.x + ny * light_view.y + nz * light_view.z) / sqrtf(len_sq);
    if (!(lambert > 0.f)) {
        lambert = 0.f;
    }

    /* 1 - (ambient + (1 - ambient) * lambert), in levels. */
    return (1.f - ambient) * (1.f - lambert) * (float)(KEK_PALETTE_SHADING_LEVELS - 1);
}

/* The face's shade plus fog at one vertex's view depth. Per vertex, after the
   near clip, so a face that runs into the distance darkens along its length;
   the rasteriser interpolates it in screen space, without the perspective
   correction the UVs get — at four levels and a dither nobody sees the
   difference. */
static float kek_3d_vertex_shade(const KEK_light* light, float face_shade, float z) {
    float shade = face_shade;

    if (light->fog_end > light->fog_start) {
        float fog = (z - light->fog_start) / (light->fog_end - light->fog_start);
        if (fog > 0.f) {
            shade += (fog < 1.f ? fog : 1.f) * (float)(KEK_PALETTE_SHADING_LEVELS - 1);
        }
    }

    return shade;
}

void kek_3d_draw_model(KEK_engine *e, KEK_model *mdl, KEK_camera *camera, KEK_FVec3 pos, KEK_FVec3 rotation) {
    /* Fix 1: static buffers — avoids ~25 KB of stack allocation per draw call */
    static KEK_FVec3 view_verts[KEK_POOL_MODEL_VERTS_MAX];
    char use_colors = mdl->face_colors != 0 && mdl->colors_count > 0;
    /* Resolved once per model, not once per face: a stale handle just means
       the model draws untextured. */
    KEK_texture* texture = kek_texture_get(e, mdl->texture);
    /* Fix 4: precompute rotation matrices and projection constants once per call */
    KEK_Mat3 model_rot, camera_rot;
    KEK_FVec3 light_view;
    float aspect_ratio, focal_length, half_w, half_h;
    uint32_t i;

    if (mdl->verts_count > KEK_POOL_MODEL_VERTS_MAX) {
        return;
    }

    model_rot    = kek_mat3_from_euler(rotation);
    camera_rot   = kek_mat3_from_euler((KEK_FVec3){
                       -camera->rotation.x,
                       -camera->rotation.y,
                       -camera->rotation.z });
    light_view   = kek_mat3_apply(camera_rot, e->light.direction);
    aspect_ratio = (float)e->w / (float)e->h;
    focal_length = 1.f / tanf(camera->fov * KEK_PI / 360.f);
    half_w       = (float)e->w * 0.5f;
    half_h       = (float)e->h * 0.5f;

    /* Transform all vertices to view space using precomputed matrices */
    for (i = 0; i < mdl->verts_count; ++i) {
        KEK_FVec3 world = kek_3d_translate(kek_mat3_apply(model_rot, mdl->verts[i]), pos);
        view_verts[i] = kek_mat3_apply(camera_rot, (KEK_FVec3){
            world.x - camera->position.x,
            world.y - camera->position.y,
            world.z - camera->position.z });
    }

    for (i = 0; i < mdl->faces_count; ++i) {
        KEK_model_face face = mdl->faces[i];
        uint8_t color = use_colors && i < mdl->colors_count ? mdl->face_colors[i] : 15;
        KEK_model_face_uv face_uv;
        char face_is_textured;
        KEK_FVec3 fv[3];
        KEK_FVec2 fuv[3];
        KEK_FVec3 cv[KEK_3D_CLIP_NEAR_MAX_VERTS];
        KEK_FVec2 cuv[KEK_3D_CLIP_NEAR_MAX_VERTS];
        float face_shade;
        int cn, tri_count, t;

        face_uv = i < mdl->textures_count ? mdl->face_textures[i] : (KEK_model_face_uv) {
            .a = {NAN, NAN},
            .b = {NAN, NAN},
            .c = {NAN, NAN}
        };
        face_is_textured = texture != 0 &&
            mdl->face_textures != 0 &&
            i < mdl->textures_count &&
            !KEK_IS_NAN(face_uv.a.x) && !KEK_IS_NAN(face_uv.a.y) &&
            !KEK_IS_NAN(face_uv.b.x) && !KEK_IS_NAN(face_uv.b.y) &&
            !KEK_IS_NAN(face_uv.c.x) && !KEK_IS_NAN(face_uv.c.y);

        fv[0] = view_verts[face.a];
        fv[1] = view_verts[face.b];
        fv[2] = view_verts[face.c];

        /* Far-plane cull: skip entirely if all vertices are beyond far plane */
        if (fv[0].z >= camera->far_plane &&
            fv[1].z >= camera->far_plane &&
            fv[2].z >= camera->far_plane) {
            continue;
        }

        face_shade = kek_3d_face_shade(fv, light_view, e->light.ambient);

        if (face_is_textured) {
            fuv[0] = face_uv.a; fuv[1] = face_uv.b; fuv[2] = face_uv.c;
        } else {
            fuv[0] = fuv[1] = fuv[2] = (KEK_FVec2){0.f, 0.f};
        }

        /* Fix 3: clip triangle against near plane in view space */
        cn = kek_3d_clip_near(fv, fuv, camera->near_plane, cv, cuv);
        if (cn < 3) continue;

        /* Rasterize as a triangle fan (1 tri for cn==3, 2 tris for cn==4) */
        tri_count = cn - 2;
        for (t = 0; t < tri_count; ++t) {
            int idxs[3];
            KEK_3D_ProjectedVertex pv[3];
            KEK_IVec2 sv[3];
            int k;

            idxs[0] = 0; idxs[1] = t + 1; idxs[2] = t + 2;

            /* Project each clipped vertex */
            for (k = 0; k < 3; ++k) {
                int vi = idxs[k];
                float inv_z = 1.f / cv[vi].z;
                pv[k].screen.x = (int)((cv[vi].x * focal_length * inv_z / aspect_ratio + 1.f) * half_w);
                pv[k].screen.y = (int)((1.f - cv[vi].y * focal_length * inv_z) * half_h);
                pv[k].depth    = cv[vi].z;
                pv[k].inv_z    = inv_z;
                pv[k].u_over_z = face_is_textured ? cuv[vi].x * inv_z : 0.f;
                pv[k].v_over_z = face_is_textured ? cuv[vi].y * inv_z : 0.f;
                pv[k].shade    = kek_3d_vertex_shade(&e->light, face_shade, cv[vi].z);
                sv[k]          = pv[k].screen;
            }

            /* Fix 2: lateral frustum cull — skip if all verts outside same screen edge */
            if ((sv[0].x < 0 && sv[1].x < 0 && sv[2].x < 0) ||
                (sv[0].x >= e->w && sv[1].x >= e->w && sv[2].x >= e->w) ||
                (sv[0].y < 0 && sv[1].y < 0 && sv[2].y < 0) ||
                (sv[0].y >= e->h && sv[1].y >= e->h && sv[2].y >= e->h)) {
                continue;
            }

            /* Backface cull */
            if (kek_area_triangle_signed(sv) < 1) {
                continue;
            }

            if (face_is_textured) {
                kek_3d_triangle_textured(e, pv, texture);
            } else {
                kek_3d_triangle(e, pv, color);
            }
        }
    }
}
