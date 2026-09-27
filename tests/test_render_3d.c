/* The 3D rasteriser and depth test against a guarded frame. As in
   test_render_2d, nothing here compares pixels to a reference picture; the
   assertions are the ones float cannot move: nothing outside the frame, the
   nearer surface wins whatever the draw order, what is off screen or behind
   the camera draws nothing, and what covers the screen covers all of it. */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_3d.h"
#include "kek_model.h"
#include "kek_texture.h"
#include "test_support.h"

static KEK_engine e;

#define NEAR_INK 2
#define FAR_INK 5

void setUp(void) {
    kek_test_init(&e);
    kek_test_frame_attach(&e, KEK_TEST_WIDTH, KEK_TEST_HEIGHT);
}

void tearDown(void) {
}

static KEK_3D_ProjectedVertex vertex(int x, int y, float depth) {
    KEK_3D_ProjectedVertex v;
    v.screen.x = x;
    v.screen.y = y;
    v.depth = depth;
    v.inv_z = 1.f / depth;
    /* UV = screen position / 64, carried as u/z and v/z like the real
       pipeline, so a textured triangle samples well outside [0, 1]. */
    v.u_over_z = ((float)x / 64.f) * v.inv_z;
    v.v_over_z = ((float)y / 64.f) * v.inv_z;
    v.shade = 0.f;
    return v;
}

static void flat(KEK_3D_ProjectedVertex a, KEK_3D_ProjectedVertex b, KEK_3D_ProjectedVertex c, uint8_t color) {
    KEK_3D_ProjectedVertex v[3];
    v[0] = a;
    v[1] = b;
    v[2] = c;
    kek_3d_triangle(&e, v, color);
}

static void textured(KEK_3D_ProjectedVertex a, KEK_3D_ProjectedVertex b, KEK_3D_ProjectedVertex c) {
    KEK_3D_ProjectedVertex v[3];
    v[0] = a;
    v[1] = b;
    v[2] = c;
    kek_3d_triangle_textured(&e, v, kek_texture_get(&e, kek_default_texture_handle(&e)));
}

/* Two triangles over the same ground, one at depth 2 and one at depth 10. */
static void near_square(uint8_t color) {
    flat(vertex(40, 30, 2.f), vertex(140, 30, 2.f), vertex(40, 130, 2.f), color);
    flat(vertex(140, 30, 2.f), vertex(140, 130, 2.f), vertex(40, 130, 2.f), color);
}

static void far_square(uint8_t color) {
    flat(vertex(40, 30, 10.f), vertex(140, 30, 10.f), vertex(40, 130, 10.f), color);
    flat(vertex(140, 30, 10.f), vertex(140, 130, 10.f), vertex(40, 130, 10.f), color);
}

/* ---- Depth ---- */

void test_the_nearer_surface_wins_drawn_last(void) {
    far_square(FAR_INK);
    near_square(NEAR_INK);

    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_count(&e, NEAR_INK));
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_count(&e, FAR_INK));
    kek_test_frame_assert_guards();
}

void test_the_nearer_surface_wins_drawn_first(void) {
    near_square(NEAR_INK);
    far_square(FAR_INK);

    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_count(&e, NEAR_INK));
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_count(&e, FAR_INK));
    kek_test_frame_assert_guards();
}

/* Only where they overlap: the far surface still shows around a nearer one
   that covers part of it. */
void test_the_far_surface_shows_where_nothing_nearer_is(void) {
    flat(vertex(60, 50, 2.f), vertex(80, 50, 2.f), vertex(60, 70, 2.f), NEAR_INK);
    far_square(FAR_INK);

    TEST_ASSERT_EQUAL_UINT8(NEAR_INK, kek_test_frame_pixel(&e, 62, 52));
    TEST_ASSERT_EQUAL_UINT8(FAR_INK, kek_test_frame_pixel(&e, 120, 110));
    TEST_ASSERT_EQUAL_UINT8(0, kek_test_frame_pixel(&e, 200, 150));
    kek_test_frame_assert_guards();
}

/* The depth buffer is 1/z in 16 bits: past its range a surface is as far as a
   pixel can be, which is still nearer than the clear. */
