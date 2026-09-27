/* The 2D primitives against a guarded frame. These are the passes that were
   done by hand under ASan — d4776d4 for rectangles and circles, 349beae for
   kek_2d_triangle — kept as tests so they are not done once and thrown away.

   No golden images: what is asserted is what does not depend on float or on
   rasterisation rules. Nothing is written outside the frame; what lies wholly
   off screen draws nothing; what covers the screen covers all of it; and a
   few exact counts where the primitive's definition fixes them. Each battery
   runs at the engine's own size and at an odd one, so code that reads
   KEK_BUFFER_WIDTH instead of e->w shows. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_2d.h"
#include "kek_font.h"
#include "test_support.h"

static KEK_engine e;

static const struct {
    uint16_t w, h;
} SIZES[] = {
    { KEK_BUFFER_WIDTH, KEK_BUFFER_HEIGHT },
    { 61, 37 }
};
#define SIZE_COUNT (sizeof(SIZES) / sizeof(SIZES[0]))

#define INK 9
#define BORDER 12

void setUp(void) {
    e = kek_init();
}

void tearDown(void) {
}

static KEK_IVec2 at(int x, int y) {
    KEK_IVec2 p;
    p.x = x;
    p.y = y;
    return p;
}

/* Points a shape can be centred on to hang off each edge and each corner by
   `reach`, or to sit wholly outside when reach exceeds the shape. */
static int hanging_points(int w, int h, int reach, KEK_IVec2 out[8]) {
    out[0] = at(-reach, h / 2);
    out[1] = at(w - 1 + reach, h / 2);
    out[2] = at(w / 2, -reach);
    out[3] = at(w / 2, h - 1 + reach);
    out[4] = at(-reach, -reach);
    out[5] = at(w - 1 + reach, -reach);
    out[6] = at(-reach, h - 1 + reach);
    out[7] = at(w - 1 + reach, h - 1 + reach);
    return 8;
}

/* ---- Rectangles ---- */

/* Closed in x, half-open in y — see kek_2d_rect — whichever way round the
   corners are given. */
void test_rect_fills_the_same_pixels_whichever_way_its_corners_come(void) {
    static uint8_t reference[KEK_TEST_FRAME_MAX_PIXELS];
    const KEK_IVec2 corners[4][2] = {
        { { 3, 4 }, { 20, 15 } },
        { { 20, 15 }, { 3, 4 } },
        { { 20, 4 }, { 3, 15 } },
        { { 3, 15 }, { 20, 4 } }
    };
    size_t pixels;
    int i;

    kek_test_frame_attach(&e, 61, 37);
    pixels = (size_t)e.w * e.h;
    kek_2d_rect(&e, corners[0][0], corners[0][1], INK);
    TEST_ASSERT_EQUAL_INT((20 - 3 + 1) * (15 - 4), kek_test_frame_count(&e, INK));
    TEST_ASSERT_EQUAL_UINT8(INK, kek_test_frame_pixel(&e, 3, 4));
    TEST_ASSERT_EQUAL_UINT8(INK, kek_test_frame_pixel(&e, 20, 14));
    TEST_ASSERT_EQUAL_UINT8(0, kek_test_frame_pixel(&e, 20, 15));
    memcpy(reference, e.fb, pixels);

    for (i = 1; i < 4; ++i) {
        kek_test_frame_clear(&e);
        kek_2d_rect(&e, corners[i][0], corners[i][1], INK);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(reference, e.fb, pixels);
    }
    kek_test_frame_assert_guards();
}

void test_rects_off_every_edge_stay_in_the_frame(void) {
    size_t s;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        n = hanging_points(e.w, e.h, 5, points);
        for (i = 0; i < n; ++i) {
            /* Both corner orders: reversed corners were one of the three
               out-of-bounds writes d4776d4 found. */
            kek_2d_rect(&e, at(points[i].x - 10, points[i].y - 10), at(points[i].x + 10, points[i].y + 10), INK);
            kek_2d_rect(&e, at(points[i].x + 10, points[i].y + 10), at(points[i].x - 10, points[i].y - 10), INK);
            kek_2d_rect_border(&e, at(points[i].x + 10, points[i].y - 10), at(points[i].x - 10, points[i].y + 10), INK, BORDER);
        }
        kek_test_frame_assert_guards();
    }
}

