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

KEK_camera KEK_DEFAULT_CAMERA = {
    .position = {0.f, 0.f, 0.f},
    .rotation = {0.f, 0.f, 0.f},
    .fov = 75.f,
    .near_plane = 0.1f,
    .far_plane = 1000.f
};

/* 1/z as the depth buffer holds it. NaN, and whatever rounds below 1, is the
   farthest a pixel can be rather than the clear, so it still draws on an
   empty frame, as it did while the buffer was float. */
static uint16_t kek_3d_depth_quantise(float inv_z) {
    float depth = inv_z * KEK_3D_DEPTH_SCALE;

    if (!(depth >= 1.f)) {
        return 1;
    }
    if (depth >= 65535.f) {
        return 65535;
    }
    return (uint16_t)depth;
}

static char kek_3d_depth_test(KEK_engine* engine, uint16_t x, uint16_t y, float inv_z) {
    uint32_t index = (uint32_t)y * (uint32_t)engine->w + (uint32_t)x;
    uint16_t depth = kek_3d_depth_quantise(inv_z);

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

/* a * b: applying it is applying b, then a. */
static KEK_Mat3 kek_3d_mat3_mul(KEK_Mat3 a, KEK_Mat3 b) {
    KEK_Mat3 result;
    int row, col;

    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            result.m[row * 3 + col] = a.m[row * 3 + 0] * b.m[0 * 3 + col] +
                                      a.m[row * 3 + 1] * b.m[1 * 3 + col] +
                                      a.m[row * 3 + 2] * b.m[2 * 3 + col];
        }
    }
    return result;
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

/* ---- The scanline rasteriser ----

   A triangle is walked down its edges a row at a time, and each row's covered
   stretch goes to a span loop that runs on integers: the depth in 16.15, u
   and v in 16.16 texels, the shade in 4096ths of a sixteenth of a level, and
   the row's place in the frame worked out once. Float is for the setup and
   the ends of a span, where it is converted once; the pixels in between are
   adds, shifts and masks. That is Quake's split, and the loop measured on the
   target in BACKLOG.md, Tier 2. */

/* Where an edge crosses a row, exactly: x + num / den, with 0 <= num < den,
   stepped by a whole part and a remainder per row as Bresenham steps a line.
   The pixels a closed triangle covers on a row run from the ceiling of its
   left edge to the floor of its right one, which is the set the edge
   functions of the bounding-box walk picked, without float in the way. */
typedef struct KEK_3D_Edge_ {
    int x;
    int32_t num, den;
    int step;
    int32_t rem;
} KEK_3D_Edge_;

/* Floor division by a positive divisor, which C's rounds toward zero. */
static int64_t kek_3d_floor_div(int64_t n, int64_t d, int64_t* out_rem) {
    int64_t q = n / d, r = n % d;

    if (r < 0) {
        --q;
        r += d;
    }
    *out_rem = r;
    return q;
}

/* The edge from a down to b, at row y. A horizontal edge is only ever asked
   for its one row, and stays at a. The products are 64-bit: after the near
   clip a vertex can be tens of thousands of pixels out. What has to fit in
   32 bits is twice an edge's height, so vertices up to 2^29 pixels out. */
static void kek_3d_edge_start(KEK_3D_Edge_* edge, KEK_IVec2 a, KEK_IVec2 b, int y) {
    int64_t dy = (int64_t)b.y - a.y;
    int64_t dx = (int64_t)b.x - a.x;
    int64_t rem;

    if (dy <= 0) {
        edge->x = a.x;
        edge->num = 0;
        edge->den = 1;
        edge->step = 0;
        edge->rem = 0;
        return;
    }
    edge->x = a.x + (int)kek_3d_floor_div((int64_t)(y - a.y) * dx, dy, &rem);
    edge->num = (int32_t)rem;
    edge->den = (int32_t)dy;
    edge->step = (int)kek_3d_floor_div(dx, dy, &rem);
    edge->rem = (int32_t)rem;
}

static void kek_3d_edge_step(KEK_3D_Edge_* edge) {
    edge->x += edge->step;
    edge->num += edge->rem;
    if (edge->num >= edge->den) {
        ++edge->x;
        edge->num -= edge->den;
    }
}

/* An attribute across the triangle: its value at vertex 0 and its change per
   pixel along x and y. Every attribute the rasterisers interpolate — 1/z,
   u/z, v/z, the shade — is linear in screen space, so this is worked out once
   per triangle and evaluated where a span starts and ends. */
