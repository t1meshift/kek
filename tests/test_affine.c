/* The affine path for small textured triangles against the perspective path
   a wide one takes. The two are told apart by width alone, so the tests draw
   the same surface twice, as one wide triangle and as a small one inside it,
   and hold the small one to what the wide one painted. The texture, as the
   perspective span steps it, is a fraction of a texel from the plane the
   affine path evaluates on so small a triangle; what may differ is the odd
   pixel at a texel's edge, not the picture. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_3d.h"
#include "kek_texture.h"
#include "test_support.h"

static KEK_engine e;

void setUp(void) {
    kek_test_init(&e);
    kek_test_frame_attach(&e, KEK_TEST_WIDTH, KEK_TEST_HEIGHT);
}

void tearDown(void) {
}

/* UV is the screen position over 64, u/z and v/z as the pipeline carries
   them. A shade_per_px other than 0 is a shade that rises with x, and twice as
   fast with y, by that many levels a pixel from 1.5 at (60, 50), so that it
   stays between the palette's levels near there instead of clamping at one.
   Every vertex at one depth, so the perspective and the affine texture are the
   same surface. */
static KEK_3D_ProjectedVertex vertex(int x, int y, float depth, float uv_scale, float shade_per_px) {
    KEK_3D_ProjectedVertex v;
    v.screen.x = x;
    v.screen.y = y;
    v.depth = depth;
    v.inv_z = 1.f / depth;
    /* Off the texel edges: a texture sampled exactly on one is decided by the
       last bit of the rounding, which is not what is under test. */
    v.u_over_z = (((float)x + 0.37f) / 64.f) * uv_scale * v.inv_z;
    v.v_over_z = (((float)y + 0.61f) / 64.f) * uv_scale * v.inv_z;
    v.shade = shade_per_px == 0.f ? 0.f : 1.5f + ((float)(x - 60) + (float)(y - 50) * 2.f) * shade_per_px;
    v.fog = 0.f;
    return v;
}

static void triangle(int x0, int y0, int x1, int y1, int x2, int y2, float depth, float uv_scale, float shade_per_px) {
    KEK_3D_ProjectedVertex v[3];
    v[0] = vertex(x0, y0, depth, uv_scale, shade_per_px);
    v[1] = vertex(x1, y1, depth, uv_scale, shade_per_px);
    v[2] = vertex(x2, y2, depth, uv_scale, shade_per_px);
    kek_3d_triangle_textured(&e, v, kek_texture_get(&e, kek_default_texture_handle(&e)));
}

/* A wide triangle over (x, y) .. (x + 24, y + 24) to snapshot, and a small one
   inside it, twelve pixels across, to compare. */
static void assert_small_matches_wide(int x, int y, float uv_scale, float shade_per_px) {
    static uint8_t wide[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    int px, py, painted = 0, differing = 0;

    triangle(x - 60, y - 60, x + 180, y - 60, x - 60, y + 180, 4.f, uv_scale, shade_per_px);
    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            wide[py * KEK_TEST_WIDTH + px] = kek_test_frame_pixel(&e, px, py);
        }
    }
    kek_test_frame_clear(&e);
    triangle(x, y, x + 12, y, x, y + 12, 4.f, uv_scale, shade_per_px);
    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            uint8_t small = kek_test_frame_pixel(&e, px, py);
            if (small != 0) {
                ++painted;
                differing += small != wide[py * KEK_TEST_WIDTH + px];
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(50, painted);
    TEST_ASSERT_LESS_OR_EQUAL_INT(painted / 10, differing);
    kek_test_frame_assert_guards();
}

void test_a_small_triangle_matches_the_perspective_path_under_clamp(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    assert_small_matches_wide(30, 40, 1.f, 0.f);
}

void test_a_small_triangle_matches_the_perspective_path_under_repeat(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    assert_small_matches_wide(30, 40, 1.f, 0.f);
}

/* Past the texture's edge under CLAMP, and several times round it under REPEAT. */
void test_a_small_triangle_far_outside_the_texture_matches_under_clamp(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    assert_small_matches_wide(200, 120, 3.f, 0.f);
}

void test_a_small_triangle_far_outside_the_texture_matches_under_repeat(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    assert_small_matches_wide(200, 120, 3.f, 0.f);
}

/* A shade that varies across the triangle takes the other pixel loop. */
void test_a_small_triangle_with_a_varying_shade_matches(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    assert_small_matches_wide(60, 50, 1.f, 0.1f);
}

