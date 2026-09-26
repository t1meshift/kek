/* kek_2d_clip_line: Cohen–Sutherland-style clipping of an integer segment to
   an inclusive rectangle. Hand-picked cases first, then a sweep of segments
   checked against a straightforward Liang–Barsky reference in double. */

#include <math.h>
#include <stdio.h>
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

/* Ends in two different outside regions, so neither trivial test applies, but
   the segment passes clear of the rectangle beyond a corner. Each of these
   misses by about seven pixels. */
void test_a_segment_passing_outside_a_corner_is_rejected(void) {
    const KEK_IVec2 cases[4][2] = {
        { { XMIN - 20, YMIN + 10 }, { XMIN + 10, YMIN - 20 } }, /* top left */
        { { XMAX + 20, YMIN + 10 }, { XMAX - 10, YMIN - 20 } }, /* top right */
        { { XMIN - 20, YMAX - 10 }, { XMIN + 10, YMAX + 20 } }, /* bottom left */
        { { XMAX + 20, YMAX - 10 }, { XMAX - 10, YMAX + 20 } }  /* bottom right */
    };
    int i;

    for (i = 0; i < 4; ++i) {
        KEK_IVec2 p0 = cases[i][0], p1 = cases[i][1];
        KEK_IVec2 q0 = cases[i][1], q1 = cases[i][0];
        TEST_ASSERT_FALSE(clip(&p0, &p1, VIEW));
        TEST_ASSERT_FALSE(clip(&q0, &q1, VIEW));
    }
}

/* ---- Sweep against a reference ---- */

/* Liang–Barsky on the closed rectangle [xmin-grow, xmax+grow] x ..., in
   double. Returns whether the segment touches it and, if so, the parameters
   of the visible part. */
static int reference_clip(double x0, double y0, double x1, double y1, double grow,
                          double* t_enter, double* t_leave) {
    const double p[4] = { -(x1 - x0), x1 - x0, -(y1 - y0), y1 - y0 };
    const double q[4] = {
        x0 - (XMIN - grow), (XMAX + grow) - x0,
        y0 - (YMIN - grow), (YMAX + grow) - y0
    };
    double t0 = 0.0, t1 = 1.0;
    int i;

    for (i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return 0;
            }
            continue;
        }
        {
            double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > t1) return 0;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return 0;
                if (t < t1) t1 = t;
            }
        }
    }

    *t_enter = t0;
    *t_leave = t1;
    return 1;
}

static unsigned sweep_state = 12345u;

static int sweep_coordinate(int lo, int hi) {
    /* Numerical Recipes LCG: deterministic on every target, which rand() is
       not. */
    sweep_state = sweep_state * 1664525u + 1013904223u;
    return lo + (int)((sweep_state >> 8) % (unsigned)(hi - lo + 1));
}

/* Every accepted segment ends inside the rectangle and on the original line;
   every segment that crosses the rectangle is accepted; and every segment
   that passes clear of it — by more than a pixel — is rejected. The pixel of
   slack is for rounding at a corner, where both answers are defensible. */
void test_sweep_agrees_with_a_reference_clipper(void) {
    int n;

    for (n = 0; n < 20000; ++n) {
        KEK_IVec2 a = { sweep_coordinate(-400, 740), sweep_coordinate(-300, 540) };
        KEK_IVec2 b = { sweep_coordinate(-400, 740), sweep_coordinate(-300, 540) };
        KEK_IVec2 p0 = a, p1 = b;
        double t_enter, t_leave;
        int touches = reference_clip(a.x, a.y, b.x, b.y, 0.0, &t_enter, &t_leave);
        int near = reference_clip(a.x, a.y, b.x, b.y, 1.0, &t_enter, &t_leave);
        char accepted = clip(&p0, &p1, VIEW);
        char message[160];

        (void)snprintf(message, sizeof(message), "segment (%d,%d)-(%d,%d)", a.x, a.y, b.x, b.y);

        if (touches) {
            TEST_ASSERT_TRUE_MESSAGE(accepted, message);
        }
        if (!near) {
            TEST_ASSERT_FALSE_MESSAGE(accepted, message);
        }
        if (!accepted) {
            continue;
        }

        TEST_ASSERT_TRUE_MESSAGE(p0.x >= XMIN && p0.x <= XMAX && p0.y >= YMIN && p0.y <= YMAX, message);
        TEST_ASSERT_TRUE_MESSAGE(p1.x >= XMIN && p1.x <= XMAX && p1.y >= YMIN && p1.y <= YMAX, message);

        /* Both clipped ends lie on the original line, to within rounding. */
        {
            double dx = b.x - a.x, dy = b.y - a.y;
            double length = sqrt(dx * dx + dy * dy);
            if (length > 0.0) {
                double d0 = fabs((p0.x - a.x) * dy - (p0.y - a.y) * dx) / length;
                double d1 = fabs((p1.x - a.x) * dy - (p1.y - a.y) * dx) / length;
                TEST_ASSERT_TRUE_MESSAGE(d0 <= 1.5 && d1 <= 1.5, message);
            }
        }

        /* Where the crossing is not marginal, the ends are where the
           reference puts them. */
        if (reference_clip(a.x, a.y, b.x, b.y, -1.0, &t_enter, &t_leave)) {
            reference_clip(a.x, a.y, b.x, b.y, 0.0, &t_enter, &t_leave);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, (float)(a.x + t_enter * (b.x - a.x)), (float)p0.x, message);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, (float)(a.y + t_enter * (b.y - a.y)), (float)p0.y, message);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, (float)(a.x + t_leave * (b.x - a.x)), (float)p1.x, message);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, (float)(a.y + t_leave * (b.y - a.y)), (float)p1.y, message);
        }
    }
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
    RUN_TEST(test_a_segment_passing_outside_a_corner_is_rejected);
    RUN_TEST(test_sweep_agrees_with_a_reference_clipper);
    return UNITY_END();
}