typedef struct KEK_3D_Plane_ {
    float origin, dx, dy;
} KEK_3D_Plane_;

/* What both rasterisers need of a triangle before its first pixel: its
   vertices by height, which side the middle one is on, and what the planes
   are measured from. */
typedef struct KEK_3D_Setup_ {
    KEK_IVec2 top, mid, bottom;
    int mid_on_right;
    int x0, y0;
    float x1, y1, x2, y2;
    /* The one divide a triangle costs; every gradient is a multiply by it. */
    float inv_area;
} KEK_3D_Setup_;

static int kek_3d_setup(const KEK_3D_ProjectedVertex vertices[3], KEK_3D_Setup_* s) {
    KEK_IVec2 v0 = vertices[0].screen, v1 = vertices[1].screen, v2 = vertices[2].screen;
    KEK_IVec2 swap;
    /* In double, as the backface cull does: exact for anything the near
       clip produces, where float is not. */
    double area = (double)(v1.x - v0.x) * (double)(v2.y - v0.y) - (double)(v2.x - v0.x) * (double)(v1.y - v0.y);

    if (area == 0.) {
        return 0;
    }

    s->top = v0;
    s->mid = v1;
    s->bottom = v2;
    if (s->mid.y < s->top.y) { swap = s->top; s->top = s->mid; s->mid = swap; }
    if (s->bottom.y < s->mid.y) { swap = s->mid; s->mid = s->bottom; s->bottom = swap; }
    if (s->mid.y < s->top.y) { swap = s->top; s->top = s->mid; s->mid = swap; }
    s->mid_on_right = (double)(s->mid.x - s->top.x) * (double)(s->bottom.y - s->top.y) >
                      (double)(s->mid.y - s->top.y) * (double)(s->bottom.x - s->top.x);

    s->x0 = v0.x;
    s->y0 = v0.y;
    s->x1 = (float)(v1.x - v0.x);
    s->y1 = (float)(v1.y - v0.y);
    s->x2 = (float)(v2.x - v0.x);
    s->y2 = (float)(v2.y - v0.y);
    s->inv_area = (float)(1. / area);
    return 1;
}

static KEK_3D_Plane_ kek_3d_plane(const KEK_3D_Setup_* s, float a0, float a1, float a2) {
    KEK_3D_Plane_ p;
    float d1 = a1 - a0, d2 = a2 - a0;

    p.origin = a0;
    p.dx = (d1 * s->y2 - d2 * s->y1) * s->inv_area;
    p.dy = (d2 * s->x1 - d1 * s->x2) * s->inv_area;
    return p;
}

static float kek_3d_plane_at(KEK_3D_Plane_ p, const KEK_3D_Setup_* s, int x, int y) {
    return p.origin + p.dx * (float)(x - s->x0) + p.dy * (float)(y - s->y0);
}

/* One row's covered pixels, first to last, both already inside the frame. */
typedef void (*KEK_3D_SpanFn_)(KEK_engine* engine, const void* context, int y, int first, int last);

/* The rows from the top vertex down to the one before the middle, then from
   the middle down to the bottom, each clipped to the frame. The long edge
   runs top to bottom; the short ones meet at the middle vertex. */
static void kek_3d_walk(KEK_engine* engine, const KEK_3D_Setup_* s, KEK_3D_SpanFn_ span, const void* context) {
    int half;

    for (half = 0; half < 2; ++half) {
        KEK_3D_Edge_ long_edge, short_edge;
        const KEK_3D_Edge_* left = s->mid_on_right ? &long_edge : &short_edge;
        const KEK_3D_Edge_* right = s->mid_on_right ? &short_edge : &long_edge;
        int from = half ? s->mid.y : s->top.y;
        int to = half ? s->bottom.y : s->mid.y - 1;
        int y;

        from = KEK_MAX(from, 0);
        to = KEK_MIN(to, engine->h - 1);
        if (from > to) {
            continue;
        }
        kek_3d_edge_start(&long_edge, s->top, s->bottom, from);
        kek_3d_edge_start(&short_edge, half ? s->mid : s->top, half ? s->bottom : s->mid, from);
        for (y = from; y <= to; ++y) {
            int first = KEK_MAX(left->x + (left->num > 0), 0);
            int last = KEK_MIN(right->x, engine->w - 1);

            if (first <= last) {
                span(engine, context, y, first, last);
            }
            kek_3d_edge_step(&long_edge);
            kek_3d_edge_step(&short_edge);
        }
    }
}

