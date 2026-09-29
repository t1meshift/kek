/* The perspective span of a wide textured triangle against the UV it stands
   for: each covered pixel is held to the texture sampled at the exact
   u = (u/z) / (1/z) worked out from the triangle's own vertices, to within a
   texel either way, since the span steps between exactly divided ends. The
   triangle's depth varies a good deal, so that the divide does real work. */

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

static KEK_3D_ProjectedVertex vertex(int x, int y, float depth, float uv_scale) {
    KEK_3D_ProjectedVertex v;
    v.screen.x = x;
    v.screen.y = y;
    v.depth = depth;
    v.inv_z = 1.f / depth;
    v.u_over_z = (((float)x + 0.37f) / 64.f) * uv_scale * v.inv_z;
    v.v_over_z = (((float)y + 0.61f) / 64.f) * uv_scale * v.inv_z;
    v.shade = 0.f;
    v.fog = 0.f;
    return v;
}

/* Twice the signed area of (a, b, p): the edge function. */
static double edge(const KEK_3D_ProjectedVertex* a, const KEK_3D_ProjectedVertex* b, int px, int py) {
    return (double)(b->screen.x - a->screen.x) * (double)(py - a->screen.y)
         - (double)(b->screen.y - a->screen.y) * (double)(px - a->screen.x);
}

/* How many of the covered pixels are further than a texel from the exact UV,
   and how many are covered. */
static void compare(const KEK_3D_ProjectedVertex v[3], int* covered, int* far) {
    const KEK_texture* texture = kek_texture_get(&e, kek_default_texture_handle(&e));
    double area = edge(&v[0], &v[1], v[2].screen.x, v[2].screen.y);
    float step_u = 1.f / (float)texture->width, step_v = 1.f / (float)texture->height;
    int px, py;

    *covered = *far = 0;
    for (py = 0; py < (int)KEK_TEST_HEIGHT; ++py) {
        for (px = 0; px < (int)KEK_TEST_WIDTH; ++px) {
            double w0 = edge(&v[1], &v[2], px, py) / area;
            double w1 = edge(&v[2], &v[0], px, py) / area;
            double w2 = 1. - w0 - w1;
            double inv_z, u, w;
            uint8_t got = kek_test_frame_pixel(&e, px, py);
            int du, dv, near = 0;

            if (got == 0) {
                continue;
            }
            inv_z = w0 * v[0].inv_z + w1 * v[1].inv_z + w2 * v[2].inv_z;
            u = (w0 * v[0].u_over_z + w1 * v[1].u_over_z + w2 * v[2].u_over_z) / inv_z;
            w = (w0 * v[0].v_over_z + w1 * v[1].v_over_z + w2 * v[2].v_over_z) / inv_z;
            for (dv = -1; dv <= 1; ++dv) {
                for (du = -1; du <= 1; ++du) {
                    near |= kek_texture_sample(&e, texture, (float)u + (float)du * step_u,
                                               (float)w + (float)dv * step_v) == got;
                }
            }
            ++*covered;
            *far += !near;
        }
    }
}

static void draw_triangle(float uv_scale, KEK_3D_ProjectedVertex v[3]) {
    v[0] = vertex(40, 30, 2.f, uv_scale);
    v[1] = vertex(280, 50, 10.f, uv_scale);
    v[2] = vertex(60, 180, 4.f, uv_scale);
    kek_3d_triangle_textured(&e, v, kek_texture_get(&e, kek_default_texture_handle(&e)));
}

static void assert_matches(float uv_scale) {
    KEK_3D_ProjectedVertex v[3];
    int covered, far;

    draw_triangle(uv_scale, v);
    compare(v, &covered, &far);
    TEST_ASSERT_GREATER_THAN_INT(10000, covered);
    TEST_ASSERT_LESS_OR_EQUAL_INT(covered / 50, far);
    kek_test_frame_assert_guards();
}

void test_the_perspective_span_matches_the_exact_uv_under_clamp(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    assert_matches(1.f);
}

void test_the_perspective_span_matches_the_exact_uv_under_repeat(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    assert_matches(1.f);
}

/* UV a dozen times round the texture: still a 32-bit span under REPEAT. Kept
   low enough for the linear step within a span, 32 pixels by default, to stay
   within a texel of the exact UV. */
void test_the_perspective_span_matches_the_exact_uv_repeating_many_times(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    assert_matches(4.f);
}

/* Past what 32 bits hold the 64-bit span takes over. It is not held to the
   exact UV: with hundreds of repeats of the texture to a span the linear step
   between its ends is noise, as it was before, so only that it draws and
   stays in the frame. */
void test_the_perspective_span_beyond_the_32_bit_range_draws_and_stays_in_the_frame(void) {
    KEK_3D_ProjectedVertex v[3];

    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    draw_triangle(700.f, v);
    TEST_ASSERT_GREATER_THAN_INT(10000, kek_test_frame_painted(&e));
    kek_test_frame_assert_guards();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_perspective_span_matches_the_exact_uv_under_clamp);
    RUN_TEST(test_the_perspective_span_matches_the_exact_uv_under_repeat);
    RUN_TEST(test_the_perspective_span_matches_the_exact_uv_repeating_many_times);
    RUN_TEST(test_the_perspective_span_beyond_the_32_bit_range_draws_and_stays_in_the_frame);
    return UNITY_END();
}
