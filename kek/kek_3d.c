#include <math.h>
#include <float.h>
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

/* Guard band for integer edge setup, including twice an edge's height. */
#define KEK_3D_SCREEN_LIMIT_ 1048576

static int kek_3d_finite_(KEK_FVec3 v) {
    return fabsf(v.x) <= FLT_MAX && fabsf(v.y) <= FLT_MAX && fabsf(v.z) <= FLT_MAX;
}

static int kek_3d_camera_valid_(const KEK_camera* c) {
    return kek_3d_finite_(c->position) && kek_3d_finite_(c->rotation)
        && c->fov > 0.f && c->fov < 180.f && c->near_plane > 0.f
        && c->far_plane <= FLT_MAX && c->far_plane > c->near_plane;
}

static KEK_Mat3 kek_3d_view_rotation_(KEK_FVec3 rotation) {
    KEK_Mat3 m = kek_mat3_from_euler(rotation);
    float swap = m.m[1]; m.m[1] = m.m[3]; m.m[3] = swap;
    swap = m.m[2]; m.m[2] = m.m[6]; m.m[6] = swap;
    swap = m.m[5]; m.m[5] = m.m[7]; m.m[7] = swap;
    return m;
}

/* How the camera maps view space to the frame. */
typedef struct KEK_3D_Lens_ {
    float focal_length, aspect_ratio, half_w, half_h;
} KEK_3D_Lens_;

static KEK_3D_Lens_ kek_3d_lens_(const KEK_engine* e, const KEK_camera* c) {
    return (KEK_3D_Lens_){ 1.f / tanf(c->fov * KEK_PI / 360.f),
        (float)e->w / (float)e->h, (float)e->w * 0.5f, (float)e->h * 0.5f };
}

static inline KEK_FVec3 kek_3d_project_float_(const KEK_3D_Lens_* lens, KEK_FVec3 v) {
    float inv_z = 1.f / v.z;
    return (KEK_FVec3){
        (v.x * lens->focal_length * inv_z / lens->aspect_ratio + 1.f) * lens->half_w,
        (1.f - v.y * lens->focal_length * inv_z) * lens->half_h, inv_z};
}

static inline int kek_3d_project_screen_(KEK_FVec3 p, KEK_IVec2* screen, float* depth) {
    if (!(fabsf(p.x) <= KEK_3D_SCREEN_LIMIT_ && fabsf(p.y) <= KEK_3D_SCREEN_LIMIT_
          && p.z <= FLT_MAX)) return 0;
    screen->x = (int)p.x;
    screen->y = (int)p.y;
    *depth = p.z;
    return 1;
}

static int kek_3d_project_(const KEK_3D_Lens_* lens, KEK_FVec3 v, KEK_IVec2* screen, float* depth) {
    return kek_3d_project_screen_(kek_3d_project_float_(lens, v), screen, depth);
}