static void square_at(float depth, uint8_t color) {
    flat(vertex(40, 30, depth), vertex(140, 30, depth), vertex(40, 130, depth), color);
    flat(vertex(140, 30, depth), vertex(140, 130, depth), vertex(40, 130, depth), color);
}

void test_a_surface_past_the_depth_range_still_draws(void) {
    square_at(1.e6f, FAR_INK);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_count(&e, FAR_INK));
    kek_test_frame_assert_guards();
}

/* Nearer than the near plane saturates rather than wrapping round to far. */
void test_a_surface_nearer_than_the_depth_range_stays_nearest(void) {
    square_at(0.001f, NEAR_INK);
    square_at(2.f, FAR_INK);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_count(&e, NEAR_INK));
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_count(&e, FAR_INK));
    kek_test_frame_assert_guards();
}

void test_a_vertex_blit_respects_depth_and_the_frame(void) {
    near_square(NEAR_INK);

    kek_3d_blit_vertex(&e, vertex(50, 50, 10.f), FAR_INK);
    TEST_ASSERT_EQUAL_UINT8(NEAR_INK, kek_test_frame_pixel(&e, 50, 50));
    kek_3d_blit_vertex(&e, vertex(50, 50, 1.f), FAR_INK);
    TEST_ASSERT_EQUAL_UINT8(FAR_INK, kek_test_frame_pixel(&e, 50, 50));

    kek_3d_blit_vertex(&e, vertex(-1, 50, 1.f), FAR_INK);
    kek_3d_blit_vertex(&e, vertex(e.w, 50, 1.f), FAR_INK);
    kek_3d_blit_vertex(&e, vertex(50, -1, 1.f), FAR_INK);
    kek_3d_blit_vertex(&e, vertex(50, e.h, 1.f), FAR_INK);
    TEST_ASSERT_EQUAL_INT(1, kek_test_frame_count(&e, FAR_INK));
    kek_test_frame_assert_guards();
}

/* ---- Coverage and bounds ---- */

void test_triangles_wholly_off_each_edge_draw_nothing(void) {
    int w = e.w, h = e.h;

    flat(vertex(10, -300, 2.f), vertex(w - 10, -250, 2.f), vertex(w / 2, -200, 2.f), NEAR_INK);
    flat(vertex(10, h + 200, 2.f), vertex(w / 2, h + 300, 2.f), vertex(w - 10, h + 250, 2.f), NEAR_INK);
    flat(vertex(-300, 5, 2.f), vertex(-250, h - 5, 2.f), vertex(-200, h / 2, 2.f), NEAR_INK);
    flat(vertex(w + 200, 5, 2.f), vertex(w + 250, h - 5, 2.f), vertex(w + 300, h / 2, 2.f), NEAR_INK);
    textured(vertex(10, -300, 2.f), vertex(w - 10, -250, 2.f), vertex(w / 2, -200, 2.f));
    textured(vertex(w + 200, 5, 2.f), vertex(w + 250, h - 5, 2.f), vertex(w + 300, h / 2, 2.f));

    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

void test_triangles_hanging_off_every_edge_stay_in_the_frame(void) {
    const int cx[3] = { 0, e.w / 2, e.w - 1 };
    const int cy[3] = { 0, e.h / 2, e.h - 1 };
    int i, j;

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            int x = cx[i], y = cy[j];
            flat(vertex(x - 40, y - 30, 3.f), vertex(x + 50, y - 10, 4.f), vertex(x - 10, y + 45, 5.f), NEAR_INK);
            flat(vertex(x - 40, y - 30, 3.f), vertex(x - 10, y + 45, 5.f), vertex(x + 50, y - 10, 4.f), NEAR_INK);
            textured(vertex(x + 40, y + 30, 3.f), vertex(x - 50, y + 10, 4.f), vertex(x + 10, y - 45, 5.f));
        }
    }
    kek_test_frame_assert_guards();
}

