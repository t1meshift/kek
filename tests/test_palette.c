/* kek_palette.c: nearest-colour search and the shading table built from it.
   Integer arithmetic throughout, so these compare exactly. */

#include <string.h>
#include "unity.h"
#include "kek_config.h"
#include "kek_palette.h"

#define LEVELS KEK_PALETTE_SHADING_LEVELS

static KEK_palette_item palette[256];
static uint8_t shading[256 * LEVELS];

void setUp(void) {
    memcpy(palette, KEK_DEFAULT_PALETTE, sizeof(palette));
    memset(shading, 0xEE, sizeof(shading));
}

void tearDown(void) {
}

static KEK_palette_item rgb(uint8_t r, uint8_t g, uint8_t b) {
    KEK_palette_item item;
    item.r = r;
    item.g = g;
    item.b = b;
    return item;
}

static void fill_palette(KEK_palette_item item) {
    int i;
    for (i = 0; i < 256; ++i) {
        palette[i] = item;
    }
}

/* palette[i] = (i, i, i) for the 64 greys, the rest a repeat of white. Every
   darkened grey is then in the palette exactly, at the index equal to its
   value, which makes the whole shading table predictable. */
static void grey_ramp_palette(void) {
    int i;
    for (i = 0; i < 256; ++i) {
        uint8_t v = (uint8_t)(i < 64 ? i : 63);
        palette[i] = rgb(v, v, v);
    }
}

/* Every entry of shading row r is r, so a lookup says which row it read. */
static void number_rows_by_level(void) {
    int level;
    for (level = 0; level < LEVELS; ++level) {
        memset(shading + (size_t)256 * level, level, 256);
    }
}

void test_nearest_finds_every_colour_that_is_in_the_palette(void) {
    int i;

    for (i = 0; i < 256; ++i) {
        uint8_t found = kek_palette_nearest_color(palette, palette[i]);
        TEST_ASSERT_EQUAL_UINT8(palette[i].r, palette[found].r);
        TEST_ASSERT_EQUAL_UINT8(palette[i].g, palette[found].g);
        TEST_ASSERT_EQUAL_UINT8(palette[i].b, palette[found].b);
    }
}

/* The default palette ends in a run of blacks after index 0; lookups must be
   stable and pick the first. */
void test_nearest_takes_the_lowest_index_on_a_tie(void) {
    fill_palette(rgb(10, 10, 10));
    TEST_ASSERT_EQUAL_UINT8(0, kek_palette_nearest_color(palette, rgb(10, 10, 10)));
    TEST_ASSERT_EQUAL_UINT8(0, kek_palette_nearest_color(palette, rgb(63, 0, 0)));

    palette[200] = rgb(1, 2, 3);
    palette[201] = rgb(1, 2, 3);
    TEST_ASSERT_EQUAL_UINT8(200, kek_palette_nearest_color(palette, rgb(1, 2, 3)));
}

void test_nearest_can_return_the_last_entry(void) {
    fill_palette(rgb(0, 0, 0));
    palette[255] = rgb(63, 63, 63);
    TEST_ASSERT_EQUAL_UINT8(255, kek_palette_nearest_color(palette, rgb(60, 60, 60)));
}

/* Each channel on its own has to pull the answer towards the closer
   candidate — whatever the weights are, none of them may be zero. */
void test_nearest_weighs_every_channel(void) {
    fill_palette(rgb(63, 63, 63));

    palette[1] = rgb(30, 20, 20);
    palette[2] = rgb(34, 20, 20);
    TEST_ASSERT_EQUAL_UINT8(1, kek_palette_nearest_color(palette, rgb(29, 20, 20)));
    TEST_ASSERT_EQUAL_UINT8(2, kek_palette_nearest_color(palette, rgb(35, 20, 20)));

    palette[1] = rgb(20, 30, 20);
    palette[2] = rgb(20, 34, 20);
    TEST_ASSERT_EQUAL_UINT8(1, kek_palette_nearest_color(palette, rgb(20, 29, 20)));
    TEST_ASSERT_EQUAL_UINT8(2, kek_palette_nearest_color(palette, rgb(20, 35, 20)));

    palette[1] = rgb(20, 20, 30);
    palette[2] = rgb(20, 20, 34);
    TEST_ASSERT_EQUAL_UINT8(1, kek_palette_nearest_color(palette, rgb(20, 20, 29)));
    TEST_ASSERT_EQUAL_UINT8(2, kek_palette_nearest_color(palette, rgb(20, 20, 35)));
}