/* 1/z at a span's end as the loop steps it: the depth buffer's value in 16.15.
   Clamped to what the buffer holds, as kek_3d_depth_quantise does, so that
   what lies between two ends is in range too; the top of the range leaves
   the int32_t a bit to spare. */
static int32_t kek_3d_depth_fixed(float inv_z) {
    float depth = inv_z * KEK_3D_DEPTH_SCALE;

    if (!(depth >= 1.f)) {
        depth = 1.f;
    } else if (depth > 65535.f) {
        depth = 65535.f;
    }
    return (int32_t)(depth * 32768.f);
}

/* The shade at a span's end in 4096ths of a sixteenth of a level. Not clamped
   to the table, since the shade may cross its ends inside a span and each
   pixel is clamped on its own; only bounded, far past any level, so the
   difference of two ends fits an int32_t. */
#define KEK_3D_SHADE_FRACTION_BITS 12
#define KEK_3D_SHADE_BOUND 8192.f

static int32_t kek_3d_shade_span_fixed(float shade) {
    if (!(shade > -KEK_3D_SHADE_BOUND)) {
        shade = shade != shade ? 0.f : -KEK_3D_SHADE_BOUND;
    } else if (shade > KEK_3D_SHADE_BOUND) {
        shade = KEK_3D_SHADE_BOUND;
    }
    return (int32_t)(shade * (float)(KEK_3D_SHADE_ONE << KEK_3D_SHADE_FRACTION_BITS));
}

/* One pixel's shading row from a stepped shade: the same dither as
   kek_3d_shade_row, with the clamp kek_3d_shade_fixed does on the way in. */
static const uint8_t* kek_3d_shade_row_stepped(const KEK_engine* engine, int32_t shade, int x, int y) {
    int32_t fixed;

    if (shade < 0) {
        fixed = 0;
    } else if (shade > ((int32_t)KEK_3D_SHADE_MAX << KEK_3D_SHADE_FRACTION_BITS)) {
        fixed = KEK_3D_SHADE_MAX;
    } else {
        fixed = shade >> KEK_3D_SHADE_FRACTION_BITS;
    }
    return kek_3d_shade_row(engine, (int)fixed, x, y);
}

/* An attribute's step per pixel between a span's ends, n pixels apart.
   Rounded toward the first, so the last pixel never overshoots the second. */
static int32_t kek_3d_span_step(int32_t from, int32_t to, int n) {
    return n > 0 ? (to - from) / n : 0;
}

typedef struct KEK_3D_Flat_ {
    KEK_3D_Setup_ setup;
    KEK_3D_Plane_ inv_z, shade;
    int shade_fixed;
    int shade_uniform;
    uint8_t color;
} KEK_3D_Flat_;

static void kek_3d_flat_span(KEK_engine* engine, const void* context, int y, int first, int last) {
    const KEK_3D_Flat_* f = (const KEK_3D_Flat_*)context;
    int n = last - first;
    int32_t z_first = kek_3d_depth_fixed(kek_3d_plane_at(f->inv_z, &f->setup, first, y));
    int32_t z_last = kek_3d_depth_fixed(kek_3d_plane_at(f->inv_z, &f->setup, last, y));
    /* Unsigned: the loop steps once past the last pixel, and near the top of
       the range that step wraps rather than overflows. */
    uint32_t z = (uint32_t)z_first;
    uint32_t dz = (uint32_t)kek_3d_span_step(z_first, z_last, n);
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    int x;

    if (f->shade_uniform) {
        /* One shade for the triangle: the dither leaves four inks a row. */
        uint8_t ink[4];
        for (x = 0; x < 4; ++x) {
            ink[x] = kek_3d_shade_row(engine, f->shade_fixed, x, y)[f->color];
        }
        for (x = first; x <= last; ++x) {
            uint16_t depth = (uint16_t)(z >> 15);
            if (depth > db[x]) {
                db[x] = depth;
                fb[x] = ink[x & 3];
            }
            z += dz;
        }
    } else {
        int32_t s = kek_3d_shade_span_fixed(kek_3d_plane_at(f->shade, &f->setup, first, y));
        int32_t ds = kek_3d_span_step(s, kek_3d_shade_span_fixed(kek_3d_plane_at(f->shade, &f->setup, last, y)), n);
        for (x = first; x <= last; ++x) {
            uint16_t depth = (uint16_t)(z >> 15);
            if (depth > db[x]) {
                db[x] = depth;
                fb[x] = kek_3d_shade_row_stepped(engine, s, x, y)[f->color];
            }
            z += dz;
            s += ds;
        }
    }
}

