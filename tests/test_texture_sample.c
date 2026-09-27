/* kek_texture_sample at the edges of the texture, under both wrap modes.
   A 4x2 texture so that a stride mix-up between width and height shows, and
   every texel distinct so that each answer names the texel it came from. */

#include <math.h>
#include "unity.h"
#include "kek.h"
#include "kek_texture.h"

/* Texel (x, y) holds 10 * y + x. */
static uint8_t PIXELS[4 * 2] = {
     0,  1,  2,  3,
    10, 11, 12, 13
};
static KEK_texture texture = { PIXELS, 4, 2 };

static KEK_engine e;

void setUp(void) {
    e = kek_init();
}

void tearDown(void) {
}

static uint8_t sample(float u, float v) {
    return kek_texture_sample(&e, &texture, u, v);
}

void test_clamp_is_the_default(void) {
    TEST_ASSERT_EQUAL_INT(KEK_TEXTURE_WARP_CLAMP, kek_texture_get_warp_mode(&e));
}

void test_texel_centres_and_corners(void) {
    TEST_ASSERT_EQUAL_UINT8(0, sample(0.f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(1, sample(0.375f, 0.25f));
    TEST_ASSERT_EQUAL_UINT8(12, sample(0.625f, 0.75f));
    TEST_ASSERT_EQUAL_UINT8(13, sample(0.875f, 0.75f));
}

/* A texel owns [x/w, (x+1)/w): the boundary belongs to the texel after it. */
void test_texel_boundaries(void) {
    TEST_ASSERT_EQUAL_UINT8(0, sample(0.2499f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(1, sample(0.25f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(0, sample(0.f, 0.4999f));
    TEST_ASSERT_EQUAL_UINT8(10, sample(0.f, 0.5f));
}

/* u = 1 is a whole texel past the last one; under CLAMP it has to come back
   to the last texel, not read the next row or past the end. */
void test_clamp_holds_one_and_beyond_to_the_last_texel(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);

    TEST_ASSERT_EQUAL_UINT8(3, sample(1.f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(13, sample(1.f, 1.f));
    TEST_ASSERT_EQUAL_UINT8(10, sample(0.f, 1.f));
    TEST_ASSERT_EQUAL_UINT8(3, sample(0.9999f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(13, sample(7.5f, 1000.f));
    TEST_ASSERT_EQUAL_UINT8(13, sample(1e30f, 1e30f));
}

void test_clamp_holds_negatives_to_the_first_texel(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);

    TEST_ASSERT_EQUAL_UINT8(0, sample(-0.0001f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(0, sample(-1.f, -1.f));
    TEST_ASSERT_EQUAL_UINT8(10, sample(-3.f, 0.75f));
    TEST_ASSERT_EQUAL_UINT8(0, sample(-1e30f, -1e30f));
}

void test_repeat_wraps_whole_units_back_to_the_start(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);

    TEST_ASSERT_EQUAL_UINT8(0, sample(1.f, 1.f));
    TEST_ASSERT_EQUAL_UINT8(0, sample(2.f, 3.f));
    TEST_ASSERT_EQUAL_UINT8(1, sample(1.25f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(12, sample(5.5f, 7.5f));
    TEST_ASSERT_EQUAL_UINT8(3, sample(1.9999f, 0.f));
}

void test_repeat_wraps_negatives_from_the_far_end(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);

    TEST_ASSERT_EQUAL_UINT8(3, sample(-0.25f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(10, sample(0.f, -0.5f));
    TEST_ASSERT_EQUAL_UINT8(0, sample(-1.f, -2.f));
    TEST_ASSERT_EQUAL_UINT8(2, sample(-2.5f, 0.f));
}

/* So small a negative that u - floor(u) rounds to exactly 1.0f in float —
   the one input that makes REPEAT produce the value it is meant to exclude.
   It has to land on the last texel, not one past it. */
void test_repeat_just_below_zero_lands_on_the_last_texel(void) {
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);

    TEST_ASSERT_EQUAL_UINT8(13, sample(-1e-9f, -1e-9f));
    TEST_ASSERT_EQUAL_UINT8(13, sample(-0.0001f, -0.0001f));
}

/* Without an engine there is no mode to read, and CLAMP is the answer. */
void test_no_engine_means_clamp(void) {
    TEST_ASSERT_EQUAL_UINT8(3, kek_texture_sample(0, &texture, 1.5f, 0.f));
    TEST_ASSERT_EQUAL_UINT8(0, kek_texture_sample(0, &texture, -0.5f, 0.f));
    TEST_ASSERT_EQUAL_INT(KEK_TEXTURE_WARP_CLAMP, kek_texture_get_warp_mode(0));
    kek_texture_set_warp_mode(0, KEK_TEXTURE_WARP_REPEAT);
}

void test_a_one_texel_texture_is_that_texel_everywhere(void) {
    uint8_t one = 77;
    KEK_texture single = { &one, 1, 1 };
    const float coordinates[] = { -1e9f, -1.f, -0.5f, 0.f, 0.5f, 0.9999f, 1.f, 3.25f, 1e9f };
    size_t i, j;
    int mode;

    for (mode = 0; mode < 2; ++mode) {
        kek_texture_set_warp_mode(&e, mode ? KEK_TEXTURE_WARP_REPEAT : KEK_TEXTURE_WARP_CLAMP);
        for (i = 0; i < sizeof(coordinates) / sizeof(coordinates[0]); ++i) {
            for (j = 0; j < sizeof(coordinates) / sizeof(coordinates[0]); ++j) {
                TEST_ASSERT_EQUAL_UINT8(77, kek_texture_sample(&e, &single, coordinates[i], coordinates[j]));
            }
        }
    }
}

/* NaN and infinity are not coordinates, and today the rasteriser filters NaN
   UVs before it samples, but the function cannot lean on its callers for
   that: converting NaN to an integer is undefined. NaN is taken to 0 and reads
   the first texel of its row or column, under both modes. */
void test_nan_and_infinity_read_the_first_texel(void) {
    const float nan = NAN;
    const float inf = INFINITY;
    int mode;

    for (mode = 0; mode < 2; ++mode) {
        kek_texture_set_warp_mode(&e, mode ? KEK_TEXTURE_WARP_REPEAT : KEK_TEXTURE_WARP_CLAMP);
        TEST_ASSERT_EQUAL_UINT8(0, sample(nan, nan));
        TEST_ASSERT_EQUAL_UINT8(10, sample(nan, 0.75f));
        TEST_ASSERT_EQUAL_UINT8(2, sample(0.625f, nan));
    }

    /* Infinity is only undefined after REPEAT turns it into NaN; CLAMP holds
       it to the edge like any other large value. */
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
    TEST_ASSERT_EQUAL_UINT8(0, sample(inf, -inf));
    TEST_ASSERT_EQUAL_UINT8(10, sample(-inf, 0.75f));
    kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    TEST_ASSERT_EQUAL_UINT8(10, sample(-inf, inf));
    TEST_ASSERT_EQUAL_UINT8(3, sample(inf, -inf));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_is_the_default);
    RUN_TEST(test_texel_centres_and_corners);
    RUN_TEST(test_texel_boundaries);
    RUN_TEST(test_clamp_holds_one_and_beyond_to_the_last_texel);
    RUN_TEST(test_clamp_holds_negatives_to_the_first_texel);
    RUN_TEST(test_repeat_wraps_whole_units_back_to_the_start);
    RUN_TEST(test_repeat_wraps_negatives_from_the_far_end);
    RUN_TEST(test_repeat_just_below_zero_lands_on_the_last_texel);
    RUN_TEST(test_no_engine_means_clamp);
    RUN_TEST(test_a_one_texel_texture_is_that_texel_everywhere);
    RUN_TEST(test_nan_and_infinity_read_the_first_texel);
    return UNITY_END();
}
