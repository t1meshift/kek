/* kek_2d_clip_line: Cohen–Sutherland-style clipping of an integer segment to
   an inclusive rectangle. */

#include "unity.h"
#include "kek_2d.h"

/* 320x200 with an origin away from zero, so a clipper that assumed the
   rectangle starts at (0, 0) gets caught. Inclusive: x 10..329, y 20..219. */
static const KEK_IRect2 VIEW = { { 10, 20 }, { 320, 200 } };
#define XMIN 10
#define YMIN 20
#define XMAX 329
#define YMAX 219

void setUp(void) {
}

void tearDown(void) {
}

static char clip(KEK_IVec2* p0, KEK_IVec2* p1, KEK_IRect2 v) {
    return kek_2d_clip_line(p0, p1, v);
}

static void assert_point(int x, int y, KEK_IVec2 p) {
    TEST_ASSERT_EQUAL_INT(x, p.x);
    TEST_ASSERT_EQUAL_INT(y, p.y);
}

void test_a_segment_inside_is_left_alone(void) {
    KEK_IVec2 p0 = { 50, 60 }, p1 = { 300, 100 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, VIEW));
    assert_point(50, 60, p0);
    assert_point(300, 100, p1);
}

void test_the_rectangle_is_inclusive_on_every_edge(void) {
    KEK_IVec2 p0 = { XMIN, YMIN }, p1 = { XMAX, YMAX };

    TEST_ASSERT_TRUE(clip(&p0, &p1, VIEW));
    assert_point(XMIN, YMIN, p0);
    assert_point(XMAX, YMAX, p1);
}

void test_segments_wholly_off_one_side_are_rejected(void) {
    KEK_IVec2 left0 = { 0, 50 }, left1 = { XMIN - 1, 150 };
    KEK_IVec2 right0 = { XMAX + 1, 50 }, right1 = { 1000, 150 };
    KEK_IVec2 above0 = { 50, -100 }, above1 = { 200, YMIN - 1 };
    KEK_IVec2 below0 = { 50, YMAX + 1 }, below1 = { 200, 900 };

    TEST_ASSERT_FALSE(clip(&left0, &left1, VIEW));
    TEST_ASSERT_FALSE(clip(&right0, &right1, VIEW));
    TEST_ASSERT_FALSE(clip(&above0, &above1, VIEW));
    TEST_ASSERT_FALSE(clip(&below0, &below1, VIEW));
}

void test_a_horizontal_segment_across_the_view_is_cut_at_both_sides(void) {
    KEK_IVec2 p0 = { -500, 100 }, p1 = { 900, 100 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, VIEW));
    assert_point(XMIN, 100, p0);
    assert_point(XMAX, 100, p1);
}

void test_a_vertical_segment_across_the_view_is_cut_at_both_ends(void) {
    KEK_IVec2 p0 = { 100, 1000 }, p1 = { 100, -1000 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, VIEW));
    assert_point(100, YMAX, p0);
    assert_point(100, YMIN, p1);
}

void test_one_end_outside_is_moved_onto_the_edge(void) {
    KEK_IVec2 p0 = { 100, 100 }, p1 = { 1000, 100 };
    KEK_IVec2 q0 = { 100, 100 }, q1 = { 100, -300 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, VIEW));
    assert_point(100, 100, p0);
    assert_point(XMAX, 100, p1);

    TEST_ASSERT_TRUE(clip(&q0, &q1, VIEW));
    assert_point(100, 100, q0);
    assert_point(100, YMIN, q1);
}

void test_a_diagonal_through_two_corners(void) {
    const KEK_IRect2 square = { { 0, 0 }, { 100, 100 } };
    KEK_IVec2 p0 = { -10, -10 }, p1 = { 110, 110 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, square));
    assert_point(0, 0, p0);
    assert_point(99, 99, p1);
}

void test_a_diagonal_entering_through_a_side_and_leaving_through_the_top(void) {
    const KEK_IRect2 square = { { 0, 0 }, { 100, 100 } };
    /* y = 50 - x, from left of the square to above it. */
    KEK_IVec2 p0 = { -10, 60 }, p1 = { 80, -30 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, square));
    assert_point(0, 50, p0);
    assert_point(50, 0, p1);
}

void test_a_single_pixel_rectangle(void) {
    const KEK_IRect2 pixel = { { 5, 5 }, { 1, 1 } };
    KEK_IVec2 p0 = { 0, 5 }, p1 = { 10, 5 };
    KEK_IVec2 q0 = { 5, 5 }, q1 = { 5, 5 };

    TEST_ASSERT_TRUE(clip(&p0, &p1, pixel));
    assert_point(5, 5, p0);
    assert_point(5, 5, p1);
    TEST_ASSERT_TRUE(clip(&q0, &q1, pixel));
}

void test_invalid_arguments_are_rejected(void) {
    KEK_IVec2 p0 = { 1, 1 }, p1 = { 2, 2 };
    const KEK_IRect2 empty_w = { { 0, 0 }, { 0, 10 } };
    const KEK_IRect2 empty_h = { { 0, 0 }, { 10, 0 } };
    const KEK_IRect2 negative = { { 0, 0 }, { -5, 10 } };

    TEST_ASSERT_FALSE(clip(0, &p1, VIEW));
    TEST_ASSERT_FALSE(clip(&p0, 0, VIEW));
    TEST_ASSERT_FALSE(clip(&p0, &p1, empty_w));
    TEST_ASSERT_FALSE(clip(&p0, &p1, empty_h));
    TEST_ASSERT_FALSE(clip(&p0, &p1, negative));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_segment_inside_is_left_alone);
    RUN_TEST(test_the_rectangle_is_inclusive_on_every_edge);
    RUN_TEST(test_segments_wholly_off_one_side_are_rejected);
    RUN_TEST(test_a_horizontal_segment_across_the_view_is_cut_at_both_sides);
    RUN_TEST(test_a_vertical_segment_across_the_view_is_cut_at_both_ends);
    RUN_TEST(test_one_end_outside_is_moved_onto_the_edge);
    RUN_TEST(test_a_diagonal_through_two_corners);
    RUN_TEST(test_a_diagonal_entering_through_a_side_and_leaving_through_the_top);
    RUN_TEST(test_a_single_pixel_rectangle);
    RUN_TEST(test_invalid_arguments_are_rejected);
    return UNITY_END();
}