void test_degenerate_triangles_stay_in_the_frame(void) {
    int w = e.w, h = e.h;

    flat(vertex(5, 5, 2.f), vertex(5, 5, 2.f), vertex(5, 5, 2.f), NEAR_INK);
    flat(vertex(-40, 7, 2.f), vertex(w / 2, 7, 2.f), vertex(w + 40, 7, 2.f), NEAR_INK);
    flat(vertex(-40, -40, 2.f), vertex(20, 20, 2.f), vertex(w + 40, w + 40, 2.f), NEAR_INK);
    flat(vertex(w, h, 2.f), vertex(w, h, 2.f), vertex(w, h, 2.f), NEAR_INK);
    textured(vertex(-40, 7, 2.f), vertex(w / 2, 7, 2.f), vertex(w + 40, 7, 2.f));
    kek_test_frame_assert_guards();
}

/* Both windings: kek_3d_triangle takes either, and either way round the
   frame has to be covered. */
void test_a_triangle_larger_than_the_frame_fills_it(void) {
    int w = e.w, h = e.h;

    flat(vertex(-10 * w, -10 * h, 3.f), vertex(10 * w, -10 * h, 3.f), vertex(w / 2, 10 * h, 3.f), NEAR_INK);
    TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_count(&e, NEAR_INK));

    kek_test_frame_clear(&e);
    flat(vertex(-10 * w, -10 * h, 3.f), vertex(w / 2, 10 * h, 3.f), vertex(10 * w, -10 * h, 3.f), NEAR_INK);
    TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_count(&e, NEAR_INK));
    kek_test_frame_assert_guards();
}

/* The default texture has no index 0 anywhere, so a full frame of samples
   leaves nothing unpainted — under either wrap mode, with UVs running far
   past [0, 1] in both directions. */
void test_a_textured_triangle_larger_than_the_frame_fills_it_in_both_wrap_modes(void) {
    int w = e.w, h = e.h;
    int mode;

    for (mode = 0; mode < 2; ++mode) {
        kek_texture_set_warp_mode(&e, mode ? KEK_TEXTURE_WARP_REPEAT : KEK_TEXTURE_WARP_CLAMP);
        kek_test_frame_clear(&e);
        textured(vertex(-10 * w, -10 * h, 3.f), vertex(10 * w, -10 * h, 3.f), vertex(w / 2, 10 * h, 3.f));
        TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_painted(&e));
        kek_test_frame_assert_guards();
    }
}

/* The rows a triangle is walked in cover exactly the pixels a closed triangle
   contains: every edge function >= 0, or every one <= 0, worked out here in
   64-bit integers. Random triangles, some reaching far off the frame, each on
   a cleared frame and compared pixel for pixel over the whole of it. */
static uint32_t lcg_state;

static int lcg_range(int lo, int hi) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lo + (int)((lcg_state >> 8) % (uint32_t)(hi - lo + 1));
}

static int64_t edge(int ax, int ay, int bx, int by, int x, int y) {
    return (int64_t)(x - ax) * (by - ay) - (int64_t)(y - ay) * (bx - ax);
}

void test_rows_cover_exactly_the_pixels_of_the_closed_triangle(void) {
    int t, x, y;

    lcg_state = 12345u;
    for (t = 0; t < 400; ++t) {
        int reach = t % 4 == 0 ? 20000 : 400;
        int ax = lcg_range(-reach, e.w + reach), ay = lcg_range(-reach, e.h + reach);
        int bx = lcg_range(-40, e.w + 40), by = lcg_range(-40, e.h + 40);
        int cx = t % 3 == 0 ? bx + lcg_range(-3, 3) : lcg_range(-40, e.w + 40);
        int cy = t % 3 == 0 ? by + lcg_range(-60, 60) : lcg_range(-40, e.h + 40);

        kek_test_frame_clear(&e);
        flat(vertex(ax, ay, 3.f), vertex(bx, by, 3.f), vertex(cx, cy, 3.f), NEAR_INK);
        for (y = 0; y < e.h; ++y) {
            for (x = 0; x < e.w; ++x) {
                int64_t w0 = edge(bx, by, cx, cy, x, y);
                int64_t w1 = edge(cx, cy, ax, ay, x, y);
                int64_t w2 = edge(ax, ay, bx, by, x, y);
                int area = edge(ax, ay, bx, by, cx, cy) != 0;
                int inside = area && ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0));
                if (inside != (kek_test_frame_pixel(&e, x, y) == NEAR_INK)) {
                    char message[128];
                    (void)snprintf(message, sizeof(message), "triangle %d (%d,%d) (%d,%d) (%d,%d), pixel (%d,%d)",
                             t, ax, ay, bx, by, cx, cy, x, y);
                    TEST_FAIL_MESSAGE(message);
                }
            }
        }
    }
    kek_test_frame_assert_guards();
}