void test_rects_wholly_off_screen_draw_nothing(void) {
    size_t s;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        n = hanging_points(e.w, e.h, 30, points);
        for (i = 0; i < n; ++i) {
            kek_2d_rect(&e, at(points[i].x - 10, points[i].y - 10), at(points[i].x + 10, points[i].y + 10), INK);
            kek_2d_rect_border(&e, at(points[i].x + 10, points[i].y + 10), at(points[i].x - 10, points[i].y - 10), INK, BORDER);
        }
        TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
        kek_test_frame_assert_guards();
    }
}

void test_a_rect_larger_than_the_frame_fills_it(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        kek_2d_rect(&e, at(100000, 100000), at(-100000, -100000), INK);
        TEST_ASSERT_EQUAL_INT(e.w * e.h, kek_test_frame_count(&e, INK));
        kek_test_frame_assert_guards();
    }
}

/* d4776d4: rect_border drew one diagonal four times. */
void test_rect_border_draws_the_four_edges(void) {
    kek_test_frame_attach(&e, 61, 37);
    kek_2d_rect_border(&e, at(5, 6), at(30, 20), INK, BORDER);

    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 5, 6));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 30, 6));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 30, 20));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 5, 20));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 17, 6));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 17, 20));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 5, 13));
    TEST_ASSERT_EQUAL_UINT8(BORDER, kek_test_frame_pixel(&e, 30, 13));
    TEST_ASSERT_EQUAL_UINT8(INK, kek_test_frame_pixel(&e, 17, 13));
    /* The perimeter of a 26x15 box, corners counted once. */
    TEST_ASSERT_EQUAL_INT(2 * 26 + 2 * 15 - 4, kek_test_frame_count(&e, BORDER));
    kek_test_frame_assert_guards();
}

/* ---- Circles ---- */

void test_circles_off_every_edge_stay_in_the_frame(void) {
    const uint16_t radii[] = { 0, 1, 7, 40, 250 };
    size_t s, r;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        for (r = 0; r < sizeof(radii) / sizeof(radii[0]); ++r) {
            KEK_IVec2 points[8];
            n = hanging_points(e.w, e.h, radii[r] / 2, points);
            for (i = 0; i < n; ++i) {
                kek_2d_circle(&e, points[i], radii[r], INK);
                kek_2d_circle_border(&e, points[i], radii[r], INK, BORDER);
            }
        }
        kek_test_frame_assert_guards();
    }
}

void test_circles_wholly_off_screen_draw_nothing(void) {
    size_t s;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        /* Radius 10, centred 20 out: diagonally that is 28 from the corner. */
        n = hanging_points(e.w, e.h, 20, points);
        for (i = 0; i < n; ++i) {
            kek_2d_circle(&e, points[i], 10, INK);
            kek_2d_circle_border(&e, points[i], 10, INK, BORDER);
        }
        TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
        kek_test_frame_assert_guards();
    }
}

void test_a_circle_larger_than_the_frame_fills_it(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        kek_2d_circle(&e, at(e.w / 2, e.h / 2), 65535, INK);
        TEST_ASSERT_EQUAL_INT(e.w * e.h, kek_test_frame_count(&e, INK));
        kek_test_frame_assert_guards();
    }
}

/* ---- Lines ---- */

void test_lines_across_and_off_every_edge_stay_in_the_frame(void) {
    size_t s;
    int i, j, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        n = hanging_points(e.w, e.h, 25, points);
        for (i = 0; i < n; ++i) {
            for (j = 0; j < n; ++j) {
                kek_2d_line(&e, points[i], points[j], INK);
            }
            kek_2d_line(&e, points[i], at(e.w / 2, e.h / 2), INK);
            kek_2d_line(&e, at(e.w / 2, e.h / 2), points[i], INK);
        }
        kek_2d_line(&e, at(-100000, -70000), at(100000, 70000), INK);
        kek_test_frame_assert_guards();
    }
}