void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill) {
    KEK_3D_Flat_ f;

    if (!kek_3d_setup(vertices, &f.setup)) {
        return;
    }
    f.inv_z = kek_3d_plane(&f.setup, vertices[0].inv_z, vertices[1].inv_z, vertices[2].inv_z);
    f.shade = kek_3d_plane(&f.setup, vertices[0].shade, vertices[1].shade, vertices[2].shade);
    f.shade_uniform = kek_3d_shade_uniform(vertices, &f.shade_fixed);
    f.color = color_fill;
    kek_3d_walk(engine, &f.setup, kek_3d_flat_span, &f);
}

/* Perspective-correct u and v are divided out every KEK_3D_SPAN pixels and
   stepped linearly in between, which is how Quake hid one divide behind
   sixteen pixels. Within a span the texture is affine: at this resolution,
   and with a span this short, the error is a fraction of a texel. */
#define KEK_3D_SPAN 16

typedef struct KEK_3D_Textured_ {
    KEK_3D_Setup_ setup;
    KEK_3D_Plane_ inv_z, u_over_z, v_over_z, shade;
    int shade_fixed;
    int shade_uniform;
    const KEK_texture* texture;
    float width, height;
    int repeat;
    /* Both sides a power of two, which is every texture in practice: a texel
       is two shifts, two masks and an or. Any other size goes through
       kek_texture_sample per pixel, as everything did before, and is several
       times slower for it. */
    int masked;
    int v_shift;
    uint32_t u_mask, v_mask;
    /* How far 1/z, u/z and v/z move across a full span. */
    float span_inv_z, span_u_over_z, span_v_over_z;
} KEK_3D_Textured_;

static int kek_3d_log2(unsigned size) {
    int n = 0;

    while ((1u << n) < size) {
        ++n;
    }
    return (1u << n) == size ? n : -1;
}

/* 1/z, u/z and v/z at a span's end. Stepped from one end to the next along
   the row, three adds, rather than worked out from the planes each time, at
   two multiplies and two int-to-float conversions apiece. */
typedef struct KEK_3D_Perspective_ {
    float inv_z, u_over_z, v_over_z;
} KEK_3D_Perspective_;

/* u and v, in UV units, at a span's end: the one divide per span. 1/z is
   positive wherever the triangle is, since every vertex is past the near
   plane; the guard is for a value that rounding has brought to zero, which
   samples the corner rather than an infinity. */
static void kek_3d_perspective(const KEK_3D_Perspective_* p, float* out_u, float* out_v) {
    float z = fabsf(p->inv_z) >= KEK_EPSILON ? 1.f / p->inv_z : 0.f;

    *out_u = p->u_over_z * z;
    *out_v = p->v_over_z * z;
}

/* A coordinate in texels as the span loop steps it: 16.16, which the loop
   carries in a uint32_t.

   Under REPEAT the loop's mask takes it modulo the side, and a power of two
   up to 32768 divides 2^32 of these, so wrapping the integer wraps the
   texture: it only has to start somewhere the conversion is defined. Past
   ±32,768 texels it is folded into the texture first, and *out_folded says
   so. Under CLAMP it is held inside, half a texel short of the far edge,
   which still samples the last texel; then everything between two ends is
   inside too. Quake clamped s and t at the ends of its spans the same way. */
static int32_t kek_3d_texel_fixed(float t, float size, int repeat, int* out_folded) {
    if (repeat) {
        if (!(t > -32768.f && t < 32768.f)) {
            *out_folded = 1;
            t -= floorf(t / size) * size;
            /* NaN, and infinity, which the line above makes NaN. */
            if (!(t >= 0.f && t < 32768.f)) {
                return 0;
            }
        }
        return (int32_t)(t * 65536.f);
    }
    if (!(t >= 0.f)) {
        return 0;
    }
    if (t > size - 0.5f) {
        t = size - 0.5f;
    }
    return (int32_t)(t * 65536.f);
}