/* A side that is not a power of two leaves the span loop's masks out, and so
   do more than 65,536 texels; either way the texture goes through the sampler
   per pixel instead. Still every pixel, in both wrap modes, and nothing
   outside the frame. */
void test_a_texture_of_any_size_fills_the_frame_in_both_wrap_modes(void) {
    static const uint16_t sizes[2][2] = { { 3, 5 }, { 512, 256 } };
    int w = e.w, h = e.h;
    KEK_3D_ProjectedVertex v[3];
    size_t i;
    int mode;

    for (i = 0; i < 2; ++i) {
        KEK_TextureHandle handle = kek_texture_create(&e, sizes[i][0], sizes[i][1]);
        KEK_texture* texture = kek_texture_get(&e, handle);

        TEST_ASSERT_NOT_NULL(texture);
        memset(texture->data, 9, (size_t)sizes[i][0] * sizes[i][1]);
        for (mode = 0; mode < 2; ++mode) {
            kek_texture_set_warp_mode(&e, mode ? KEK_TEXTURE_WARP_REPEAT : KEK_TEXTURE_WARP_CLAMP);
            kek_test_frame_clear(&e);
            v[0] = vertex(-10 * w, -10 * h, 3.f);
            v[1] = vertex(10 * w, -10 * h, 3.f);
            v[2] = vertex(w / 2, 10 * h, 3.f);
            kek_3d_triangle_textured(&e, v, texture);
            TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_count(&e, 9));
            kek_test_frame_assert_guards();
        }
        kek_texture_destroy(&e, handle);
    }
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
}

/* The masked path wraps under REPEAT as the sampler does: a 256x256 texture,
   the most the masks take, drawn across many repeats with a different index
   in every texel, comes out the same pixel for pixel whichever whole number
   of repeats the UVs are shifted by. */
void test_repeat_wraps_the_same_whatever_repeat_it_starts_in(void) {
    static uint8_t first[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    KEK_TextureHandle handle = kek_texture_create(&e, 256, 256);
    KEK_texture* texture = kek_texture_get(&e, handle);
    const float shifts[3] = { 0.f, 3.f, -7.f };
    KEK_3D_ProjectedVertex v[3];
    size_t i, k;

    TEST_ASSERT_NOT_NULL(texture);
    for (i = 0; i < (size_t)256 * 256; ++i) {
        texture->data[i] = (uint8_t)(1 + (i * 7 + (i >> 8) * 13) % 255);
    }
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    for (k = 0; k < 3; ++k) {
        kek_test_frame_clear(&e);
        for (i = 0; i < 3; ++i) {
            v[i] = vertex(i == 1 ? 310 : 5, i == 2 ? 195 : 5, 2.f);
            v[i].u_over_z = ((float)(i == 1) * 2.25f + shifts[k]) * v[i].inv_z;
            v[i].v_over_z = ((float)(i == 2) * 1.75f - shifts[k]) * v[i].inv_z;
        }
        kek_3d_triangle_textured(&e, v, texture);
        if (k == 0) {
            memcpy(first, e.fb, sizeof(first));
            TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
        } else {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(first, e.fb, sizeof(first));
        }
    }
    kek_test_frame_assert_guards();
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    kek_texture_destroy(&e, handle);
}

/* Under CLAMP, UVs past the far corner sample the far corner's texel and
   nothing else, however far past it they are. */
void test_uvs_past_the_texture_sample_its_edge_under_clamp(void) {
    KEK_TextureHandle handle = kek_texture_create(&e, 4, 4);
    KEK_texture* texture = kek_texture_get(&e, handle);
    KEK_3D_ProjectedVertex v[3];
    int i;

    TEST_ASSERT_NOT_NULL(texture);
    for (i = 0; i < 16; ++i) {
        texture->data[i] = (uint8_t)(i + 1);
    }
    for (i = 0; i < 3; ++i) {
        v[i] = vertex(i == 1 ? 300 : 10, i == 2 ? 190 : 10, 3.f);
        v[i].u_over_z = (1.f + (float)i * 50.f) * v[i].inv_z;
        v[i].v_over_z = (1.f + (float)i * 7.f) * v[i].inv_z;
    }
    kek_3d_triangle_textured(&e, v, texture);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_count(&e, 16));
    TEST_ASSERT_EQUAL_INT(kek_test_frame_painted(&e), kek_test_frame_count(&e, 16));
    kek_test_frame_assert_guards();
    kek_texture_destroy(&e, handle);
}

