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

/* What both rasterisers need of a triangle before its first pixel: the
   bounding box clipped to the frame, and the three edge functions — their
   values at the box's top-left corner and how much they change per pixel.
   Edge i is the one opposite vertex i, so w[i] / area is vertex i's weight. */
typedef struct KEK_3D_Setup_ {
    int min_x, min_y, max_x, max_y;
    float w[3];
    float step_x[3];
    float step_y[3];
    /* The one divide a triangle costs; every weight is a multiply by it. */
    float inv_area;
} KEK_3D_Setup_;

/* An attribute across the triangle: its value at the box's top-left corner
   and its change per pixel along x and y. Every attribute the rasterisers
   interpolate — 1/z, u/z, v/z, the shade — is linear in screen space, so
   this is the vertices weighted by the edge functions, worked out once per
   triangle instead of once per pixel. */
typedef struct KEK_3D_Plane_ {
    float origin, dx, dy;
} KEK_3D_Plane_;

static int kek_3d_setup(const KEK_engine* engine, const KEK_3D_ProjectedVertex vertices[3], KEK_3D_Setup_* s) {
    KEK_IVec2 v0 = vertices[0].screen, v1 = vertices[1].screen, v2 = vertices[2].screen;
    float area = kek_3d_edge_function(v0, v1, v2.x, v2.y);

    if (area == 0.f) {
        return 0;
    }

    s->min_x = KEK_MAX(KEK_MIN(KEK_MIN(v0.x, v1.x), v2.x), 0);
    s->min_y = KEK_MAX(KEK_MIN(KEK_MIN(v0.y, v1.y), v2.y), 0);
    s->max_x = KEK_MIN(KEK_MAX(KEK_MAX(v0.x, v1.x), v2.x), engine->w - 1);
    s->max_y = KEK_MIN(KEK_MAX(KEK_MAX(v0.y, v1.y), v2.y), engine->h - 1);

    s->step_x[0] = (float)(v2.y - v1.y);
    s->step_y[0] = (float)(v1.x - v2.x);
    s->step_x[1] = (float)(v0.y - v2.y);
    s->step_y[1] = (float)(v2.x - v0.x);
    s->step_x[2] = (float)(v1.y - v0.y);
    s->step_y[2] = (float)(v0.x - v1.x);
    s->w[0] = kek_3d_edge_function(v1, v2, s->min_x, s->min_y);
    s->w[1] = kek_3d_edge_function(v2, v0, s->min_x, s->min_y);
    s->w[2] = kek_3d_edge_function(v0, v1, s->min_x, s->min_y);
    s->inv_area = 1.f / area;
    return 1;
}

static KEK_3D_Plane_ kek_3d_plane(const KEK_3D_Setup_* s, float a0, float a1, float a2) {
    KEK_3D_Plane_ p;

    p.origin = (s->w[0] * a0 + s->w[1] * a1 + s->w[2] * a2) * s->inv_area;
    p.dx = (s->step_x[0] * a0 + s->step_x[1] * a1 + s->step_x[2] * a2) * s->inv_area;
    p.dy = (s->step_y[0] * a0 + s->step_y[1] * a1 + s->step_y[2] * a2) * s->inv_area;
    return p;
}

/* Its value at (x, y), for the start of a row or a span. Per pixel along a
   row it is stepped by dx instead. */
static float kek_3d_plane_at(KEK_3D_Plane_ p, const KEK_3D_Setup_* s, int x, int y) {
    return p.origin + p.dx * (float)(x - s->min_x) + p.dy * (float)(y - s->min_y);
}

/* Inside, or on an edge, in either winding. */
static int kek_3d_covered(float w0, float w1, float w2) {
    return !((w0 < 0.f || w1 < 0.f || w2 < 0.f) && (w0 > 0.f || w1 > 0.f || w2 > 0.f));
}