/* The step between two span ends: the difference of the ends as converted,
   divided, which for a full span is a shift. Rounded toward the first end,
   so under CLAMP the last pixel stays inside the texture. An end folded
   under REPEAT has lost its distance from the other, and the step comes from
   the coordinates as they were, in UV units, instead. Either way bounded at
   16,384 texels a span, past which a texture is noise whatever is sampled. */
#define KEK_3D_TEXEL_STEP_BOUND 1073741824

static uint32_t kek_3d_texel_step(int32_t from, int32_t to, float from_uv, float to_uv, float size,
                                  int folded, int steps) {
    int32_t d;

    if (steps <= 0) {
        return 0;
    }
    if (folded) {
        float f = (to_uv - from_uv) * size * 65536.f;
        if (f != f) {
            f = 0.f;
        } else if (f > (float)KEK_3D_TEXEL_STEP_BOUND) {
            f = (float)KEK_3D_TEXEL_STEP_BOUND;
        } else if (f < -(float)KEK_3D_TEXEL_STEP_BOUND) {
            f = -(float)KEK_3D_TEXEL_STEP_BOUND;
        }
        d = (int32_t)f;
    } else {
        int64_t wide = (int64_t)to - from;
        d = wide > KEK_3D_TEXEL_STEP_BOUND ? KEK_3D_TEXEL_STEP_BOUND
          : wide < -KEK_3D_TEXEL_STEP_BOUND ? -KEK_3D_TEXEL_STEP_BOUND : (int32_t)wide;
    }
    return (uint32_t)(steps == KEK_3D_SPAN ? d / KEK_3D_SPAN : d / steps);
}

/* What a row carries from one span of KEK_3D_SPAN pixels to the next: depth
   and shade are stepped across the whole row, not restarted per span. */
typedef struct KEK_3D_Stepped_ {
    /* Unsigned for the same reason as in kek_3d_flat_span. */
    uint32_t z, dz;
    int32_t s, ds;
} KEK_3D_Stepped_;

/* For a texture the masks cannot address: u and v in UV units, stepped as
   floats and sampled per pixel. */
static void kek_3d_textured_pixels_sampled(KEK_engine* engine, const KEK_3D_Textured_* t, KEK_3D_Stepped_* st,
                                           int y, int x, int count, int steps,
                                           float u, float v, float u_end, float v_end) {
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    float du = steps > 0 ? (u_end - u) / (float)steps : 0.f;
    float dv = steps > 0 ? (v_end - v) / (float)steps : 0.f;
    int i;

    for (i = 0; i < count; ++i, ++x) {
        uint16_t depth = (uint16_t)(st->z >> 15);
        if (depth > db[x]) {
            uint8_t texel = kek_texture_sample(engine, t->texture, u, v);
            const uint8_t* row = t->shade_uniform ? kek_3d_shade_row(engine, t->shade_fixed, x, y)
                                                  : kek_3d_shade_row_stepped(engine, st->s, x, y);
            db[x] = depth;
            fb[x] = row[texel];
        }
        st->z += st->dz;
        st->s += st->ds;
        u += du;
        v += dv;
    }
}