/* ---- Whole models ---- */

static void draw_cube(float x, float y, float z, float turn) {
    KEK_camera camera = KEK_DEFAULT_CAMERA;
    KEK_model* cube = kek_model_get(&e, kek_default_cube_model_handle(&e));
    KEK_FVec3 pos;
    KEK_FVec3 rotation;

    pos.x = x;
    pos.y = y;
    pos.z = z;
    rotation.x = turn;
    rotation.y = turn * 2.f;
    rotation.z = turn * 0.5f;
    kek_3d_draw_model(&e, cube, &camera, pos, rotation);
}

void test_a_cube_in_front_of_the_camera_draws(void) {
    draw_cube(0.f, 0.f, 3.f, 0.6f);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

/* The view-space vertices are a temporary from the arena. A model with more
   than the arena can hold is not drawn — and not read either, so its arrays
   can be as short as this one's. The arena is whole again afterwards, and a
   model that fits still draws. */
void test_a_model_too_big_to_transform_draws_nothing(void) {
    KEK_camera camera = KEK_DEFAULT_CAMERA;
    KEK_model huge = *kek_model_get(&e, kek_default_cube_model_handle(&e));
    size_t free_bytes = kek_arena_available(&e);

    /* 65535 view-space vertices are 786,420 bytes, past the test arena. */
    huge.verts_count = 0xFFFFu;
    kek_3d_draw_model(&e, &huge, &camera, (KEK_FVec3){ 0.f, 0.f, 3.f }, (KEK_FVec3){ 0.f, 0.f, 0.f });
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    TEST_ASSERT_EQUAL_size_t(free_bytes, kek_arena_available(&e));

    draw_cube(0.f, 0.f, 3.f, 0.6f);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    TEST_ASSERT_EQUAL_size_t(free_bytes, kek_arena_available(&e));
    kek_test_frame_assert_guards();
}

void test_a_cube_behind_the_camera_or_past_the_far_plane_draws_nothing(void) {
    draw_cube(0.f, 0.f, -3.f, 0.6f);
    draw_cube(0.f, 0.f, 2000.f, 0.6f);
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

/* The camera inside the cube, and cubes straddling the near plane and the
   edges of the view: every face that reaches the rasteriser has been through
   kek_3d_clip_near and comes out with vertices far off screen. */
void test_cubes_through_the_near_plane_and_off_the_edges_stay_in_the_frame(void) {
    const float offsets[] = { -3.f, -0.6f, 0.f, 0.6f, 3.f };
    size_t i, j;

    draw_cube(0.f, 0.f, 0.f, 0.f);
    draw_cube(0.f, 0.f, 0.f, 0.9f);
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        for (j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
            draw_cube(offsets[i], offsets[j], 0.3f, 0.4f);
            draw_cube(offsets[i], offsets[j], 2.f, 1.3f);
        }
    }
    kek_test_frame_assert_guards();
}

/* A wall 35 units to the right, 82 tall, running from 50 behind the camera to
   50 in front, so its far end is on screen and the lateral cull lets it
   through. The near-plane clip puts vertices on z = 0.1, where each unit of
   offset is ~1300 px: a clipped triangle reaches (45772, -50832) and twice its
   area passes INT_MAX. The backface test has to get the sign right anyway:
   one winding draws, the other draws nothing. */
static void draw_wall(int facing_camera) {
    static const KEK_FVec3 positions[4] = {
        { 35.f, -2.f, -50.f }, { 35.f, 80.f, -50.f },
        { 35.f, 80.f,  50.f }, { 35.f, -2.f,  50.f }
    };
    static KEK_model_vertex verts[4];
    static const KEK_model_face front[2] = { { 0, 2, 1 }, { 0, 3, 2 } };
    static KEK_model_face faces[2];
    KEK_camera camera = KEK_DEFAULT_CAMERA;
    KEK_model wall;
    KEK_FVec3 zero = { 0.f, 0.f, 0.f };
    size_t i;

    for (i = 0; i < 2; ++i) {
        faces[i] = front[i];
        if (!facing_camera) {
            faces[i].b = front[i].c;
            faces[i].c = front[i].b;
        }
    }

    memset(&wall, 0, sizeof(wall));
    wall.verts = verts;
    wall.faces = faces;
    wall.verts_count = 4;
    wall.faces_count = 2;
    wall.texture = KEK_TEXTURE_HANDLE_INVALID;
    kek_model_quantise(&wall, positions);
    kek_3d_draw_model(&e, &wall, &camera, zero, zero);
}

void test_a_huge_wall_across_the_near_plane_is_culled_by_its_winding_alone(void) {
    draw_wall(1);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();

    kek_test_frame_clear(&e);
    draw_wall(0);
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

/* ---- Lighting ----

   No reference pictures here either. What float cannot move: a face square
   to the light is the colour itself, a face turned away with no ambient is
   the darkest row and nothing else, ambient 1 is no lighting at all, and a
   shade between two rows is exactly those two rows in the proportion the
   dither promises. */

#define LEVELS KEK_PALETTE_SHADING_LEVELS
#define WHITE 15

static uint8_t shaded(int level, uint8_t color) {
    return e.shading_palette[256 * level + color];
}

static KEK_3D_ProjectedVertex vertex_shaded(int x, int y, float depth, float shade) {
    KEK_3D_ProjectedVertex v = vertex(x, y, depth);
    v.shade = shade;
    return v;
}

/* A 2x2 panel drawn through kek_3d_draw_model in the model's default colour.
   Corners in order around the panel; the faces wind them so that
   cross(b - a, c - a) points the way the panel faces. */
static void draw_panel(const KEK_FVec3 corners[4], const KEK_camera* camera) {
    static KEK_model_vertex verts[4];
    static const KEK_model_face faces[2] = { { 0, 2, 1 }, { 0, 3, 2 } };
    KEK_camera cam = *camera;
    KEK_model panel;
    KEK_FVec3 zero = { 0.f, 0.f, 0.f };

    memset(&panel, 0, sizeof(panel));
    panel.verts = verts;
    panel.faces = (KEK_model_face*)faces;
    panel.verts_count = 4;
    panel.faces_count = 2;
    panel.texture = KEK_TEXTURE_HANDLE_INVALID;
    kek_model_quantise(&panel, corners);
    kek_3d_draw_model(&e, &panel, &cam, zero, zero);
}

/* Square to the default camera at depth z, facing it: its normal is -z. */
static void draw_panel_facing_camera(float z) {
    KEK_FVec3 corners[4];
    corners[0] = (KEK_FVec3){ -1.f, -1.f, z };
    corners[1] = (KEK_FVec3){  1.f, -1.f, z };
    corners[2] = (KEK_FVec3){  1.f,  1.f, z };
    corners[3] = (KEK_FVec3){ -1.f,  1.f, z };
    draw_panel(corners, &KEK_DEFAULT_CAMERA);
}

/* Every painted pixel is this colour, and there is at least one. */
static void assert_painted_all(uint8_t color) {
    int painted = kek_test_frame_painted(&e);
    TEST_ASSERT_GREATER_THAN_INT(0, painted);
    TEST_ASSERT_EQUAL_INT(painted, kek_test_frame_count(&e, color));
    kek_test_frame_assert_guards();
}

void test_a_face_square_to_the_light_is_its_own_colour(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, 1.f }, 0.f);
    draw_panel_facing_camera(3.f);
    assert_painted_all(WHITE);
}