void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill) {
    KEK_3D_Setup_ s;
    KEK_3D_Plane_ depth, shade;
    int shade_fixed;
    int shade_uniform = kek_3d_shade_uniform(vertices, &shade_fixed);
    float row_w0, row_w1, row_w2;

    if (!kek_3d_setup(engine, vertices, &s)) {
        return;
    }
    depth = kek_3d_plane(&s, vertices[0].inv_z, vertices[1].inv_z, vertices[2].inv_z);
    shade = kek_3d_plane(&s, vertices[0].shade, vertices[1].shade, vertices[2].shade);
    row_w0 = s.w[0];
    row_w1 = s.w[1];
    row_w2 = s.w[2];

    for (int y = s.min_y; y <= s.max_y; ++y) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        float inv_z = kek_3d_plane_at(depth, &s, s.min_x, y);
        float shade_here = kek_3d_plane_at(shade, &s, s.min_x, y);
        for (int x = s.min_x; x <= s.max_x; ++x) {
            if (kek_3d_covered(w0, w1, w2) &&
                kek_3d_depth_test(engine, (uint16_t)x, (uint16_t)y, inv_z)) {
                int level = shade_uniform ? shade_fixed : kek_3d_shade_fixed(shade_here);
                kek_blit(engine, (uint16_t)x, (uint16_t)y, kek_3d_shade_row(engine, level, x, y)[color_fill]);
            }
            w0 += s.step_x[0]; w1 += s.step_x[1]; w2 += s.step_x[2];
            inv_z += depth.dx;
            shade_here += shade.dx;
        }
        row_w0 += s.step_y[0]; row_w1 += s.step_y[1]; row_w2 += s.step_y[2];
    }
}

/* Perspective-correct u and v are divided out every KEK_3D_SPAN pixels and
   stepped linearly in between, which is how Quake hid one divide behind
   sixteen pixels. Within a span the texture is affine: at this resolution,
   and with a span this short, the error is a fraction of a texel. */
#define KEK_3D_SPAN 16

/* 1/n for the last, shorter span of a row, where the step is over n pixels
   rather than KEK_3D_SPAN: a table, so that span costs no divide either. */
static const float KEK_3D_RECIPROCAL[KEK_3D_SPAN] = {
    0.f, 1.f / 1.f, 1.f / 2.f, 1.f / 3.f, 1.f / 4.f, 1.f / 5.f, 1.f / 6.f, 1.f / 7.f,
    1.f / 8.f, 1.f / 9.f, 1.f / 10.f, 1.f / 11.f, 1.f / 12.f, 1.f / 13.f, 1.f / 14.f, 1.f / 15.f
};

typedef struct KEK_3D_TexturePlanes_ {
    KEK_3D_Plane_ inv_z, u_over_z, v_over_z, shade;
    int shade_fixed;
    int shade_uniform;
} KEK_3D_TexturePlanes_;

/* u and v at (x, y): the one divide per span. 1/z is positive wherever the
   triangle is, since every vertex is past the near plane; the guard is for a
   value that rounding has brought to zero, which samples the corner rather
   than an infinity. */
static void kek_3d_perspective(const KEK_3D_TexturePlanes_* p, const KEK_3D_Setup_* s, int x, int y,
                               float* out_u, float* out_v) {
    float inv_z = kek_3d_plane_at(p->inv_z, s, x, y);
    float z = fabsf(inv_z) >= KEK_EPSILON ? 1.f / inv_z : 0.f;

    *out_u = kek_3d_plane_at(p->u_over_z, s, x, y) * z;
    *out_v = kek_3d_plane_at(p->v_over_z, s, x, y) * z;
}

/* One row's covered pixels, first to last, with the edge functions as they
   stood at the first. The ends of each span are always inside the row's
   covered stretch, so the divide never sees 1/z extrapolated past the
   triangle, where it can reach zero. */
static void kek_3d_textured_row(KEK_engine* engine, const KEK_texture* texture, const KEK_3D_Setup_* s,
                                const KEK_3D_TexturePlanes_* p, int y, int first, int last,
                                float w0, float w1, float w2) {
    float inv_z = kek_3d_plane_at(p->inv_z, s, first, y);
    float shade = kek_3d_plane_at(p->shade, s, first, y);
    float u, v;
    int x = first;

    kek_3d_perspective(p, s, x, y, &u, &v);
    while (x <= last) {
        int count = last - x + 1;
        float u_end, v_end, du, dv, step;

        /* A full span ends on the first pixel of the next, which is where
           that one starts; the last ends on the row's last pixel. */
        if (count > KEK_3D_SPAN) {
            count = KEK_3D_SPAN;
            kek_3d_perspective(p, s, x + KEK_3D_SPAN, y, &u_end, &v_end);
            step = 1.f / (float)KEK_3D_SPAN;
        } else {
            kek_3d_perspective(p, s, last, y, &u_end, &v_end);
            step = KEK_3D_RECIPROCAL[count - 1];
        }
        du = (u_end - u) * step;
        dv = (v_end - v) * step;

        for (int i = 0; i < count; ++i, ++x) {
            if (kek_3d_covered(w0, w1, w2) && fabsf(inv_z) >= KEK_EPSILON &&
                kek_3d_depth_test(engine, (uint16_t)x, (uint16_t)y, inv_z)) {
                uint8_t texel = kek_texture_sample(engine, texture, u, v);
                int level = p->shade_uniform ? p->shade_fixed : kek_3d_shade_fixed(shade);
                kek_blit(engine, (uint16_t)x, (uint16_t)y, kek_3d_shade_row(engine, level, x, y)[texel]);
            }
            w0 += s->step_x[0]; w1 += s->step_x[1]; w2 += s->step_x[2];
            inv_z += p->inv_z.dx;
            shade += p->shade.dx;
            u += du;
            v += dv;
        }
        /* Exact again for the next span rather than carrying the drift. */
        u = u_end;
        v = v_end;
    }
}