static void kek_3d_textured_span(KEK_engine* engine, const void* context, int y, int first, int last) {
    const KEK_3D_Textured_* t = (const KEK_3D_Textured_*)context;
    const uint8_t* pixels = t->texture->data;
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    const uint8_t* rows[4];
    KEK_3D_Stepped_ st;
    KEK_3D_Perspective_ p;
    int32_t z_first;
    float u, v;
    int32_t tu_next = 0, tv_next = 0;
    int u_folded_next = 0, v_folded_next = 0;
    int n = last - first;
    int x = first;

    z_first = kek_3d_depth_fixed(kek_3d_plane_at(t->inv_z, &t->setup, first, y));
    st.z = (uint32_t)z_first;
    st.dz = (uint32_t)kek_3d_span_step(z_first,
        kek_3d_depth_fixed(kek_3d_plane_at(t->inv_z, &t->setup, last, y)), n);
    st.s = 0;
    st.ds = 0;
    if (t->shade_uniform) {
        for (x = 0; x < 4; ++x) {
            rows[x] = kek_3d_shade_row(engine, t->shade_fixed, x, y);
        }
        x = first;
    } else {
        st.s = kek_3d_shade_span_fixed(kek_3d_plane_at(t->shade, &t->setup, first, y));
        st.ds = kek_3d_span_step(st.s, kek_3d_shade_span_fixed(kek_3d_plane_at(t->shade, &t->setup, last, y)), n);
    }

    p.inv_z = kek_3d_plane_at(t->inv_z, &t->setup, x, y);
    p.u_over_z = kek_3d_plane_at(t->u_over_z, &t->setup, x, y);
    p.v_over_z = kek_3d_plane_at(t->v_over_z, &t->setup, x, y);
    kek_3d_perspective(&p, &u, &v);
    /* Each span's end, converted, is where the next one starts. */
    if (t->masked) {
        tu_next = kek_3d_texel_fixed(u * t->width, t->width, t->repeat, &u_folded_next);
        tv_next = kek_3d_texel_fixed(v * t->height, t->height, t->repeat, &v_folded_next);
    }
    while (x <= last) {
        int count = last - x + 1;
        int steps;
        float u_end, v_end;
        int32_t tu_first, tv_first;
        int u_folded, v_folded;
        uint32_t tu, tv, du, dv;
        int i;

        /* A full span ends on the first pixel of the next, which is where
           that one starts; the last ends on the row's last pixel. */
        if (count > KEK_3D_SPAN) {
            count = steps = KEK_3D_SPAN;
            p.inv_z += t->span_inv_z;
            p.u_over_z += t->span_u_over_z;
            p.v_over_z += t->span_v_over_z;
        } else {
            float advance;
            steps = count - 1;
            advance = (float)steps;
            p.inv_z += t->inv_z.dx * advance;
            p.u_over_z += t->u_over_z.dx * advance;
            p.v_over_z += t->v_over_z.dx * advance;
        }
        kek_3d_perspective(&p, &u_end, &v_end);

        if (!t->masked) {
            kek_3d_textured_pixels_sampled(engine, t, &st, y, x, count, steps, u, v, u_end, v_end);
            x += count;
            u = u_end;
            v = v_end;
            continue;
        }

        tu_first = tu_next;
        tv_first = tv_next;
        u_folded = u_folded_next;
        v_folded = v_folded_next;
        u_folded_next = 0;
        v_folded_next = 0;
        tu_next = kek_3d_texel_fixed(u_end * t->width, t->width, t->repeat, &u_folded_next);
        tv_next = kek_3d_texel_fixed(v_end * t->height, t->height, t->repeat, &v_folded_next);
        du = kek_3d_texel_step(tu_first, tu_next, u, u_end, t->width, u_folded | u_folded_next, steps);
        dv = kek_3d_texel_step(tv_first, tv_next, v, v_end, t->height, v_folded | v_folded_next, steps);
        /* Unsigned from here: under REPEAT the loop's adds wrap. */
        tu = (uint32_t)tu_first;
        tv = (uint32_t)tv_first;

        if (t->shade_uniform) {
            for (i = 0; i < count; ++i, ++x) {
                uint16_t depth = (uint16_t)(st.z >> 15);
                if (depth > db[x]) {
                    db[x] = depth;
                    fb[x] = rows[x & 3][pixels[((tv >> t->v_shift) & t->v_mask) | ((tu >> 16) & t->u_mask)]];
                }
                st.z += st.dz;
                tu += du;
                tv += dv;
            }
        } else {
            for (i = 0; i < count; ++i, ++x) {
                uint16_t depth = (uint16_t)(st.z >> 15);
                if (depth > db[x]) {
                    uint8_t texel = pixels[((tv >> t->v_shift) & t->v_mask) | ((tu >> 16) & t->u_mask)];
                    db[x] = depth;
                    fb[x] = kek_3d_shade_row_stepped(engine, st.s, x, y)[texel];
                }
                st.z += st.dz;
                st.s += st.ds;
                tu += du;
                tv += dv;
            }
        }
        /* Exact again for the next span rather than carrying the drift. */
        u = u_end;
        v = v_end;
    }
}