void test_lines_wholly_off_screen_draw_nothing(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        int w, h;
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        w = e.w;
        h = e.h;
        kek_2d_line(&e, at(-5, 0), at(-5, h - 1), INK);
        kek_2d_line(&e, at(w, 0), at(w + 40, h - 1), INK);
        kek_2d_line(&e, at(0, -1), at(w - 1, -1), INK);
        kek_2d_line(&e, at(0, h), at(w - 1, h + 3), INK);
        /* Past each corner, ends in two different outside regions: the case
           fd4b3b2 fixed, where the whole left column used to come back. */
        kek_2d_line(&e, at(-20, 10), at(10, -20), INK);
        kek_2d_line(&e, at(w + 19, 10), at(w - 11, -20), INK);
        kek_2d_line(&e, at(-20, h - 11), at(10, h + 19), INK);
        kek_2d_line(&e, at(w + 19, h - 11), at(w - 11, h + 19), INK);
        TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
        kek_test_frame_assert_guards();
    }
}

/* Both ends, in either direction, and one pixel per step along the major
   axis — the same count the horizontal fast path has always drawn. */
void test_a_line_includes_both_ends(void) {
    const KEK_IVec2 ends[][2] = {
        { { 2, 3 }, { 12, 3 } },
        { { 4, 30 }, { 4, 20 } },
        { { 0, 0 }, { 10, 10 } },
        { { 0, 0 }, { 10, 3 } },
        { { 50, 2 }, { 47, 35 } },
        { { 60, 36 }, { 0, 0 } },
        { { 9, 9 }, { 9, 9 } }
    };
    size_t i;
    int pass;

    kek_test_frame_attach(&e, 61, 37);
    for (i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i) {
        for (pass = 0; pass < 2; ++pass) {
            KEK_IVec2 from = ends[i][pass], to = ends[i][1 - pass];
            int dx = to.x > from.x ? to.x - from.x : from.x - to.x;
            int dy = to.y > from.y ? to.y - from.y : from.y - to.y;

            kek_test_frame_clear(&e);
            kek_2d_line(&e, from, to, INK);
            TEST_ASSERT_EQUAL_UINT8(INK, kek_test_frame_pixel(&e, from.x, from.y));
            TEST_ASSERT_EQUAL_UINT8(INK, kek_test_frame_pixel(&e, to.x, to.y));
            TEST_ASSERT_EQUAL_INT((dx > dy ? dx : dy) + 1, kek_test_frame_count(&e, INK));
        }
    }
    kek_test_frame_assert_guards();
}

/* ---- Triangles ---- */

static void triangle(KEK_IVec2 a, KEK_IVec2 b, KEK_IVec2 c) {
    KEK_IVec2 v[3];
    v[0] = a;
    v[1] = b;
    v[2] = c;
    kek_2d_triangle(&e, v, INK);
}

static void triangle_border(KEK_IVec2 a, KEK_IVec2 b, KEK_IVec2 c) {
    KEK_IVec2 v[3];
    v[0] = a;
    v[1] = b;
    v[2] = c;
    kek_2d_triangle_border(&e, v, INK, BORDER);
}

/* 349beae: before it, the four of these painted 320, 320, 91 and 91 pixels
   at 320x200, glued to the edge each one fell off. */
void test_triangles_wholly_off_each_edge_draw_nothing(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        int w, h;
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        w = e.w;
        h = e.h;
        triangle(at(10, -300), at(w - 10, -250), at(w / 2, -200));
        triangle(at(10, h + 200), at(w - 10, h + 250), at(w / 2, h + 300));
        triangle(at(-300, 5), at(-200, h / 2), at(-250, h - 5));
        triangle(at(w + 200, 5), at(w + 300, h / 2), at(w + 250, h - 5));
        triangle_border(at(10, -300), at(w - 10, -250), at(w / 2, -200));
        triangle_border(at(w + 200, 5), at(w + 300, h / 2), at(w + 250, h - 5));
        TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
        kek_test_frame_assert_guards();
    }
}