void test_a_face_turned_from_the_light_with_no_ambient_is_the_darkest_row(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, -1.f }, 0.f);
    draw_panel_facing_camera(3.f);
    assert_painted_all(shaded(LEVELS - 1, WHITE));
}

void test_full_ambient_switches_lighting_off(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, -1.f }, 1.f);
    draw_panel_facing_camera(3.f);
    assert_painted_all(WHITE);
}

/* Not scaled by the length of the direction it was given. */
void test_the_light_direction_is_normalised(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, 40.f }, 0.f);
    TEST_ASSERT_EQUAL_FLOAT(1.f, e.light.direction.z);
    draw_panel_facing_camera(3.f);
    assert_painted_all(WHITE);
}

/* The light stays in the world. A panel at x = 3 facing -x, lit square on by
   a light travelling +x, seen from a camera turned to look at it from several
   angles: still fully lit every time. A light that turned with the camera
   would see the panel's normal as -z and leave it dark. */
void test_turning_the_camera_does_not_move_the_light(void) {
    const float turns[] = { -0.4f, 0.f, 0.4f };
    KEK_FVec3 corners[4];
    size_t i;

    corners[0] = (KEK_FVec3){ 3.f, -1.f, -1.f };
    corners[1] = (KEK_FVec3){ 3.f,  1.f, -1.f };
    corners[2] = (KEK_FVec3){ 3.f,  1.f,  1.f };
    corners[3] = (KEK_FVec3){ 3.f, -1.f,  1.f };
    kek_3d_set_light(&e, (KEK_FVec3){ 1.f, 0.f, 0.f }, 0.f);
    for (i = 0; i < sizeof(turns) / sizeof(turns[0]); ++i) {
        KEK_camera camera = KEK_DEFAULT_CAMERA;
        camera.rotation.y = -1.5707963f + turns[i];
        kek_test_frame_clear(&e);
        draw_panel(corners, &camera);
        assert_painted_all(WHITE);
    }
}