KEK_FVec3 kek_3d_world_to_view(KEK_FVec3 p, KEK_camera* camera) {
    KEK_FVec3 translated = {
        .x = p.x - camera->position.x,
        .y = p.y - camera->position.y,
        .z = p.z - camera->position.z
    };

    return kek_mat3_apply(kek_3d_view_rotation_(camera->rotation), translated);
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
    KEK_3D_ProjectedVertex result;
    KEK_3D_Lens_ lens;
    KEK_FVec3 view;
    if (!kek_3d_camera_valid_(camera) || !kek_3d_finite_(p)) return 0;
    view = kek_3d_world_to_view(p, camera);
    lens = kek_3d_lens_(engine, camera);
    if (!(lens.focal_length > 0.f && lens.focal_length <= FLT_MAX)) return 0;
    if (!kek_3d_finite_(view) || !kek_3d_is_in_depth_range(view, camera)
        || !kek_3d_project_(&lens, view, &result.screen, &result.inv_z)) return 0;
    result.depth = view.z;
    result.u_over_z = 0.f;
    result.v_over_z = 0.f;
    result.shade = 0.f;
    *out_vertex = result;
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

/* Floor division by a positive divisor, which C's rounds toward zero. One
   idiv gives both halves. */
static int32_t kek_3d_floor_div(int32_t n, int32_t d, int32_t* out_rem) {
    int32_t q = n / d, r = n % d;

    if (r < 0) {
        --q;
        r += d;
    }
    *out_rem = r;
    return q;
}

/* The same over 64 bits: a call into libgcc on x86-32, 150 cycles or so, so
   only for an edge whose product cannot fit 32. */
static int64_t kek_3d_floor_div64(int64_t n, int64_t d, int64_t* out_rem) {
    int64_t q = n / d, r = n % d;

    if (r < 0) {
        --q;
        r += d;
    }
    *out_rem = r;
    return q;
}

/* The edge from a down to b, at row y. A horizontal edge is only ever asked
   for its one row, and stays at a. What has to fit in 32 bits is twice an
   edge's height, so vertices up to 2^29 pixels out.

   The step is a 32-bit divide. Where the edge starts at its top, which is
   wherever the frame has not clipped the triangle, the start needs none; past
   that, the product of how far down and how far across is 32 bits while both
   are under 2^15 and 64 beyond, where the near clip has put a vertex tens of
   thousands of pixels out. */
static void kek_3d_edge_start(KEK_3D_Edge_* edge, KEK_IVec2 a, KEK_IVec2 b, int y) {
    int32_t dy = (int32_t)b.y - (int32_t)a.y;
    int32_t dx = (int32_t)b.x - (int32_t)a.x;
    int32_t down = (int32_t)y - (int32_t)a.y;
    int32_t rem;

    if (dy <= 0) {
        edge->x = a.x;
        edge->num = 0;
        edge->den = 1;
        edge->step = 0;
        edge->rem = 0;
        return;
    }
    if (down == 0) {
        edge->x = a.x;
        edge->num = 0;
    } else if (down < 32768 && dx > -32768 && dx < 32768) {
        edge->x = a.x + (int)kek_3d_floor_div(down * dx, dy, &rem);
        edge->num = rem;
    } else {
        int64_t rem64;
        edge->x = a.x + (int)kek_3d_floor_div64((int64_t)down * dx, dy, &rem64);
        edge->num = (int32_t)rem64;
    }
    edge->den = dy;
    edge->step = (int)kek_3d_floor_div(dx, dy, &rem);
    edge->rem = rem;
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

static int kek_3d_screen_bounded_(const KEK_3D_ProjectedVertex vertices[3]) {
    int i;
    for (i = 0; i < 3; ++i) {
        if (vertices[i].screen.x < -KEK_3D_SCREEN_LIMIT_ || vertices[i].screen.x > KEK_3D_SCREEN_LIMIT_
            || vertices[i].screen.y < -KEK_3D_SCREEN_LIMIT_ || vertices[i].screen.y > KEK_3D_SCREEN_LIMIT_) return 0;
    }
    return 1;
}

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
    KEK_3D_Edge_ long_edge, short_edge;
    const KEK_3D_Edge_* left = s->mid_on_right ? &long_edge : &short_edge;
    const KEK_3D_Edge_* right = s->mid_on_right ? &short_edge : &long_edge;
    /* The row the long edge stands at once started: it runs through both
       halves, and carries on from one into the other rather than starting
       again. */
    int long_y = -1;
    int half;

    for (half = 0; half < 2; ++half) {
        int from = half ? s->mid.y : s->top.y;
        int to = half ? s->bottom.y : s->mid.y - 1;
        int y;

        from = KEK_MAX(from, 0);
        to = KEK_MIN(to, engine->h - 1);
        if (from > to) {
            continue;
        }
        if (long_y != from) {
            kek_3d_edge_start(&long_edge, s->top, s->bottom, from);
        }
        long_y = to + 1;
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

/* value rounded to nearest. The double's mantissa does the rounding: adding
   1.5 * 2^52 fixes the exponent so that a unit lands in the lowest bit, and
   the integer is read straight out of the bits. That is no float-to-int
   conversion, which on x87 is an fldcw pair around fistp, and no range to
   check first: past 2^51 the result means nothing, but it is defined, and
   the mask keeps a NaN's sign out of it. Sound wherever double is IEEE-754
   binary64 and an assignment rounds to it, which C99 requires of x87 too. */
KEK_STATIC_ASSERT_DECL(kek_3d_double_is_64_bits, sizeof(double) == 8);

static int64_t kek_3d_round(double value) {
    double rounded = value + 6755399441055744.0;
    uint64_t bits;

    memcpy(&bits, &rounded, sizeof(bits));
    return (int64_t)(bits & 0x7FFFFFFFFFFFFFFFULL) - (int64_t)0x4338000000000000LL;
}

/* The same rounding for a value known to be within 2^31, as a 32-bit integer:
   the low word of the double is all of it, and no 64-bit arithmetic runs. */
static int32_t kek_3d_round32(double value) {
    double rounded = value + 6755399441055744.0;
    uint64_t bits;

    memcpy(&bits, &rounded, sizeof(bits));
    return (int32_t)(uint32_t)bits;
}

/* 1/z as the span loop steps it: the depth buffer's value in 16.15. Clamped
   to what the buffer holds, as kek_3d_depth_quantise does, and NaN to the
   farthest; the top of the range leaves the int32_t a bit to spare.
   *out_clamped says whether it had to be. */
#define KEK_3D_DEPTH_LEAST ((int64_t)1 << 15)
#define KEK_3D_DEPTH_MOST ((int64_t)65535 << 15)

static int32_t kek_3d_depth_fixed(float inv_z, int* out_clamped) {
    int64_t z;

    if (inv_z != inv_z) {
        *out_clamped = 1;
        return (int32_t)KEK_3D_DEPTH_LEAST;
    }
    z = kek_3d_round((double)inv_z * (double)KEK_3D_DEPTH_SCALE * 32768.);
    *out_clamped = z < KEK_3D_DEPTH_LEAST || z > KEK_3D_DEPTH_MOST;
    return (int32_t)(z < KEK_3D_DEPTH_LEAST ? KEK_3D_DEPTH_LEAST : z > KEK_3D_DEPTH_MOST ? KEK_3D_DEPTH_MOST : z);
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

/* What both rasterisers work out once per triangle for depth and shade, so
   that a row starts with one evaluation of each plane rather than two and a
   divide. */
typedef struct KEK_3D_Common_ {
    KEK_3D_Setup_ setup;
    KEK_3D_Plane_ inv_z, shade;
    int shade_fixed;
    int shade_uniform;
    /* A uniform shade's two shading rows, as kek_3d_shade_row picks between
       them: the one at its whole level, and how much of the next it has. */
    const uint8_t* shade_row;
    int shade_fraction;
    /* The planes' gradients along x in the loop's fixed point, when they
       fit: the step a row takes unless its far end would leave the range. */
    int32_t depth_dx, shade_dx;
    int depth_dx_fits, shade_dx_fits;
} KEK_3D_Common_;

static void kek_3d_common(const KEK_engine* engine, const KEK_3D_ProjectedVertex vertices[3], KEK_3D_Common_* c) {
    double depth_dx, shade_dx;

    c->inv_z = kek_3d_plane(&c->setup, vertices[0].inv_z, vertices[1].inv_z, vertices[2].inv_z);
    c->shade = kek_3d_plane(&c->setup, vertices[0].shade, vertices[1].shade, vertices[2].shade);
    c->shade_uniform = kek_3d_shade_uniform(vertices, &c->shade_fixed);
    c->shade_row = engine->shading_palette + (size_t)256 * (size_t)(c->shade_fixed / KEK_3D_SHADE_ONE);
    c->shade_fraction = c->shade_fixed % KEK_3D_SHADE_ONE;

    depth_dx = (double)c->inv_z.dx * (double)KEK_3D_DEPTH_SCALE * 32768.;
    c->depth_dx_fits = depth_dx > -1073741824. && depth_dx < 1073741824.;
    c->depth_dx = c->depth_dx_fits ? (int32_t)kek_3d_round(depth_dx) : 0;
    shade_dx = (double)c->shade.dx * (double)(KEK_3D_SHADE_ONE << KEK_3D_SHADE_FRACTION_BITS);
    c->shade_dx_fits = shade_dx > -536870912. && shade_dx < 536870912.;
    c->shade_dx = c->shade_dx_fits ? (int32_t)kek_3d_round(shade_dx) : 0;
}

/* The four shading rows a uniform shade dithers between along row y. */
static void kek_3d_dither_rows(const KEK_3D_Common_* c, int y, const uint8_t* rows[4]) {
    const uint8_t* thresholds = KEK_3D_BAYER4[y & 3];
    int i;

    for (i = 0; i < 4; ++i) {
        rows[i] = c->shade_row + (c->shade_fraction > thresholds[i] ? 256 : 0);
    }
}

/* A row's depth and, for a shade that varies, its shade: at the first pixel
   and stepped per pixel. */
typedef struct KEK_3D_Row_ {
    /* Unsigned: the loop steps once past the last pixel, and near the top of
       the range that step wraps rather than overflows. */
    uint32_t z, dz;
    int32_t s, ds;
} KEK_3D_Row_;

/* The step is the plane's own while it keeps the row's far end in range —
   checked in 64 bits, a multiply — and otherwise the difference of the ends,
   clamped each, divided. A start that had to be clamped takes the second
   way too: stepping from a clamped start is not the plane. */
static void kek_3d_row(const KEK_3D_Common_* c, int y, int first, int last, KEK_3D_Row_* r) {
    float fx = (float)(first - c->setup.x0), fy = (float)(y - c->setup.y0);
    int n = last - first;
    int clamped, ignored;
    int32_t z = kek_3d_depth_fixed(c->inv_z.origin + c->inv_z.dx * fx + c->inv_z.dy * fy, &clamped);
    int64_t z_end = (int64_t)z + (int64_t)n * c->depth_dx;

    r->z = (uint32_t)z;
    if (!clamped && c->depth_dx_fits && z_end >= KEK_3D_DEPTH_LEAST && z_end <= KEK_3D_DEPTH_MOST) {
        r->dz = (uint32_t)c->depth_dx;
    } else {
        r->dz = (uint32_t)kek_3d_span_step(z, kek_3d_depth_fixed(kek_3d_plane_at(c->inv_z, &c->setup, last, y),
                                                                 &ignored), n);
    }

    r->s = 0;
    r->ds = 0;
    if (!c->shade_uniform) {
        int32_t s = kek_3d_shade_span_fixed(c->shade.origin + c->shade.dx * fx + c->shade.dy * fy);
        int64_t s_end = (int64_t)s + (int64_t)n * c->shade_dx;
        r->s = s;
        if (c->shade_dx_fits && s_end > -1073741824 && s_end < 1073741824) {
            r->ds = c->shade_dx;
        } else {
            r->ds = kek_3d_span_step(s, kek_3d_shade_span_fixed(kek_3d_plane_at(c->shade, &c->setup, last, y)), n);
        }
    }
}

typedef struct KEK_3D_Flat_ {
    KEK_3D_Common_ c;
    uint8_t color;
} KEK_3D_Flat_;

static void kek_3d_flat_span(KEK_engine* engine, const void* context, int y, int first, int last) {
    const KEK_3D_Flat_* f = (const KEK_3D_Flat_*)context;
    KEK_3D_Row_ r;
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    int x;

    kek_3d_row(&f->c, y, first, last, &r);
    if (f->c.shade_uniform) {
        /* One shade for the triangle: the dither leaves four inks a row. */
        const uint8_t* rows[4];
        uint8_t ink[4];
        kek_3d_dither_rows(&f->c, y, rows);
        for (x = 0; x < 4; ++x) {
            ink[x] = rows[x][f->color];
        }
        for (x = first; x <= last; ++x) {
            uint16_t depth = (uint16_t)(r.z >> 15);
            if (depth > db[x]) {
                db[x] = depth;
                fb[x] = ink[x & 3];
            }
            r.z += r.dz;
        }
    } else {
        for (x = first; x <= last; ++x) {
            uint16_t depth = (uint16_t)(r.z >> 15);
            if (depth > db[x]) {
                db[x] = depth;
                fb[x] = kek_3d_shade_row_stepped(engine, r.s, x, y)[f->color];
            }
            r.z += r.dz;
            r.s += r.ds;
        }
    }
}

/* Keep these large hot functions on a cache-line boundary: adding camera
   validation must not shift their unchanged loops across instruction lines.
   DJGPP's COFF format permits at most 16-byte alignment; it and other C99
   compilers retain their default function alignment. */
#if defined(__GNUC__) && !defined(__DJGPP__)
#define KEK_3D_RASTER_ALIGN_ __attribute__((aligned(64)))
#else
#define KEK_3D_RASTER_ALIGN_
#endif

static KEK_3D_RASTER_ALIGN_ void kek_3d_triangle_bounded_(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill) {
    KEK_3D_Flat_ f;

    if (!kek_3d_setup(vertices, &f.c.setup)) {
        return;
    }
    kek_3d_common(engine, vertices, &f.c);
    f.color = color_fill;
    kek_3d_walk(engine, &f.c.setup, kek_3d_flat_span, &f);
}

/* Perspective-correct u and v are divided out every KEK_3D_SPAN pixels and
   stepped linearly in between, which is how Quake hid one divide behind
   sixteen pixels. Within a span the texture is affine, and the error grows
   with the square of its length. A span's setup costs ~260 cycles on the
   emulated Pentium whatever the span's length, so a longer span is a faster
   surface and a less exact one: there, 32 is 15% faster than 16 on a wall
   and 64 is 22%, at the price of near walls that wobble at 64. Quake's 16 is
   the exact end of it; 32 is the default. Up to 64; KEK_3D_SPAN sets it. */
#ifndef KEK_3D_SPAN
#define KEK_3D_SPAN 32
#endif
KEK_STATIC_ASSERT_DECL(kek_3d_span_in_the_reciprocal_table, KEK_3D_SPAN >= 1 && KEK_3D_SPAN <= 64);

typedef struct KEK_3D_Textured_ {
    KEK_3D_Common_ c;
    KEK_3D_Plane_ u_over_z, v_over_z;
    const KEK_texture* texture;
    int repeat;
    /* Both sides a power of two and no more than 65,536 texels, which is
       every texture in practice. The loop steps u in texels and v in texels
       times the width, both in 16.16, so a texel is two constant shifts, two
       masks and an or. Any other texture goes through kek_texture_sample per
       pixel, as everything did before, and is several times slower for it. */
    int masked;
    uint32_t u_mask, v_mask;
    /* From UV to what the loop steps: the width, and the height times the
       width. */
    float u_scale, v_scale;
    /* The most either may be under CLAMP: half a texel short of the far edge,
       which still samples the last texel. */
    int64_t u_most, v_most;
    /* How far 1/z, u/z and v/z move across a full span. */
    float span_inv_z, span_u_over_z, span_v_over_z;
    /* When u and v at the triangle's vertices are all within 4,096 texels and
       rows, so that they are within what 32 bits hold everywhere the spans
       reach: a span's ends are then converted and stepped in 32 bits, v in
       rows and multiplied by the width once clamped, as the affine path does;
       past that, the 64-bit way above. */
    int fast32;
    uint32_t width;
    float v_rows;
    int32_t u_lo, u_hi, v_lo, v_hi;
} KEK_3D_Textured_;

static int32_t kek_3d_clamp32(int32_t t, int32_t lo, int32_t hi);
static uint32_t kek_3d_texel_step32(uint32_t from, uint32_t to, int steps);

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

/* value in 16.16, rounded to nearest; meaningless but defined past 2^35. */
static int64_t kek_3d_fixed16(float value) {
    return kek_3d_round((double)value * 65536.0);
}

/* A span end as the loop steps it. Under REPEAT as it is: the loop carries
   it in a uint32_t, and the mask takes that modulo the side, which divides
   2^32. Under CLAMP held inside the texture, so everything between two ends
   is inside too; Quake clamped s and t at the ends of its spans the same
   way. */
static int64_t kek_3d_texel_end(float uv, float scale, int64_t most, int repeat) {
    int64_t t = kek_3d_fixed16(uv * scale);

    if (!repeat) {
        t = t < 0 ? 0 : t > most ? most : t;
    }
    return t;
}

/* The step between two span ends, rounded toward the first so that under
   CLAMP the last pixel stays inside the texture, and a shift for a full
   span. The ends are as they are, not as they wrap; the difference is
   bounded at 16,384 texels a span, past which a texture is noise whatever
   is sampled. Two results of kek_3d_fixed16 are never far enough apart to
   overflow their difference. */
#define KEK_3D_TEXEL_STEP_BOUND 1073741824

/* (2^32 - 1) / n: a row's last, shorter span divides by its number of steps
   as a multiply, 10 cycles on a Pentium against idiv's 46. The quotient it
   gives is never larger than the true one, so the rounding toward the first
   end holds. */
#define KEK_3D_RECIPROCAL_(n) (0xFFFFFFFFu / (n))
#define KEK_3D_RECIPROCAL_4_(n) \
    KEK_3D_RECIPROCAL_(n), KEK_3D_RECIPROCAL_((n) + 1), KEK_3D_RECIPROCAL_((n) + 2), KEK_3D_RECIPROCAL_((n) + 3)
#define KEK_3D_RECIPROCAL_16_(n) \
    KEK_3D_RECIPROCAL_4_(n), KEK_3D_RECIPROCAL_4_((n) + 4), KEK_3D_RECIPROCAL_4_((n) + 8), KEK_3D_RECIPROCAL_4_((n) + 12)
/* Up to the longest span there can be, 64; entry 0 is never used. */
static const uint32_t KEK_3D_RECIPROCAL[65] = {
    0, KEK_3D_RECIPROCAL_(1), KEK_3D_RECIPROCAL_(2), KEK_3D_RECIPROCAL_(3),
    KEK_3D_RECIPROCAL_4_(4), KEK_3D_RECIPROCAL_4_(8), KEK_3D_RECIPROCAL_4_(12),
    KEK_3D_RECIPROCAL_16_(16), KEK_3D_RECIPROCAL_16_(32), KEK_3D_RECIPROCAL_16_(48)
};

static uint32_t kek_3d_texel_step(int64_t from, int64_t to, int steps) {
    int64_t d = to - from;
    uint32_t size = (uint32_t)(d > KEK_3D_TEXEL_STEP_BOUND ? KEK_3D_TEXEL_STEP_BOUND
                             : d < -KEK_3D_TEXEL_STEP_BOUND ? KEK_3D_TEXEL_STEP_BOUND : (d < 0 ? -d : d));
    uint32_t step;

    if (steps <= 0) {
        return 0;
    }
    if (steps == KEK_3D_SPAN) {
        step = size / KEK_3D_SPAN;
    } else {
        step = (uint32_t)(((uint64_t)size * KEK_3D_RECIPROCAL[steps]) >> 32);
    }
    /* Negated as unsigned: the loop adds it modulo 2^32. */
    return d < 0 ? 0u - step : step;
}

/* For a texture the masks cannot address: u and v in UV units, stepped as
   floats and sampled per pixel. Depth and shade go on from span to span in
   the row's st. */
static void kek_3d_textured_pixels_sampled(KEK_engine* engine, const KEK_3D_Textured_* t, KEK_3D_Row_* st,
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
            const uint8_t* row = t->c.shade_uniform ? kek_3d_shade_row(engine, t->c.shade_fixed, x, y)
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

/* The pixels [x, end) of one span with a uniform shade: the loop the rest is
   there to feed. Everything arrives as a plain value rather than through the
   triangle's struct, since a store through a uint8_t* may alias anything the
   compiler cannot see is local, and it would reload each of them after every
   pixel. Returns the depth where the span ended. */
static uint32_t kek_3d_texels(uint16_t* db, uint8_t* fb, int x, int end, const uint8_t* const rows[4],
                              const uint8_t* pixels, uint32_t u_mask, uint32_t v_mask,
                              uint32_t z, uint32_t dz, uint32_t tu, uint32_t du, uint32_t tv, uint32_t dv) {
    for (; x < end; ++x) {
        uint16_t depth = (uint16_t)(z >> 15);
        if (depth > db[x]) {
            db[x] = depth;
            fb[x] = rows[x & 3][pixels[((tv >> 16) & v_mask) | ((tu >> 16) & u_mask)]];
        }
        z += dz;
        tu += du;
        tv += dv;
    }
    return z;
}

/* The same with a shade stepped across the span: each pixel's shade is
   clamped to the table and dithered as kek_3d_shade_row_stepped does it,
   with the row of thresholds for this y picked once. The shade where the
   span ended goes back through *s. */
static uint32_t kek_3d_texels_shaded(uint16_t* db, uint8_t* fb, int x, int end, const uint8_t* shading,
                                     const uint8_t bayer[4], const uint8_t* pixels, uint32_t u_mask, uint32_t v_mask,
                                     uint32_t z, uint32_t dz, int32_t* s, int32_t ds,
                                     uint32_t tu, uint32_t du, uint32_t tv, uint32_t dv) {
    const int32_t most = (int32_t)KEK_3D_SHADE_MAX << KEK_3D_SHADE_FRACTION_BITS;
    int32_t shade = *s;

    for (; x < end; ++x) {
        uint16_t depth = (uint16_t)(z >> 15);
        if (depth > db[x]) {
            uint32_t fixed = (uint32_t)(shade < 0 ? 0 : shade > most ? most : shade) >> KEK_3D_SHADE_FRACTION_BITS;
            uint32_t level = fixed / KEK_3D_SHADE_ONE + (fixed % KEK_3D_SHADE_ONE > bayer[x & 3]);
            db[x] = depth;
            fb[x] = shading[((size_t)level << 8) + pixels[((tv >> 16) & v_mask) | ((tu >> 16) & u_mask)]];
        }
        z += dz;
        shade += ds;
        tu += du;
        tv += dv;
    }
    *s = shade;
    return z;
}

static void kek_3d_textured_span(KEK_engine* engine, const void* context, int y, int first, int last) {
    const KEK_3D_Textured_* t = (const KEK_3D_Textured_*)context;
    const uint8_t* pixels = t->texture->data;
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    const uint8_t* rows[4];
    KEK_3D_Row_ st;
    KEK_3D_Perspective_ p;
    float fx = (float)(first - t->c.setup.x0), fy = (float)(y - t->c.setup.y0);
    float u, v;
    int64_t tu_next = 0, tv_next = 0;
    int32_t fu_next = 0, fv_next = 0;
    int x = first;

    kek_3d_row(&t->c, y, first, last, &st);
    if (t->c.shade_uniform) {
        kek_3d_dither_rows(&t->c, y, rows);
    }

    p.inv_z = t->c.inv_z.origin + t->c.inv_z.dx * fx + t->c.inv_z.dy * fy;
    p.u_over_z = t->u_over_z.origin + t->u_over_z.dx * fx + t->u_over_z.dy * fy;
    p.v_over_z = t->v_over_z.origin + t->v_over_z.dx * fx + t->v_over_z.dy * fy;
    kek_3d_perspective(&p, &u, &v);
    /* Each span's end, converted, is where the next one starts. */
    if (t->fast32) {
        fu_next = kek_3d_clamp32(kek_3d_round32((double)(u * t->u_scale) * 65536.0), t->u_lo, t->u_hi);
        fv_next = kek_3d_clamp32(kek_3d_round32((double)(v * t->v_rows) * 65536.0), t->v_lo, t->v_hi);
    } else if (t->masked) {
        tu_next = kek_3d_texel_end(u, t->u_scale, t->u_most, t->repeat);
        tv_next = kek_3d_texel_end(v, t->v_scale, t->v_most, t->repeat);
    }
    while (x <= last) {
        int count = last - x + 1;
        int steps, end;
        float u_end, v_end;
        int64_t tu_first, tv_first;
        uint32_t tu, tv, du, dv;

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
            p.inv_z += t->c.inv_z.dx * advance;
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

        if (t->fast32) {
            int32_t fu_first = fu_next, fv_first = fv_next;

            fu_next = kek_3d_clamp32(kek_3d_round32((double)(u_end * t->u_scale) * 65536.0), t->u_lo, t->u_hi);
            fv_next = kek_3d_clamp32(kek_3d_round32((double)(v_end * t->v_rows) * 65536.0), t->v_lo, t->v_hi);
            du = kek_3d_texel_step32((uint32_t)fu_first, (uint32_t)fu_next, steps);
            dv = kek_3d_texel_step32((uint32_t)fv_first, (uint32_t)fv_next, steps) * t->width;
            tu = (uint32_t)fu_first;
            tv = (uint32_t)fv_first * t->width;
        } else {
            tu_first = tu_next;
            tv_first = tv_next;
            tu_next = kek_3d_texel_end(u_end, t->u_scale, t->u_most, t->repeat);
            tv_next = kek_3d_texel_end(v_end, t->v_scale, t->v_most, t->repeat);
            du = kek_3d_texel_step(tu_first, tu_next, steps);
            dv = kek_3d_texel_step(tv_first, tv_next, steps);
            /* Unsigned from here, taking the low 32 bits: under REPEAT the
               loop's adds wrap, and so does the texture. */
            tu = (uint32_t)(uint64_t)tu_first;
            tv = (uint32_t)(uint64_t)tv_first;
        }

        end = x + count;
        if (t->c.shade_uniform) {
            st.z = kek_3d_texels(db, fb, x, end, rows, pixels, t->u_mask, t->v_mask, st.z, st.dz, tu, du, tv, dv);
            x = end;
        } else {
            st.z = kek_3d_texels_shaded(db, fb, x, end, engine->shading_palette, KEK_3D_BAYER4[y & 3], pixels,
                                        t->u_mask, t->v_mask, st.z, st.dz, &st.s, st.ds, tu, du, tv, dv);
            x = end;
        }
        /* Exact again for the next span rather than carrying the drift. */
        u = u_end;
        v = v_end;
    }
}

/* A triangle this many pixels wide or less is drawn affine: u and v are
   planes over the screen like the depth, worked out once per triangle from its
   vertices, and a row is a few integer multiplies rather than the
   perspective divide at each end of every span. Up to KEK_3D_SPAN pixels a
   row is one span already and only its ends come from the plane instead of
   the divide; past that the texture is affine across the triangle where it
   was perspective-correct every 16 pixels, the trade Quake made for its
   models. The demo's cat, on the emulated Pentium, is 16% faster at 48 and
   its frames differ from the perspective ones by a handful of pixels. Wider
   triangles keep the perspective path. 0 turns it off. */
#ifndef KEK_3D_AFFINE_WIDTH
#define KEK_3D_AFFINE_WIDTH 48
#endif
#ifndef KEK_3D_AFFINE_FAST_ROW
#define KEK_3D_AFFINE_FAST_ROW 1
#endif

typedef struct KEK_3D_Affine_ {
    KEK_3D_Common_ c;
    const KEK_texture* texture;
    uint32_t u_mask, v_mask, width;
    /* u in texels and v in texel rows, in 16.16, at vertex 0 and per pixel;
       the loop steps v times the width, which is one multiply after the
       clamp. Computed unsigned, modulo 2^32, and read as signed to clamp:
       the caller keeps them inside +-2^15 rows, so the read is exact, and
       32 bits is the whole cost where 64 is a libgcc call on the targets
       this is for. The clamp is the perspective span's: half a texel short
       of the far edge, or no bounds at all under REPEAT, where the loop's
       adds wrap as the texture does. */
    uint32_t u_origin, v_origin, u_dx, u_dy, v_dx, v_dy;
    int32_t u_lo, u_hi, v_lo, v_hi;
    /* Depth and shade as planes in the loop's fixed point too, when the
       triangle's vertices are well inside what the buffer and the shade can
       hold, so that nothing between them needs clamping: a row then starts
       with a multiply per attribute, not kek_3d_row's float evaluation and
       rounding. */
    int fast_row;
    uint32_t z_origin, z_dy, s_origin, s_dy;
} KEK_3D_Affine_;

/* Whether a perspective span's ends may be converted and stepped in 32 bits
   (see KEK_3D_Textured_::fast32): a texture of up to 4,096 a side, and u and v
   at every vertex within `bound` texels and rows. Both are linear over the
   screen divided by a positive 1/z, so nothing inside the triangle is beyond
   the vertices; checked as u/z * width < bound * 1/z, without the divide.
   NaN fails every comparison and so takes the 64-bit way. */
#define KEK_3D_PERSPECTIVE_TEXELS 4096.f
#ifndef KEK_3D_PERSPECTIVE_FAST32
#define KEK_3D_PERSPECTIVE_FAST32 1
#endif

static int kek_3d_texels_within_(const KEK_3D_ProjectedVertex v[3], const KEK_texture* texture, float bound) {
    float w = (float)texture->width, h = (float)texture->height;
    int k;

    if (texture->width > 4096 || texture->height > 4096) {
        return 0;
    }
    for (k = 0; k < 3; ++k) {
        if (!(v[k].inv_z >= KEK_EPSILON && fabsf(v[k].u_over_z) * w < bound * v[k].inv_z
              && fabsf(v[k].v_over_z) * h < bound * v[k].inv_z)) {
            return 0;
        }
    }
    return 1;
}

static int kek_3d_affine_small_(const KEK_3D_ProjectedVertex v[3]) {
    int lo = v[0].screen.x, hi = lo, i;

    if (KEK_3D_AFFINE_WIDTH <= 0) {
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (!(v[i].inv_z >= KEK_EPSILON)) {
            return 0;
        }
        lo = KEK_MIN(lo, v[i].screen.x);
        hi = KEK_MAX(hi, v[i].screen.x);
    }
    return hi - lo <= KEK_3D_AFFINE_WIDTH;
}

/* The step from one span end to the next, both modulo 2^32, as
   kek_3d_texel_step gives it: the signed difference over the steps, rounded
   toward the first end, with a multiply by a reciprocal in place of the
   divide. */
static int32_t kek_3d_clamp32(int32_t t, int32_t lo, int32_t hi) {
    return t < lo ? lo : t > hi ? hi : t;
}

static uint32_t kek_3d_texel_step32(uint32_t from, uint32_t to, int steps) {
    int32_t d = (int32_t)(to - from);
    uint32_t size = d < 0 ? 0u - (uint32_t)d : (uint32_t)d;
    uint32_t step;

    if (steps <= 0) {
        return 0;
    }
    step = steps == KEK_3D_SPAN ? size / KEK_3D_SPAN
                                : (uint32_t)(((uint64_t)size * KEK_3D_RECIPROCAL[steps]) >> 32);
    return d < 0 ? 0u - step : step;
}

static void kek_3d_affine_span(KEK_engine* engine, const void* context, int y, int first, int last) {
    const KEK_3D_Affine_* a = (const KEK_3D_Affine_*)context;
    const uint8_t* pixels = a->texture->data;
    uint16_t* db = engine->db + (size_t)y * engine->w;
    uint8_t* fb = engine->fb + (size_t)y * engine->w;
    const uint8_t* rows[4];
    KEK_3D_Row_ st;
    uint32_t dy = (uint32_t)(y - a->c.setup.y0), x0 = (uint32_t)a->c.setup.x0;
    uint32_t u_row = a->u_origin + a->u_dy * dy - a->u_dx * x0;
    uint32_t v_row = a->v_origin + a->v_dy * dy - a->v_dx * x0;
    int32_t u_next = kek_3d_clamp32((int32_t)(u_row + a->u_dx * (uint32_t)first), a->u_lo, a->u_hi);
    int32_t v_next = kek_3d_clamp32((int32_t)(v_row + a->v_dx * (uint32_t)first), a->v_lo, a->v_hi);
    int x = first;

    if (a->fast_row) {
        st.z = a->z_origin + a->z_dy * (uint32_t)y + (uint32_t)a->c.depth_dx * (uint32_t)first;
        st.dz = (uint32_t)a->c.depth_dx;
        st.s = 0;
        st.ds = 0;
        if (!a->c.shade_uniform) {
            st.s = (int32_t)(a->s_origin + a->s_dy * (uint32_t)y + (uint32_t)a->c.shade_dx * (uint32_t)first);
            st.ds = a->c.shade_dx;
        }
    } else {
        kek_3d_row(&a->c, y, first, last, &st);
    }
    if (a->c.shade_uniform) {
        kek_3d_dither_rows(&a->c, y, rows);
    }
    /* Single-pixel rows are common on small models. There is no UV step to
       derive and the general texel loop would only execute once. */
    if (first == last) {
        uint16_t depth = (uint16_t)(st.z >> 15);
        if (depth > db[first]) {
            uint32_t tu = (uint32_t)u_next;
            uint32_t tv = (uint32_t)v_next * a->width;
            uint32_t texel = pixels[((tv >> 16) & a->v_mask) | ((tu >> 16) & a->u_mask)];
            const uint8_t* row = a->c.shade_uniform ? rows[first & 3]
                                      : kek_3d_shade_row_stepped(engine, st.s, first, y);
            db[first] = depth;
            fb[first] = row[texel];
        }
        return;
    }
    while (x <= last) {
        int count = last - x + 1;
        int steps = count > KEK_3D_SPAN ? KEK_3D_SPAN : count - 1;
        int end;
        int32_t u_first = u_next, v_first = v_next;
        uint32_t tu_first = (uint32_t)u_first, tv_first = (uint32_t)v_first * a->width;
        uint32_t du, dv;

        if (count > KEK_3D_SPAN) {
            count = KEK_3D_SPAN;
        }
        u_next = kek_3d_clamp32((int32_t)(u_row + a->u_dx * (uint32_t)(x + steps)), a->u_lo, a->u_hi);
        v_next = kek_3d_clamp32((int32_t)(v_row + a->v_dx * (uint32_t)(x + steps)), a->v_lo, a->v_hi);
        du = kek_3d_texel_step32((uint32_t)u_first, (uint32_t)u_next, steps);
        dv = kek_3d_texel_step32((uint32_t)v_first, (uint32_t)v_next, steps) * a->width;
        end = x + count;
        if (a->c.shade_uniform) {
            st.z = kek_3d_texels(db, fb, x, end, rows, pixels, a->u_mask, a->v_mask, st.z, st.dz,
                                 tu_first, du, tv_first, dv);
        } else {
            st.z = kek_3d_texels_shaded(db, fb, x, end, engine->shading_palette, KEK_3D_BAYER4[y & 3], pixels,
                                        a->u_mask, a->v_mask, st.z, st.dz, &st.s, st.ds,
                                        tu_first, du, tv_first, dv);
        }
        x = end;
    }
}

/* Whether the depth and shade planes may be stepped in fixed point from the
   vertices: their values there unclamped, and far enough inside the ranges
   that the pixels, which reach a pixel past the vertices, stay inside them
   too. Fills a's fixed-point origins and y steps. */
static int kek_3d_affine_fast_row_(const KEK_3D_ProjectedVertex v[3], KEK_3D_Affine_* a) {
    const KEK_3D_Common_* c = &a->c;
    double depth_dy = (double)c->inv_z.dy * (double)KEK_3D_DEPTH_SCALE * 32768.;
    int32_t z[3];
    int64_t margin, zmin, zmax;
    int k, clamped;

    if (!c->depth_dx_fits || !(depth_dy > -1073741824. && depth_dy < 1073741824.)) {
        return 0;
    }
    zmin = zmax = 0;
    for (k = 0; k < 3; ++k) {
        z[k] = kek_3d_depth_fixed(v[k].inv_z, &clamped);
        if (clamped) {
            return 0;
        }
        zmin = k == 0 || z[k] < zmin ? z[k] : zmin;
        zmax = k == 0 || z[k] > zmax ? z[k] : zmax;
    }
    a->z_dy = (uint32_t)(int32_t)kek_3d_round(depth_dy);
    margin = 2 * (((int64_t)c->depth_dx < 0 ? -(int64_t)c->depth_dx : (int64_t)c->depth_dx)
                  + ((int32_t)a->z_dy < 0 ? -(int64_t)(int32_t)a->z_dy : (int64_t)(int32_t)a->z_dy));
    if (zmin - margin < KEK_3D_DEPTH_LEAST || zmax + margin > KEK_3D_DEPTH_MOST) {
        return 0;
    }
    a->z_origin = (uint32_t)z[0] - (uint32_t)c->depth_dx * (uint32_t)c->setup.x0 - a->z_dy * (uint32_t)c->setup.y0;
    if (!c->shade_uniform) {
        double shade_dy = (double)c->shade.dy * (double)(KEK_3D_SHADE_ONE << KEK_3D_SHADE_FRACTION_BITS);

        if (!c->shade_dx_fits || !(shade_dy > -536870912. && shade_dy < 536870912.)) {
            return 0;
        }
        for (k = 0; k < 3; ++k) {
            if (!(v[k].shade > -KEK_3D_SHADE_BOUND / 2.f && v[k].shade < KEK_3D_SHADE_BOUND / 2.f)) {
                return 0;
            }
        }
        a->s_dy = (uint32_t)(int32_t)kek_3d_round(shade_dy);
        a->s_origin = (uint32_t)kek_3d_shade_span_fixed(v[0].shade) - (uint32_t)c->shade_dx * (uint32_t)c->setup.x0
                      - a->s_dy * (uint32_t)c->setup.y0;
    }
    return 1;
}

/* The limits under which the 32-bit planes are exact: textures of up to 4,096
   a side, u and v at the vertices within 8,192 texels and rows, and no more
   than 4,096 of either per pixel, so that a pixel past the vertices still
   reads inside +-2^15. A triangle outside them, or a texture, is drawn the
   perspective way. */
#define KEK_3D_AFFINE_TEXELS 8192.f
#define KEK_3D_AFFINE_STEP 4096.f

static int kek_3d_texel_plane_(const KEK_3D_Setup_* s, const float t[3], float bound,
                               uint32_t* origin, uint32_t* dx, uint32_t* dy) {
    KEK_3D_Plane_ p;
    int k;

    for (k = 0; k < 3; ++k) {
        if (!(fabsf(t[k]) < bound)) {
            return 0;
        }
    }
    p = kek_3d_plane(s, t[0], t[1], t[2]);
    if (!(fabsf(p.dx) < KEK_3D_AFFINE_STEP && fabsf(p.dy) < KEK_3D_AFFINE_STEP)) {
        return 0;
    }
    *origin = (uint32_t)(int32_t)kek_3d_fixed16(p.origin);
    *dx = (uint32_t)(int32_t)kek_3d_fixed16(p.dx);
    *dy = (uint32_t)(int32_t)kek_3d_fixed16(p.dy);
    return 1;
}

/* 0 when the triangle is outside the limits above, drawn nothing; 1 otherwise,
   drawn or, if it covers no pixel, nothing to draw. Only for a texture the
   masks can address, which is why the caller checks. */
static int kek_3d_triangle_textured_affine_(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3],
                                            const KEK_texture* texture) {
    KEK_3D_Affine_ a;
    float ut[3], vt[3];
    int k;

    if (texture->width > 4096 || texture->height > 4096) {
        return 0;
    }
    for (k = 0; k < 3; ++k) {
        float z = 1.f / vertices[k].inv_z;
        ut[k] = vertices[k].u_over_z * z * (float)texture->width;
        vt[k] = vertices[k].v_over_z * z * (float)texture->height;
    }
    if (!kek_3d_setup(vertices, &a.c.setup)) {
        return 1;
    }
    if (!kek_3d_texel_plane_(&a.c.setup, ut, KEK_3D_AFFINE_TEXELS, &a.u_origin, &a.u_dx, &a.u_dy)
        || !kek_3d_texel_plane_(&a.c.setup, vt, KEK_3D_AFFINE_TEXELS, &a.v_origin, &a.v_dx, &a.v_dy)) {
        return 0;
    }
    kek_3d_common(engine, vertices, &a.c);
    a.texture = texture;
    a.width = (uint32_t)texture->width;
    a.u_mask = a.width - 1u;
    a.v_mask = ((uint32_t)texture->height - 1u) * a.width;
    if (engine->texture_warp_mode == KEK_TEXTURE_WARP_REPEAT) {
        a.u_lo = a.v_lo = INT32_MIN;
        a.u_hi = a.v_hi = INT32_MAX;
    } else {
        a.u_lo = a.v_lo = 0;
        a.u_hi = (int32_t)texture->width * 65536 - 32768;
        a.v_hi = (int32_t)texture->height * 65536 - 32768;
    }
    a.fast_row = KEK_3D_AFFINE_FAST_ROW && kek_3d_affine_fast_row_(vertices, &a);
    kek_3d_walk(engine, &a.c.setup, kek_3d_affine_span, &a);
    return 1;
}

static KEK_3D_RASTER_ALIGN_ void kek_3d_triangle_textured_bounded_(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    KEK_3D_Textured_ t;
    int log2_w = kek_3d_log2(texture->width);
    int log2_h = kek_3d_log2(texture->height);

    if (log2_w >= 0 && log2_h >= 0 && log2_w + log2_h <= 16 && kek_3d_affine_small_(vertices)
        && kek_3d_triangle_textured_affine_(engine, vertices, texture)) {
        return;
    }
    if (!kek_3d_setup(vertices, &t.c.setup)) {
        return;
    }
    kek_3d_common(engine, vertices, &t.c);
    t.u_over_z = kek_3d_plane(&t.c.setup, vertices[0].u_over_z, vertices[1].u_over_z, vertices[2].u_over_z);
    t.v_over_z = kek_3d_plane(&t.c.setup, vertices[0].v_over_z, vertices[1].v_over_z, vertices[2].v_over_z);
    t.texture = texture;
    t.repeat = engine->texture_warp_mode == KEK_TEXTURE_WARP_REPEAT;
    /* Under REPEAT v times the width wraps at 2^32 as the texture does only
       while width * height divides 65536. */
    t.masked = log2_w >= 0 && log2_h >= 0 && log2_w + log2_h <= 16;
    t.u_mask = (uint32_t)texture->width - 1u;
    t.v_mask = ((uint32_t)texture->height - 1u) * (uint32_t)texture->width;
    t.u_scale = (float)texture->width;
    t.v_scale = (float)texture->height * (float)texture->width;
    t.u_most = (int64_t)texture->width * 65536 - 32768;
    t.v_most = ((int64_t)texture->height * 65536 - 32768) * (int64_t)texture->width;
    t.span_inv_z = t.c.inv_z.dx * (float)KEK_3D_SPAN;
    t.span_u_over_z = t.u_over_z.dx * (float)KEK_3D_SPAN;
    t.span_v_over_z = t.v_over_z.dx * (float)KEK_3D_SPAN;
    t.fast32 = t.masked && KEK_3D_PERSPECTIVE_FAST32 && kek_3d_texels_within_(vertices, texture, KEK_3D_PERSPECTIVE_TEXELS);
    t.width = (uint32_t)texture->width;
    t.v_rows = (float)texture->height;
    if (t.repeat) {
        t.u_lo = t.v_lo = INT32_MIN;
        t.u_hi = t.v_hi = INT32_MAX;
    } else if (t.fast32) {
        t.u_lo = t.v_lo = 0;
        t.u_hi = (int32_t)texture->width * 65536 - 32768;
        t.v_hi = (int32_t)texture->height * 65536 - 32768;
    }
    kek_3d_walk(engine, &t.c.setup, kek_3d_textured_span, &t);
}

/* Public entry points validate integer coordinates once. Internal model
   drawing has already checked its projections, so it uses the bounded paths
   directly and leaves the rasteriser's setup and pixel loops unchanged. */
void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color) {
    if (kek_3d_screen_bounded_(vertices)) kek_3d_triangle_bounded_(engine, vertices, color);
}

void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    if (kek_3d_screen_bounded_(vertices)) kek_3d_triangle_textured_bounded_(engine, vertices, texture);
}

void kek_3d_triangle_border(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill, uint8_t color_border) {
    KEK_IVec2 screen_vertices[3];
    if (!kek_3d_screen_bounded_(vertices)) return;

    kek_3d_triangle_bounded_(engine, vertices, color_fill);
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
            double t = ((double)near - a.z) / ((double)b.z - a.z);
            out_v[n].x  = (float)((double)a.x + t * ((double)b.x - a.x));
            out_v[n].y  = (float)((double)a.y + t * ((double)b.y - a.y));
            out_v[n].z  = near;
            out_uv[n].x = (float)((double)ua.x + t * ((double)ub.x - ua.x));
            out_uv[n].y = (float)((double)ua.y + t * ((double)ub.y - ua.y));
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

/* cross(b - a, c - a), in view space: it points out of the face, the way the
   winding the backface cull keeps faces. A model stores no normals, since
   this is all flat shading needs of one. */
/* Scale before narrowing so both the normal and its squared length fit.
   Only used when float cross products or their squared length overflow. */
static KEK_FVec3 kek_3d_face_normal_wide_(const KEK_FVec3 v[3]) {
    double bx = (double)v[1].x - v[0].x, by = (double)v[1].y - v[0].y, bz = (double)v[1].z - v[0].z;
    double cx = (double)v[2].x - v[0].x, cy = (double)v[2].y - v[0].y, cz = (double)v[2].z - v[0].z;
    double nx = by * cz - bz * cy, ny = bz * cx - bx * cz, nz = bx * cy - by * cx;
    double scale = fabs(nx);
    if (fabs(ny) > scale) scale = fabs(ny);
    if (fabs(nz) > scale) scale = fabs(nz);
    if (scale == 0.) return (KEK_FVec3){0.f, 0.f, 0.f};
    return (KEK_FVec3){(float)(nx / scale), (float)(ny / scale), (float)(nz / scale)};
}

static KEK_FVec3 kek_3d_face_normal(const KEK_FVec3 v[3]) {
    float abx = v[1].x - v[0].x, aby = v[1].y - v[0].y, abz = v[1].z - v[0].z;
    float acx = v[2].x - v[0].x, acy = v[2].y - v[0].y, acz = v[2].z - v[0].z;

    KEK_FVec3 normal = {aby * acz - abz * acy, abz * acx - abx * acz, abx * acy - aby * acx};
    if (!(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z <= FLT_MAX)) {
        return kek_3d_face_normal_wide_(v);
    }

    return normal;
}

/* The face's own shade in levels, from its normal. Both vectors are in view
   space: the camera turns the light and the face alike, so this is the same
   dot product as in the world. */
static float kek_3d_face_shade(KEK_FVec3 normal, KEK_FVec3 light_view, float ambient) {
    float nx = normal.x, ny = normal.y, nz = normal.z;
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

/* The fog at one view depth, in levels, which a vertex adds to its face's
   shade. Per vertex, after the near clip, so a face that runs into the
   distance darkens along its length; the rasteriser interpolates it in screen
   space, without the perspective correction the UVs get — at four levels and
   a dither nobody sees the difference. */
static float kek_3d_fog(const KEK_light* light, float z) {
    if (light->fog_end > light->fog_start) {
        float fog = (z - light->fog_start) / (light->fog_end - light->fog_start);
        if (fog > 0.f) {
            return (fog < 1.f ? fog : 1.f) * (float)(KEK_PALETTE_SHADING_LEVELS - 1);
        }
    }
    return 0.f;
}

/* A vertex in front of the near plane, projected once for every face that
   shares it rather than once per face. */
typedef struct KEK_3D_Projected_ {
    KEK_IVec2 screen;
    float inv_z;
    float fog;
} KEK_3D_Projected_;

static int kek_3d_screen_visible_(const KEK_engine* e, KEK_IVec2 sv[3]) {
    return !((sv[0].x < 0 && sv[1].x < 0 && sv[2].x < 0) ||
        (sv[0].x >= e->w && sv[1].x >= e->w && sv[2].x >= e->w) ||
        (sv[0].y < 0 && sv[1].y < 0 && sv[2].y < 0) ||
        (sv[0].y >= e->h && sv[1].y >= e->h && sv[2].y >= e->h))
        && kek_area_triangle_signed(sv) >= 1.f;
}

/* The model's byte coordinates are in [0, 255]. Test the transformed box
   against a view-space plane using its exact support radius along that plane.
   A small tolerance keeps float roundoff from rejecting a touching box. */
static int kek_3d_model_outside_plane_(const KEK_Mat3* to_view, KEK_FVec3 center, KEK_FVec3 normal) {
    const float half = 127.5f;
    float distance, support, tolerance;

    if (!kek_3d_finite_(center)) return 0;

    distance = normal.x * center.x + normal.y * center.y + normal.z * center.z;
    support = half * (fabsf(normal.x * to_view->m[0] + normal.y * to_view->m[3] + normal.z * to_view->m[6])
                    + fabsf(normal.x * to_view->m[1] + normal.y * to_view->m[4] + normal.z * to_view->m[7])
                    + fabsf(normal.x * to_view->m[2] + normal.y * to_view->m[5] + normal.z * to_view->m[8]));
    if (!(distance == distance && support >= 0.f && support <= FLT_MAX)) return 0;
    tolerance = 1.e-5f * (fabsf(distance) + support + 1.f);
    return distance + support < -tolerance;
}

/* A cheap conservative reject before transforming every vertex. The expanded
   left/top planes account for float-to-int truncation of slightly negative
   screen coordinates on the all-in-front path. */
#ifndef KEK_3D_MODEL_FRUSTUM_CULL
#define KEK_3D_MODEL_FRUSTUM_CULL 1
#endif
static int kek_3d_model_outside_frustum_(const KEK_3D_Lens_* lens, const KEK_camera* camera,
                                        const KEK_Mat3* to_view, KEK_FVec3 origin, KEK_engine* e) {
    KEK_FVec3 left = {lens->focal_length, 0.f, lens->aspect_ratio * (1.f + 2.f / (float)e->w)};
    KEK_FVec3 right = {-lens->focal_length, 0.f, lens->aspect_ratio};
    KEK_FVec3 top = {0.f, -lens->focal_length, 1.f + 2.f / (float)e->h};
    KEK_FVec3 bottom = {0.f, lens->focal_length, 1.f};
    KEK_FVec3 center = kek_mat3_apply(*to_view, (KEK_FVec3){127.5f, 127.5f, 127.5f});
    float z_radius, z_tolerance;

    center.x += origin.x;
    center.y += origin.y;
    center.z += origin.z;
    if (!kek_3d_finite_(center)) return 0;
    z_radius = 127.5f * (fabsf(to_view->m[6]) + fabsf(to_view->m[7]) + fabsf(to_view->m[8]));
    if (!(z_radius >= 0.f && z_radius <= FLT_MAX)) return 0;
    z_tolerance = 1.e-5f * (fabsf(center.z) + z_radius + camera->far_plane + 1.f);
    if (center.z + z_radius < camera->near_plane - z_tolerance
        || center.z - z_radius > camera->far_plane + z_tolerance) return 1;

    return kek_3d_model_outside_plane_(to_view, center, left)
        || kek_3d_model_outside_plane_(to_view, center, right)
        || kek_3d_model_outside_plane_(to_view, center, top)
        || kek_3d_model_outside_plane_(to_view, center, bottom);
}

/* Clipped triangles still need the screen-space winding/coverage check. */
static void kek_3d_draw_triangle_(KEK_engine* e, KEK_3D_ProjectedVertex pv[3], const KEK_texture* texture,
                                  uint8_t color) {
    KEK_IVec2 sv[3] = {pv[0].screen, pv[1].screen, pv[2].screen};
    if (!kek_3d_screen_visible_(e, sv)) return;
    if (texture) {
        kek_3d_triangle_textured_bounded_(e, pv, texture);
    } else {
        kek_3d_triangle_bounded_(e, pv, color);
    }
}

/* Rare path: a triangle clipped by five planes has at most eight corners.
   Keep intersections in double until projection, including UVs: narrowing
   between planes loses the visible sliver of a very large triangle. */
typedef struct KEK_3D_ClipVertex_ { double x, y, z, u, v; } KEK_3D_ClipVertex_;

static double kek_3d_clip_distance_(KEK_3D_ClipVertex_ v, int plane,
                                   const KEK_3D_Lens_* lens, float near) {
    switch (plane) {
        case 0: return v.z - near;
        case 1: return v.z * lens->aspect_ratio + v.x * lens->focal_length;
        case 2: return v.z * lens->aspect_ratio - v.x * lens->focal_length;
        case 3: return v.z + v.y * lens->focal_length;
        default: return v.z - v.y * lens->focal_length;
    }
}

static void kek_3d_clip_sides_(KEK_engine* e, const KEK_3D_Lens_* lens, float near,
                               const KEK_FVec3 v[3], const KEK_FVec2 uv[3],
                               float shade, const KEK_texture* texture, uint8_t color) {
    KEK_3D_ClipVertex_ buffers[2][8];
    KEK_3D_ClipVertex_* in = buffers[0];
    KEK_3D_ClipVertex_* out = buffers[1];
    KEK_3D_ProjectedVertex projected[8];
    int count = 3, plane, i;
    for (i = 0; i < 3; ++i) {
        in[i] = (KEK_3D_ClipVertex_){v[i].x, v[i].y, v[i].z, uv[i].x, uv[i].y};
    }
    for (plane = 0; plane < 5 && count >= 3; ++plane) {
        int n = 0;
        KEK_3D_ClipVertex_* swap;
        for (i = 0; i < count; ++i) {
            KEK_3D_ClipVertex_ a = in[i], b = in[(i + 1) % count];
            double da = kek_3d_clip_distance_(a, plane, lens, near);
            double db = kek_3d_clip_distance_(b, plane, lens, near);
            if (da >= 0.) {
                if (n == 8) return;
                out[n++] = a;
            }
            if ((da < 0. && db > 0.) || (da > 0. && db < 0.)) {
                double t = da / (da - db);
                KEK_3D_ClipVertex_ c = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                    a.z + t * (b.z - a.z), a.u + t * (b.u - a.u), a.v + t * (b.v - a.v)};
                /* Snap the intersected component to the plane. */
                switch (plane) {
                    case 0: c.z = near; break;
                    case 1: c.x = -c.z * lens->aspect_ratio / lens->focal_length; break;
                    case 2: c.x = c.z * lens->aspect_ratio / lens->focal_length; break;
                    case 3: c.y = -c.z / lens->focal_length; break;
                    default: c.y = c.z / lens->focal_length; break;
                }
                if (n == 8) return;
                out[n++] = c;
            }
        }
        count = n;
        swap = in; in = out; out = swap;
    }
    for (i = 0; i < count; ++i) {
        double inv_z = 1. / in[i].z;
        double x = (in[i].x * lens->focal_length * inv_z / lens->aspect_ratio + 1.) * lens->half_w;
        double y = (1. - in[i].y * lens->focal_length * inv_z) * lens->half_h;
        if (!(x >= -KEK_3D_SCREEN_LIMIT_ && x <= KEK_3D_SCREEN_LIMIT_
              && y >= -KEK_3D_SCREEN_LIMIT_ && y <= KEK_3D_SCREEN_LIMIT_) || !(inv_z > 0. && inv_z <= FLT_MAX)) return;
        projected[i] = (KEK_3D_ProjectedVertex){
            {(int)x, (int)y}, (float)in[i].z, (float)inv_z,
            texture ? (float)(in[i].u * inv_z) : 0.f,
            texture ? (float)(in[i].v * inv_z) : 0.f,
            shade + kek_3d_fog(&e->light, (float)in[i].z)};
    }
    for (i = 1; i + 1 < count; ++i) {
        KEK_3D_ProjectedVertex triangle[3] = {projected[0], projected[i], projected[i + 1]};
        kek_3d_draw_triangle_(e, triangle, texture, color);
    }
}

void kek_3d_draw_model(KEK_engine *e, KEK_model *mdl, KEK_camera *camera, KEK_FVec3 pos, KEK_FVec3 rotation) {
    /* The view-space vertices and their projections are temporaries off the
       top of the arena, as many as this model has, gone again at the end of
       the call. */
    size_t mark = kek_arena_temp_mark(&e->arena);
    KEK_FVec3* view_verts;
    KEK_3D_Projected_* projected;
    char use_colors = mdl->face_colors != 0 && mdl->colors_count > 0;
    /* Resolved once per model, not once per face: a stale handle just means
       the model draws untextured. */
    KEK_texture* texture = kek_texture_get(e, mdl->texture);
    /* Fix 4: precompute rotation matrices and projection constants once per call */
    KEK_Mat3 model_rot, camera_rot, to_view;
    KEK_FVec3 light_view, origin;
    KEK_3D_Lens_ lens;
    float near = camera->near_plane;
    uint32_t i;
    int row;

    if (!kek_3d_camera_valid_(camera) || !kek_3d_finite_(pos) || !kek_3d_finite_(rotation)
        || !kek_3d_finite_(mdl->scale) || !kek_3d_finite_(mdl->offset)) return;

    model_rot    = kek_mat3_from_euler(rotation);
    camera_rot = kek_3d_view_rotation_(camera->rotation);
    light_view   = kek_mat3_apply(camera_rot, e->light.direction);
    lens = kek_3d_lens_(e, camera);
    if (!(lens.focal_length > 0.f && lens.focal_length <= FLT_MAX)) {
        kek_arena_temp_release(&e->arena, mark);
        return;
    }

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

    /* Large models wholly outside the view can be dropped before allocating
       temporary vertex arrays or transforming every vertex. */
#if KEK_3D_MODEL_FRUSTUM_CULL
    if (mdl->verts_count >= 16 && kek_3d_model_outside_frustum_(&lens, camera, &to_view, origin, e)) {
        kek_arena_temp_release(&e->arena, mark);
        return;
    }
#endif

    /* A model the arena has no room to transform is not drawn. */
    view_verts = (KEK_FVec3*)kek_arena_temp(&e->arena, (size_t)mdl->verts_count * sizeof(KEK_FVec3));
    projected = (KEK_3D_Projected_*)kek_arena_temp(&e->arena, (size_t)mdl->verts_count * sizeof(KEK_3D_Projected_));
    if (!view_verts || !projected) {
        kek_arena_temp_release(&e->arena, mark);
        return;
    }

    for (i = 0; i < mdl->verts_count; ++i) {
        KEK_model_vertex q = mdl->verts[i];
        float x = (float)q.x, y = (float)q.y, z = (float)q.z;
        /* Keep the fixed matrix in this loop instead of passing its nine
           floats by value through an external call for every vertex. */
        KEK_FVec3 v = {
            to_view.m[0] * x + to_view.m[1] * y + to_view.m[2] * z + origin.x,
            to_view.m[3] * x + to_view.m[4] * y + to_view.m[5] * z + origin.y,
            to_view.m[6] * x + to_view.m[7] * y + to_view.m[8] * z + origin.z
        };
        if (!kek_3d_finite_(v)) {
            kek_arena_temp_release(&e->arena, mark);
            return;
        }
        view_verts[i] = v;
        projected[i].inv_z = 0.f;
        if (v.z > near) {
            KEK_FVec3 p = kek_3d_project_float_(&lens, v);
            kek_3d_project_screen_(p, &projected[i].screen, &projected[i].inv_z);
            projected[i].fog = kek_3d_fog(&e->light, v.z);
        }
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
        KEK_FVec3 normal;
        KEK_3D_ProjectedVertex pv[3], clipped[KEK_3D_CLIP_NEAR_MAX_VERTS];
        const KEK_texture* face_texture;
        float face_shade;
        int cn, tri_count, t, k, projected_face;

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

        if (fv[0].z <= near && fv[1].z <= near && fv[2].z <= near) continue;

        /* Far-plane cull: skip entirely if all vertices are beyond far plane */
        if (fv[0].z >= camera->far_plane &&
            fv[1].z >= camera->far_plane &&
            fv[2].z >= camera->far_plane) {
            continue;
        }

        projected_face = projected[face.a].inv_z > 0.f && projected[face.b].inv_z > 0.f
            && projected[face.c].inv_z > 0.f;
        if (projected_face) {
            KEK_IVec2 screen[3] = {projected[face.a].screen, projected[face.b].screen, projected[face.c].screen};
            if (!kek_3d_screen_visible_(e, screen)) continue;
        }

        /* Keep the view-space cull too: integer projection can give a thin
           back face a different winding. Double keeps the dot finite for
           faces awaiting clipping; ordinary faces retain float arithmetic. */
        normal = kek_3d_face_normal(fv);
        if (projected_face
            ? normal.x * fv[0].x + normal.y * fv[0].y + normal.z * fv[0].z >= 0.f
            : (double)normal.x * fv[0].x + (double)normal.y * fv[0].y + (double)normal.z * fv[0].z >= 0.) {
            continue;
        }
        face_shade = kek_3d_face_shade(normal, light_view, e->light.ambient);
        face_texture = face_is_textured ? texture : 0;

        if (face_is_textured) {
            fuv[0] = mdl->uvs[face_uv.a]; fuv[1] = mdl->uvs[face_uv.b]; fuv[2] = mdl->uvs[face_uv.c];
        } else {
            fuv[0] = fuv[1] = fuv[2] = (KEK_FVec2){0.f, 0.f};
        }

        /* Wholly in front of the near plane, which is most faces: the
           vertices were projected already. */
        if (projected_face) {
            const uint16_t corner[3] = { face.a, face.b, face.c };
            for (k = 0; k < 3; ++k) {
                const KEK_3D_Projected_* p = &projected[corner[k]];
                pv[k].screen   = p->screen;
                pv[k].depth    = fv[k].z;
                pv[k].inv_z    = p->inv_z;
                pv[k].u_over_z = face_is_textured ? fuv[k].x * p->inv_z : 0.f;
                pv[k].v_over_z = face_is_textured ? fuv[k].y * p->inv_z : 0.f;
                pv[k].shade    = face_shade + p->fog;
            }
            if (face_texture) {
                kek_3d_triangle_textured_bounded_(e, pv, face_texture);
            } else {
                kek_3d_triangle_bounded_(e, pv, color);
            }
            continue;
        }

        if ((fv[0].z > near && projected[face.a].inv_z == 0.f)
            || (fv[1].z > near && projected[face.b].inv_z == 0.f)
            || (fv[2].z > near && projected[face.c].inv_z == 0.f)) {
            kek_3d_clip_sides_(e, &lens, near, fv, fuv, face_shade, face_texture, color);
            continue;
        }

        /* Fix 3: clip triangle against near plane in view space */
        cn = kek_3d_clip_near(fv, fuv, near, cv, cuv);
        if (cn < 3) continue;

        for (k = 0; k < cn; ++k) {
            if (!kek_3d_project_(&lens, cv[k], &clipped[k].screen, &clipped[k].inv_z)) break;
            clipped[k].depth = cv[k].z;
            clipped[k].u_over_z = face_is_textured ? cuv[k].x * clipped[k].inv_z : 0.f;
            clipped[k].v_over_z = face_is_textured ? cuv[k].y * clipped[k].inv_z : 0.f;
            clipped[k].shade = face_shade + kek_3d_fog(&e->light, cv[k].z);
        }
        if (k != cn) {
            kek_3d_clip_sides_(e, &lens, near, fv, fuv, face_shade, face_texture, color);
            continue;
        }

        /* Rasterize as a triangle fan (1 tri for cn==3, 2 tris for cn==4) */
        tri_count = cn - 2;
        for (t = 0; t < tri_count; ++t) {
            pv[0] = clipped[0];
            pv[1] = clipped[t + 1];
            pv[2] = clipped[t + 2];
            kek_3d_draw_triangle_(e, pv, face_texture, color);
        }
    }

    kek_arena_temp_release(&e->arena, mark);
}