void test_triangles_hanging_off_every_edge_stay_in_the_frame(void) {
    size_t s;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        n = hanging_points(e.w, e.h, 8, points);
        for (i = 0; i < n; ++i) {
            KEK_IVec2 p = points[i];
            triangle(at(p.x - 30, p.y - 20), at(p.x + 25, p.y - 5), at(p.x - 5, p.y + 30));
            triangle_border(at(p.x + 30, p.y + 20), at(p.x - 25, p.y + 5), at(p.x + 5, p.y - 30));
        }
        /* One across two corners at once. */
        triangle(at(-20, -20), at(e.w + 20, e.h / 2), at(e.w / 3, e.h + 20));
        kek_test_frame_assert_guards();
    }
}

void test_degenerate_triangles_stay_in_the_frame(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        int w, h;
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        w = e.w;
        h = e.h;
        /* A point, a horizontal line, a sloped line and a vertical one, each
           both on screen and running off it. */
        triangle(at(w / 2, h / 2), at(w / 2, h / 2), at(w / 2, h / 2));
        triangle(at(-1, -1), at(-1, -1), at(-1, -1));
        triangle(at(-50, 3), at(w / 2, 3), at(w + 50, 3));
        triangle(at(-50, 3), at(w + 50, 3), at(w / 2, 3));
        triangle(at(-40, -40), at(20, 20), at(w + 40, w + 40));
        triangle(at(7, -60), at(7, h / 2), at(7, h + 60));
        triangle(at(w, h), at(w, h), at(w, h));
        kek_test_frame_assert_guards();
    }
}

void test_a_triangle_larger_than_the_frame_fills_it(void) {
    size_t s;

    for (s = 0; s < SIZE_COUNT; ++s) {
        int w, h;
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        w = e.w;
        h = e.h;
        triangle(at(-10 * w, -10 * h), at(10 * w, -10 * h), at(w / 2, 10 * h));
        TEST_ASSERT_EQUAL_INT(w * h, kek_test_frame_count(&e, INK));
        kek_test_frame_assert_guards();
    }
}

/* ---- Text ---- */

void test_text_off_every_edge_stays_in_the_frame(void) {
    size_t s;
    int i, n;

    for (s = 0; s < SIZE_COUNT; ++s) {
        KEK_IVec2 points[8];
        kek_test_frame_attach(&e, SIZES[s].w, SIZES[s].h);
        n = hanging_points(e.w, e.h, 3, points);
        for (i = 0; i < n; ++i) {
            kek_2d_text_5x8(&e, &KEK_FONT_DEFAULT_5X8, points[i], "#@W\nM8#", INK);
            kek_2d_text_5x8(&e, &KEK_FONT_DEFAULT_5X8, at(points[i].x - 12, points[i].y - 8), "#@W\nM8#", INK);
        }
        kek_test_frame_assert_guards();
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rect_fills_the_same_pixels_whichever_way_its_corners_come);
    RUN_TEST(test_rects_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_rects_wholly_off_screen_draw_nothing);
    RUN_TEST(test_a_rect_larger_than_the_frame_fills_it);
    RUN_TEST(test_rect_border_draws_the_four_edges);
    RUN_TEST(test_circles_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_circles_wholly_off_screen_draw_nothing);
    RUN_TEST(test_a_circle_larger_than_the_frame_fills_it);
    RUN_TEST(test_lines_across_and_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_lines_wholly_off_screen_draw_nothing);
    RUN_TEST(test_a_line_includes_both_ends);
    RUN_TEST(test_triangles_wholly_off_each_edge_draw_nothing);
    RUN_TEST(test_triangles_hanging_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_degenerate_triangles_stay_in_the_frame);
    RUN_TEST(test_a_triangle_larger_than_the_frame_fills_it);
    RUN_TEST(test_text_off_every_edge_stays_in_the_frame);
    return UNITY_END();
}