/* Half a level over the whole frame: the next row on exactly half the
   pixels, since 320x200 is whole 4x4 tiles and a fraction of 8/16 beats 8 of
   the 16 thresholds. */
void test_half_a_level_dithers_half_the_pixels_to_the_next_row(void) {
    int w = e.w, h = e.h;

    flat(vertex_shaded(-10 * w, -10 * h, 3.f, 0.5f),
         vertex_shaded(10 * w, -10 * h, 3.f, 0.5f),
         vertex_shaded(w / 2, 10 * h, 3.f, 0.5f), WHITE);
    TEST_ASSERT_NOT_EQUAL(shaded(0, WHITE), shaded(1, WHITE));
    TEST_ASSERT_EQUAL_INT(w * h / 2, kek_test_frame_count(&e, shaded(0, WHITE)));
    TEST_ASSERT_EQUAL_INT(w * h / 2, kek_test_frame_count(&e, shaded(1, WHITE)));
    kek_test_frame_assert_guards();
}

/* A shade past the table is the last row, not a read past it. */
void test_a_shade_past_the_last_level_is_the_last_row(void) {
    int w = e.w, h = e.h;

    flat(vertex_shaded(-10 * w, -10 * h, 3.f, 1000.f),
         vertex_shaded(10 * w, -10 * h, 3.f, 1000.f),
         vertex_shaded(w / 2, 10 * h, 3.f, 1000.f), WHITE);
    TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_count(&e, shaded(LEVELS - 1, WHITE)));
    kek_test_frame_assert_guards();
}

/* The same textured triangle unshaded and at the darkest level: pixel for
   pixel, the second is the first looked up in the last row. */
