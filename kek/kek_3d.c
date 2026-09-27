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

void kek_3d_triangle(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], uint8_t color_fill) {
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
   sixteen pixels. Within a span the texture is affine: at this resolution,
   and with a span this short, the error is a fraction of a texel. */
#define KEK_3D_SPAN 16

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
static const uint32_t KEK_3D_RECIPROCAL[KEK_3D_SPAN + 1] = {
    0, KEK_3D_RECIPROCAL_(1), KEK_3D_RECIPROCAL_(2), KEK_3D_RECIPROCAL_(3), KEK_3D_RECIPROCAL_(4),
    KEK_3D_RECIPROCAL_(5), KEK_3D_RECIPROCAL_(6), KEK_3D_RECIPROCAL_(7), KEK_3D_RECIPROCAL_(8),
    KEK_3D_RECIPROCAL_(9), KEK_3D_RECIPROCAL_(10), KEK_3D_RECIPROCAL_(11), KEK_3D_RECIPROCAL_(12),
    KEK_3D_RECIPROCAL_(13), KEK_3D_RECIPROCAL_(14), KEK_3D_RECIPROCAL_(15), KEK_3D_RECIPROCAL_(16)
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
    if (t->masked) {
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

void kek_3d_triangle_textured(KEK_engine* engine, KEK_3D_ProjectedVertex vertices[3], const KEK_texture* texture) {
    KEK_3D_Textured_ t;
    int log2_w = kek_3d_log2(texture->width);
    int log2_h = kek_3d_log2(texture->height);

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
    kek_3d_walk(engine, &t.c.setup, kek_3d_textured_span, &t);
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

/* cross(b - a, c - a), in view space: it points out of the face, the way the
   winding the backface cull keeps faces. A model stores no normals, since
   this is all flat shading needs of one. */
static KEK_FVec3 kek_3d_face_normal(const KEK_FVec3 v[3]) {
    float abx = v[1].x - v[0].x, aby = v[1].y - v[0].y, abz = v[1].z - v[0].z;
    float acx = v[2].x - v[0].x, acy = v[2].y - v[0].y, acz = v[2].z - v[0].z;

    return (KEK_FVec3){
        aby * acz - abz * acy,
        abz * acx - abx * acz,
        abx * acy - aby * acx
    };
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

/* How the camera maps view space to the frame. */
typedef struct KEK_3D_Lens_ {
    float focal_length, aspect_ratio, half_w, half_h;
} KEK_3D_Lens_;

/* A point in front of the near plane onto the frame. */
static void kek_3d_project_(const KEK_3D_Lens_* lens, KEK_FVec3 v, KEK_IVec2* out_screen, float* out_inv_z) {
    float inv_z = 1.f / v.z;

    out_screen->x = (int)((v.x * lens->focal_length * inv_z / lens->aspect_ratio + 1.f) * lens->half_w);
    out_screen->y = (int)((1.f - v.y * lens->focal_length * inv_z) * lens->half_h);
    *out_inv_z = inv_z;
}

/* A vertex in front of the near plane, projected once for every face that
   shares it rather than once per face. */
typedef struct KEK_3D_Projected_ {
    KEK_IVec2 screen;
    float inv_z;
    float fog;
} KEK_3D_Projected_;

/* One triangle of a face on to the rasteriser: culled if it is wholly off one
   side of the frame, or back-facing or thinner than a pixel on screen. */
static void kek_3d_draw_triangle_(KEK_engine* e, KEK_3D_ProjectedVertex pv[3], const KEK_texture* texture,
                                  uint8_t color) {
    KEK_IVec2 sv[3];

    sv[0] = pv[0].screen;
    sv[1] = pv[1].screen;
    sv[2] = pv[2].screen;
    if ((sv[0].x < 0 && sv[1].x < 0 && sv[2].x < 0) ||
        (sv[0].x >= e->w && sv[1].x >= e->w && sv[2].x >= e->w) ||
        (sv[0].y < 0 && sv[1].y < 0 && sv[2].y < 0) ||
        (sv[0].y >= e->h && sv[1].y >= e->h && sv[2].y >= e->h)) {
        return;
    }
    if (kek_area_triangle_signed(sv) < 1) {
        return;
    }
    if (texture) {
        kek_3d_triangle_textured(e, pv, texture);
    } else {
        kek_3d_triangle(e, pv, color);
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

    /* A model the arena has no room to transform is not drawn. */
    view_verts = (KEK_FVec3*)kek_arena_temp(&e->arena, (size_t)mdl->verts_count * sizeof(KEK_FVec3));
    projected = (KEK_3D_Projected_*)kek_arena_temp(&e->arena, (size_t)mdl->verts_count * sizeof(KEK_3D_Projected_));
    if (!view_verts || !projected) {
        kek_arena_temp_release(&e->arena, mark);
        return;
    }

    model_rot    = kek_mat3_from_euler(rotation);
    camera_rot   = kek_mat3_from_euler((KEK_FVec3){
                       -camera->rotation.x,
                       -camera->rotation.y,
                       -camera->rotation.z });
    light_view   = kek_mat3_apply(camera_rot, e->light.direction);
    lens.aspect_ratio = (float)e->w / (float)e->h;
    lens.focal_length = 1.f / tanf(camera->fov * KEK_PI / 360.f);
    lens.half_w       = (float)e->w * 0.5f;
    lens.half_h       = (float)e->h * 0.5f;

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
        KEK_FVec3 v = kek_3d_translate(
            kek_mat3_apply(to_view, (KEK_FVec3){ (float)q.x, (float)q.y, (float)q.z }), origin);
        view_verts[i] = v;
        if (v.z > near) {
            kek_3d_project_(&lens, v, &projected[i].screen, &projected[i].inv_z);
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
        KEK_3D_ProjectedVertex pv[3];
        const KEK_texture* face_texture;
        float face_shade;
        int cn, tri_count, t, k;

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

        /* The eye, at the origin, behind the face's plane: a back face, which
           the camera cannot see whatever the clip and the projection make of
           it, so it is dropped before it costs a square root. The screen-space
           test after projection stays, for faces thinner than a pixel. */
        normal = kek_3d_face_normal(fv);
        if (normal.x * fv[0].x + normal.y * fv[0].y + normal.z * fv[0].z >= 0.f) {
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
        if (fv[0].z > near && fv[1].z > near && fv[2].z > near) {
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
            kek_3d_draw_triangle_(e, pv, face_texture, color);
            continue;
        }

        /* Fix 3: clip triangle against near plane in view space */
        cn = kek_3d_clip_near(fv, fuv, near, cv, cuv);
        if (cn < 3) continue;

        /* Rasterize as a triangle fan (1 tri for cn==3, 2 tris for cn==4) */
        tri_count = cn - 2;
        for (t = 0; t < tri_count; ++t) {
            int idxs[3];

            idxs[0] = 0; idxs[1] = t + 1; idxs[2] = t + 2;

            /* Project each clipped vertex */
            for (k = 0; k < 3; ++k) {
                int vi = idxs[k];
                float inv_z;
                kek_3d_project_(&lens, cv[vi], &pv[k].screen, &inv_z);
                pv[k].depth    = cv[vi].z;
                pv[k].inv_z    = inv_z;
                pv[k].u_over_z = face_is_textured ? cuv[vi].x * inv_z : 0.f;
                pv[k].v_over_z = face_is_textured ? cuv[vi].y * inv_z : 0.f;
                pv[k].shade    = face_shade + kek_3d_fog(&e->light, cv[vi].z);
            }
            kek_3d_draw_triangle_(e, pv, face_texture, color);
        }
    }

    kek_arena_temp_release(&e->arena, mark);
}