void test_nearest_without_a_palette_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, kek_palette_nearest_color(0, rgb(63, 63, 63)));
}

void test_shading_level_zero_is_the_identity(void) {
    int j;

    kek_palette_calculate_shading(shading, palette);
    for (j = 0; j < 256; ++j) {
        TEST_ASSERT_EQUAL_UINT8(j, shading[j]);
    }
}

void test_shading_darkens_by_level_over_a_grey_ramp(void) {
    int level, j;

    grey_ramp_palette();
    kek_palette_calculate_shading(shading, palette);

    for (level = 1; level < LEVELS; ++level) {
        for (j = 0; j < 256; ++j) {
            int grey = j < 64 ? j : 63;
            int expected = grey * (LEVELS - level) / LEVELS;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, shading[256 * level + j], "grey ramp shade");
        }
    }
}

/* On the real palette, darker means darker: no shade may come out brighter
   than the colour it shades, and each level is no brighter than the one
   before it. Brightness here is the plain channel sum — crude, but the
   palette search has no business making anything brighter by any measure. */
void test_shading_never_brightens_the_default_palette(void) {
    int level, j;

    kek_palette_calculate_shading(shading, palette);
    for (j = 0; j < 256; ++j) {
        int previous = palette[j].r + palette[j].g + palette[j].b;
        for (level = 1; level < LEVELS; ++level) {
            KEK_palette_item shade = palette[shading[256 * level + j]];
            int brightness = shade.r + shade.g + shade.b;
            TEST_ASSERT_LESS_OR_EQUAL_INT(previous, brightness);
            previous = brightness;
        }
    }
}

void test_shading_with_null_arguments_writes_nothing(void) {
    int i;

    kek_palette_calculate_shading(shading, 0);
    kek_palette_calculate_shading(0, palette);
    for (i = 0; i < 256 * LEVELS; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0xEE, shading[i]);
    }
}

void test_shade_picks_the_row_for_the_level(void) {
    int level;

    number_rows_by_level();
    for (level = 0; level < LEVELS; ++level) {
        TEST_ASSERT_EQUAL_UINT8(level, kek_palette_shade(shading, 17, level));
    }
}

void test_shade_clamps_a_negative_level_to_the_first_row(void) {
    number_rows_by_level();
    TEST_ASSERT_EQUAL_UINT8(0, kek_palette_shade(shading, 17, -1));
    TEST_ASSERT_EQUAL_UINT8(0, kek_palette_shade(shading, 255, -1000));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nearest_finds_every_colour_that_is_in_the_palette);
    RUN_TEST(test_nearest_takes_the_lowest_index_on_a_tie);
    RUN_TEST(test_nearest_can_return_the_last_entry);
    RUN_TEST(test_nearest_weighs_every_channel);
    RUN_TEST(test_nearest_without_a_palette_is_zero);
    RUN_TEST(test_shading_level_zero_is_the_identity);
    RUN_TEST(test_shading_darkens_by_level_over_a_grey_ramp);
    RUN_TEST(test_shading_never_brightens_the_default_palette);
    RUN_TEST(test_shading_with_null_arguments_writes_nothing);
    RUN_TEST(test_shade_picks_the_row_for_the_level);
    RUN_TEST(test_shade_clamps_a_negative_level_to_the_first_row);
    return UNITY_END();
}