void test_a_shaded_texture_is_the_unshaded_one_through_the_shading_palette(void) {
    static uint8_t unshaded[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    const KEK_texture* texture = kek_texture_get(&e, kek_default_texture_handle(&e));
    KEK_3D_ProjectedVertex v[3];
    int x, y, k;

    for (k = 0; k < 2; ++k) {
        float shade = k ? (float)(LEVELS - 1) : 0.f;
        kek_test_frame_clear(&e);
        v[0] = vertex_shaded(10, 10, 3.f, shade);
        v[1] = vertex_shaded(300, 20, 3.f, shade);
        v[2] = vertex_shaded(40, 190, 3.f, shade);
        kek_3d_triangle_textured(&e, v, texture);
        for (y = 0; y < e.h; ++y) {
            for (x = 0; x < e.w; ++x) {
                uint8_t pixel = kek_test_frame_pixel(&e, x, y);
                if (!k) {
                    unshaded[y * e.w + x] = pixel;
                } else if (unshaded[y * e.w + x]) {
                    TEST_ASSERT_EQUAL_UINT8(shaded(LEVELS - 1, unshaded[y * e.w + x]), pixel);
                }
            }
        }
    }
    kek_test_frame_assert_guards();
}

void test_fog_darkens_what_is_past_its_end_and_spares_what_is_before_its_start(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, 1.f }, 1.f);

    kek_3d_set_fog(&e, 1.f, 2.f);
    draw_panel_facing_camera(5.f);
    assert_painted_all(shaded(LEVELS - 1, WHITE));

    kek_test_frame_clear(&e);
    kek_3d_set_fog(&e, 10.f, 20.f);
    draw_panel_facing_camera(3.f);
    assert_painted_all(WHITE);
}

void test_fog_with_its_end_not_past_its_start_is_off(void) {
    kek_3d_set_light(&e, (KEK_FVec3){ 0.f, 0.f, 1.f }, 1.f);
    kek_3d_set_fog(&e, 2.f, 2.f);
    draw_panel_facing_camera(5.f);
    assert_painted_all(WHITE);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_nearer_surface_wins_drawn_last);
    RUN_TEST(test_the_nearer_surface_wins_drawn_first);
    RUN_TEST(test_the_far_surface_shows_where_nothing_nearer_is);
    RUN_TEST(test_a_surface_past_the_depth_range_still_draws);
    RUN_TEST(test_a_surface_nearer_than_the_depth_range_stays_nearest);
    RUN_TEST(test_a_vertex_blit_respects_depth_and_the_frame);
    RUN_TEST(test_triangles_wholly_off_each_edge_draw_nothing);
    RUN_TEST(test_triangles_hanging_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_degenerate_triangles_stay_in_the_frame);
    RUN_TEST(test_a_triangle_larger_than_the_frame_fills_it);
    RUN_TEST(test_a_textured_triangle_larger_than_the_frame_fills_it_in_both_wrap_modes);
    RUN_TEST(test_rows_cover_exactly_the_pixels_of_the_closed_triangle);
    RUN_TEST(test_a_texture_of_any_size_fills_the_frame_in_both_wrap_modes);
    RUN_TEST(test_repeat_wraps_the_same_whatever_repeat_it_starts_in);
    RUN_TEST(test_uvs_past_the_texture_sample_its_edge_under_clamp);
    RUN_TEST(test_a_cube_in_front_of_the_camera_draws);
    RUN_TEST(test_a_cube_behind_the_camera_or_past_the_far_plane_draws_nothing);
    RUN_TEST(test_cubes_through_the_near_plane_and_off_the_edges_stay_in_the_frame);
    RUN_TEST(test_a_huge_wall_across_the_near_plane_is_culled_by_its_winding_alone);
    RUN_TEST(test_a_face_square_to_the_light_is_its_own_colour);
    RUN_TEST(test_a_face_turned_from_the_light_with_no_ambient_is_the_darkest_row);
    RUN_TEST(test_full_ambient_switches_lighting_off);
    RUN_TEST(test_the_light_direction_is_normalised);
    RUN_TEST(test_turning_the_camera_does_not_move_the_light);
    RUN_TEST(test_half_a_level_dithers_half_the_pixels_to_the_next_row);
    RUN_TEST(test_a_shade_past_the_last_level_is_the_last_row);
    RUN_TEST(test_a_shaded_texture_is_the_unshaded_one_through_the_shading_palette);
    RUN_TEST(test_fog_darkens_what_is_past_its_end_and_spares_what_is_before_its_start);
    RUN_TEST(test_fog_with_its_end_not_past_its_start_is_off);
    RUN_TEST(test_a_model_too_big_to_transform_draws_nothing);
    return UNITY_END();
}