void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    KEK_3D_Setup_ s;
    KEK_3D_TexturePlanes_ p;
    float row_w0, row_w1, row_w2;

    if (!kek_3d_setup(engine, vertices, &s)) {
        return;
    }
    p.inv_z = kek_3d_plane(&s, vertices[0].inv_z, vertices[1].inv_z, vertices[2].inv_z);
    p.u_over_z = kek_3d_plane(&s, vertices[0].u_over_z, vertices[1].u_over_z, vertices[2].u_over_z);
    p.v_over_z = kek_3d_plane(&s, vertices[0].v_over_z, vertices[1].v_over_z, vertices[2].v_over_z);
    p.shade = kek_3d_plane(&s, vertices[0].shade, vertices[1].shade, vertices[2].shade);
    p.shade_uniform = kek_3d_shade_uniform(vertices, &p.shade_fixed);
    row_w0 = s.w[0];
    row_w1 = s.w[1];
    row_w2 = s.w[2];

    for (int y = s.min_y; y <= s.max_y; ++y) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        float first_w0 = 0.f, first_w1 = 0.f, first_w2 = 0.f;
        int first = -1, last = -1;

        /* Where the row is covered, found with the edge functions alone —
           the same additions the drawing pass repeats, so both agree on
           every pixel. A triangle covers one stretch of a row, but the
           drawing pass tests each pixel again rather than rely on it. */
        for (int x = s.min_x; x <= s.max_x; ++x) {
            if (kek_3d_covered(w0, w1, w2)) {
                if (first < 0) {
                    first = x;
                    first_w0 = w0; first_w1 = w1; first_w2 = w2;
                }
                last = x;
            }
            w0 += s.step_x[0]; w1 += s.step_x[1]; w2 += s.step_x[2];
        }
        if (first >= 0) {
            kek_3d_textured_row(engine, texture, &s, &p, y, first, last, first_w0, first_w1, first_w2);
        }
        row_w0 += s.step_y[0]; row_w1 += s.step_y[1]; row_w2 += s.step_y[2];
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

/* The face's own shade in levels, from a normal derived from the face: a model
   stores no normals, since this is all flat shading needs of one.

   Both vectors are in view space. The camera turns the light and the face
   alike, so this is the same dot product as in the world. cross(b - a, c - a)
   points out of the face — the winding the backface cull keeps. */
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
    /* The view-space vertices are a temporary off the top of the arena, as
       many as this model has, gone again at the end of the call. */
    size_t mark = kek_arena_temp_mark(&e->arena);
    size_t view_verts_size;
    KEK_FVec3* view_verts;
    char use_colors = mdl->face_colors != 0 && mdl->colors_count > 0;
    /* Resolved once per model, not once per face: a stale handle just means
       the model draws untextured. */
    KEK_texture* texture = kek_texture_get(e, mdl->texture);
    /* Fix 4: precompute rotation matrices and projection constants once per call */
    KEK_Mat3 model_rot, camera_rot;
    KEK_FVec3 light_view;
    float aspect_ratio, focal_length, half_w, half_h;
    uint32_t i;

    /* A model the arena has no room to transform is not drawn. The product
       can only wrap where size_t is 32 bits, and the division catches that. */
    view_verts_size = (size_t)mdl->verts_count * sizeof(KEK_FVec3);
    if (view_verts_size / sizeof(KEK_FVec3) != mdl->verts_count) {
        return;
    }
    view_verts = (KEK_FVec3*)kek_arena_temp(&e->arena, view_verts_size);
    if (!view_verts) {
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

    kek_arena_temp_release(&e->arena, mark);
}