/* The depth planes are stepped in fixed point when the vertices allow it. */
void test_small_triangles_are_depth_tested_in_either_order(void) {
    static uint8_t first[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    int px, py, differing = 0;

    triangle(50, 50, 62, 50, 50, 62, 2.f, 1.f, 0.f);
    triangle(52, 52, 64, 52, 52, 64, 10.f, 2.f, 0.f);
    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            first[py * KEK_TEST_WIDTH + px] = kek_test_frame_pixel(&e, px, py);
        }
    }
    kek_test_frame_clear(&e);
    triangle(52, 52, 64, 52, 52, 64, 10.f, 2.f, 0.f);
    triangle(50, 50, 62, 50, 50, 62, 2.f, 1.f, 0.f);
    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            differing += kek_test_frame_pixel(&e, px, py) != first[py * KEK_TEST_WIDTH + px];
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(50, kek_test_frame_painted(&e));
    TEST_ASSERT_EQUAL_INT(0, differing);
    kek_test_frame_assert_guards();
}

static void snapshot(uint8_t* out) {
    int px, py;

    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            out[py * KEK_TEST_WIDTH + px] = kek_test_frame_pixel(&e, px, py);
        }
    }
}

/* Where each of two crossing surfaces wins: a triangle that runs from depth 2
   at its top to 10 at its bottom against a flat one at 5, so the slanted one
   is nearer along its top rows only. Each is drawn alone first to see whose
   pixels are whose. */
void test_a_slanted_small_triangle_crosses_a_flat_one_at_the_right_row(void) {
    static uint8_t slanted[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    static uint8_t flat_one[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    static uint8_t both[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    KEK_3D_ProjectedVertex s[3], f[3];
    const KEK_texture* texture = kek_texture_get(&e, kek_default_texture_handle(&e));
    int px, py, top_slanted = 0, top_total = 0, bottom_flat = 0, bottom_total = 0;

    s[0] = vertex(8, 8, 2.f, 1.f, 0.f);
    s[1] = vertex(20, 8, 2.f, 1.f, 0.f);
    s[2] = vertex(8, 20, 10.f, 1.f, 0.f);
    f[0] = vertex(6, 6, 5.f, 3.f, 0.f);
    f[1] = vertex(30, 6, 5.f, 3.f, 0.f);
    f[2] = vertex(6, 30, 5.f, 3.f, 0.f);
    kek_3d_triangle_textured(&e, s, texture);
    snapshot(slanted);
    kek_test_frame_clear(&e);
    kek_3d_triangle_textured(&e, f, texture);
    snapshot(flat_one);
    kek_3d_triangle_textured(&e, s, texture);
    snapshot(both);
    for (py = 8; py < 20; ++py) {
        for (px = 0; px < 30; ++px) {
            int i = py * KEK_TEST_WIDTH + px;
            if (slanted[i] == 0 || slanted[i] == flat_one[i]) {
                continue;
            }
            if (py <= 9) {
                ++top_total;
                top_slanted += both[i] == slanted[i];
            } else if (py >= 16) {
                ++bottom_total;
                bottom_flat += both[i] == flat_one[i];
            }
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(5, top_total);
    TEST_ASSERT_GREATER_THAN_INT(5, bottom_total);
    TEST_ASSERT_EQUAL_INT(top_total, top_slanted);
    TEST_ASSERT_EQUAL_INT(bottom_total, bottom_flat);
    kek_test_frame_assert_guards();
}

/* Beyond the limits the fixed-point planes are exact within, the triangle is
   drawn the perspective way, and nothing strays out of the frame. */
void test_a_triangle_with_enormous_uv_stays_in_the_frame(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    triangle(100, 100, 112, 100, 100, 112, 4.f, 1.e7f, 0.f);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_clear(&e);
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    triangle(100, 100, 112, 100, 100, 112, 4.f, 1.e7f, 0.f);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_small_triangle_matches_the_perspective_path_under_clamp);
    RUN_TEST(test_a_small_triangle_matches_the_perspective_path_under_repeat);
    RUN_TEST(test_a_small_triangle_far_outside_the_texture_matches_under_clamp);
    RUN_TEST(test_a_small_triangle_far_outside_the_texture_matches_under_repeat);
    RUN_TEST(test_a_small_triangle_with_a_varying_shade_matches);
    RUN_TEST(test_small_triangles_are_depth_tested_in_either_order);
    RUN_TEST(test_a_slanted_small_triangle_crosses_a_flat_one_at_the_right_row);
    RUN_TEST(test_a_triangle_with_enormous_uv_stays_in_the_frame);
    return UNITY_END();
}