void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    KEK_3D_Textured_ t;
    int log2_w = kek_3d_log2(texture->width);
    int log2_h = kek_3d_log2(texture->height);

    if (!kek_3d_setup(vertices, &t.setup)) {
        return;
    }
    t.inv_z = kek_3d_plane(&t.setup, vertices[0].inv_z, vertices[1].inv_z, vertices[2].inv_z);
    t.u_over_z = kek_3d_plane(&t.setup, vertices[0].u_over_z, vertices[1].u_over_z, vertices[2].u_over_z);
    t.v_over_z = kek_3d_plane(&t.setup, vertices[0].v_over_z, vertices[1].v_over_z, vertices[2].v_over_z);
    t.shade = kek_3d_plane(&t.setup, vertices[0].shade, vertices[1].shade, vertices[2].shade);
    t.shade_uniform = kek_3d_shade_uniform(vertices, &t.shade_fixed);
    t.texture = texture;
    t.width = (float)texture->width;
    t.height = (float)texture->height;
    t.repeat = engine->texture_warp_mode == KEK_TEXTURE_WARP_REPEAT;
    t.masked = log2_w >= 0 && log2_h >= 0;
    t.v_shift = t.masked ? 16 - log2_w : 0;
    t.u_mask = (uint32_t)texture->width - 1u;
    t.v_mask = t.masked ? ((uint32_t)texture->height - 1u) << log2_w : 0u;
    t.span_inv_z = t.inv_z.dx * (float)KEK_3D_SPAN;
    t.span_u_over_z = t.u_over_z.dx * (float)KEK_3D_SPAN;
    t.span_v_over_z = t.v_over_z.dx * (float)KEK_3D_SPAN;
    kek_3d_walk(engine, &t.setup, kek_3d_textured_span, &t);
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
    KEK_Mat3 model_rot, camera_rot, to_view;
    KEK_FVec3 light_view, origin;
    float aspect_ratio, focal_length, half_w, half_h;
    uint32_t i;
    int row;

    /* A model the arena has no room to transform is not drawn. */
    view_verts_size = (size_t)mdl->verts_count * sizeof(KEK_FVec3);
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

    /* A stored vertex q is at offset + scale * q in the model, so in view
       space it is camera_rot * (model_rot * (offset + scale * q) + pos -
       camera): one matrix, the rotations with the scale folded into their
       columns, and one translation, so taking the bytes back to float costs
       nothing beyond the conversion. */
    to_view = kek_3d_mat3_mul(camera_rot, model_rot);
    for (row = 0; row < 3; ++row) {
        to_view.m[row * 3 + 0] *= mdl->scale.x;
        to_view.m[row * 3 + 1] *= mdl->scale.y;
        to_view.m[row * 3 + 2] *= mdl->scale.z;
    }
    origin = kek_3d_translate(kek_mat3_apply(model_rot, mdl->offset), pos);
    origin = kek_mat3_apply(camera_rot, (KEK_FVec3){
        origin.x - camera->position.x,
        origin.y - camera->position.y,
        origin.z - camera->position.z });

    for (i = 0; i < mdl->verts_count; ++i) {
        KEK_model_vertex q = mdl->verts[i];
        view_verts[i] = kek_3d_translate(
            kek_mat3_apply(to_view, (KEK_FVec3){ (float)q.x, (float)q.y, (float)q.z }), origin);
    }

    for (i = 0; i < mdl->faces_count; ++i) {
        KEK_model_face face = mdl->faces[i];
        uint8_t color = use_colors && i < mdl->colors_count ? mdl->face_colors[i] : 15;
        KEK_model_face_uv face_uv = { 0, 0, 0 };
        char face_is_textured;
        KEK_FVec3 fv[3];
        KEK_FVec2 fuv[3];
        KEK_FVec3 cv[KEK_3D_CLIP_NEAR_MAX_VERTS];
        KEK_FVec2 cuv[KEK_3D_CLIP_NEAR_MAX_VERTS];
        float face_shade;
        int cn, tri_count, t;

        /* KEK_MODEL_UV_NONE is past every uvs_count, so a corner without a
           UV fails the same test as one out of range. */
        face_is_textured = texture != 0 && mdl->face_uvs != 0 && mdl->uvs != 0 && i < mdl->face_uvs_count;
        if (face_is_textured) {
            face_uv = mdl->face_uvs[i];
            face_is_textured = face_uv.a < mdl->uvs_count && face_uv.b < mdl->uvs_count &&
                               face_uv.c < mdl->uvs_count;
        }

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
            fuv[0] = mdl->uvs[face_uv.a]; fuv[1] = mdl->uvs[face_uv.b]; fuv[2] = mdl->uvs[face_uv.c];
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
