/* kek_math.c. Float, so everything here compares within a tolerance: exact
   bits are not something libm promises across toolchains (Tier 3). */

#include <math.h>
#include "unity.h"
#include "kek_math.h"
#include "kek_3d.h"

#define TOLERANCE 1e-5f

void setUp(void) {
}

void tearDown(void) {
}

static void assert_vec3_within(float tolerance, KEK_FVec3 expected, KEK_FVec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, expected.z, actual.z);
}

/* A small deterministic spread of angles, including whole turns and
   negatives, so the matrix is checked well away from the identity. */
static const KEK_FVec3 ANGLES[] = {
    { 0.f, 0.f, 0.f },
    { 1.5707964f, 0.f, 0.f },
    { 0.f, 1.5707964f, 0.f },
    { 0.f, 0.f, 1.5707964f },
    { 0.3f, -1.1f, 2.7f },
    { -2.9f, 0.4f, -0.6f },
    { 6.2831853f, -6.2831853f, 3.1415927f },
    { 10.f, 20.f, -30.f }
};
#define ANGLE_COUNT (sizeof(ANGLES) / sizeof(ANGLES[0]))

static const KEK_FVec3 POINTS[] = {
    { 1.f, 0.f, 0.f },
    { 0.f, 1.f, 0.f },
    { 0.f, 0.f, 1.f },
    { -3.f, 2.5f, 7.f },
    { 0.25f, -8.f, -1.5f }
};
#define POINT_COUNT (sizeof(POINTS) / sizeof(POINTS[0]))

void test_mat3_from_zero_euler_is_the_identity(void) {
    KEK_Mat3 m = kek_mat3_from_euler((KEK_FVec3){ 0.f, 0.f, 0.f });
    const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    int i;

    for (i = 0; i < 9; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, identity[i], m.m[i]);
    }
}

/* The contract written above kek_mat3_from_euler: the matrix is
   kek_3d_rotate, expanded. kek_3d_draw_model uses the matrix and the rest of
   kek_3d uses rotate, so the two drifting apart would put models and
   everything else in different places. */
void test_mat3_from_euler_agrees_with_kek_3d_rotate(void) {
    size_t a, p;

    for (a = 0; a < ANGLE_COUNT; ++a) {
        KEK_Mat3 m = kek_mat3_from_euler(ANGLES[a]);
        for (p = 0; p < POINT_COUNT; ++p) {
            assert_vec3_within(1e-4f, kek_3d_rotate(POINTS[p], ANGLES[a]), kek_mat3_apply(m, POINTS[p]));
        }
    }
}

void test_mat3_from_euler_is_a_rotation(void) {
    size_t a;
    int r, c, k;

    for (a = 0; a < ANGLE_COUNT; ++a) {
        KEK_Mat3 m = kek_mat3_from_euler(ANGLES[a]);
        float det;

        /* Orthonormal rows: M * M^T is the identity... */
        for (r = 0; r < 3; ++r) {
            for (c = 0; c < 3; ++c) {
                float dot = 0.f;
                for (k = 0; k < 3; ++k) {
                    dot += m.m[r * 3 + k] * m.m[c * 3 + k];
                }
                TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, r == c ? 1.f : 0.f, dot);
            }
        }

        /* ...and no reflection. */
        det = m.m[0] * (m.m[4] * m.m[8] - m.m[5] * m.m[7])
            - m.m[1] * (m.m[3] * m.m[8] - m.m[5] * m.m[6])
            + m.m[2] * (m.m[3] * m.m[7] - m.m[4] * m.m[6]);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 1.f, det);
    }
}

void test_normalize_gives_unit_length_in_the_same_direction(void) {
    KEK_FVec3 v = { 3.f, 4.f, 0.f };
    KEK_FVec3 w = { -2.f, 0.5f, 7.f };
    float length;

    kek_normalize_fvec3(&v);
    assert_vec3_within(TOLERANCE, (KEK_FVec3){ 0.6f, 0.8f, 0.f }, v);

    kek_normalize_fvec3(&w);
    length = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 1.f, length);
    TEST_ASSERT_TRUE(w.x < 0.f && w.y > 0.f && w.z > 0.f);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, -2.f / 0.5f, w.x / w.y);
}

void test_normalize_leaves_a_unit_vector_alone(void) {
    KEK_FVec3 v = { 0.f, -1.f, 0.f };

    kek_normalize_fvec3(&v);
    assert_vec3_within(0.f, (KEK_FVec3){ 0.f, -1.f, 0.f }, v);
}

/* A degenerate face has a zero cross product, and the model loader normalizes
   exactly that. It must come out as zero, not as NaN. */
void test_normalize_leaves_the_zero_vector_zero(void) {
    KEK_FVec3 v = { 0.f, 0.f, 0.f };
    KEK_FVec3 copy;

    kek_normalize_fvec3(&v);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v.x);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v.y);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v.z);

    copy = kek_normalize_fvec3_copy((KEK_FVec3){ 0.f, 0.f, 0.f });
    TEST_ASSERT_FALSE(isnan(copy.x) || isnan(copy.y) || isnan(copy.z));
}

void test_normalize_copy_leaves_its_argument_alone(void) {
    KEK_FVec3 v = { 0.f, 0.f, 5.f };
    KEK_FVec3 n = kek_normalize_fvec3_copy(v);

    assert_vec3_within(0.f, (KEK_FVec3){ 0.f, 0.f, 5.f }, v);
    assert_vec3_within(TOLERANCE, (KEK_FVec3){ 0.f, 0.f, 1.f }, n);
}

void test_signed_area_of_a_right_triangle(void) {
    KEK_IVec2 t[3] = { { 0, 0 }, { 4, 0 }, { 0, 3 } };

    TEST_ASSERT_EQUAL_FLOAT(6.f, kek_area_triangle_signed(t));
}

/* The sign is what kek_3d_draw_model culls back faces by, so reversing the
   winding has to flip it and nothing else. */
void test_signed_area_flips_with_the_winding(void) {
    KEK_IVec2 cw[3] = { { 10, 20 }, { 50, 25 }, { 30, 90 } };
    KEK_IVec2 ccw[3] = { { 10, 20 }, { 30, 90 }, { 50, 25 } };
    KEK_IVec2 rotated[3] = { { 50, 25 }, { 30, 90 }, { 10, 20 } };
    float area = kek_area_triangle_signed(cw);

    TEST_ASSERT_TRUE(area != 0.f);
    TEST_ASSERT_EQUAL_FLOAT(-area, kek_area_triangle_signed(ccw));
    TEST_ASSERT_EQUAL_FLOAT(area, kek_area_triangle_signed(rotated));
    /* Shoelace by hand: |10*(25-90) + 50*(90-20) + 30*(20-25)| / 2 = 1350. */
    TEST_ASSERT_EQUAL_FLOAT(1350.f, fabsf(area));
}

void test_signed_area_ignores_translation(void) {
    KEK_IVec2 t[3] = { { 0, 0 }, { 7, 1 }, { 2, 5 } };
    KEK_IVec2 moved[3] = { { -300, 180 }, { -293, 181 }, { -298, 185 } };

    TEST_ASSERT_EQUAL_FLOAT(kek_area_triangle_signed(t), kek_area_triangle_signed(moved));
}

void test_signed_area_of_a_degenerate_triangle_is_zero(void) {
    KEK_IVec2 point[3] = { { 5, 5 }, { 5, 5 }, { 5, 5 } };
    KEK_IVec2 line[3] = { { 0, 0 }, { 3, 6 }, { 10, 20 } };

    TEST_ASSERT_EQUAL_FLOAT(0.f, kek_area_triangle_signed(point));
    TEST_ASSERT_EQUAL_FLOAT(0.f, kek_area_triangle_signed(line));
}

void test_signed_area_is_exact_for_odd_doubled_areas(void) {
    KEK_IVec2 t[3] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };

    TEST_ASSERT_EQUAL_FLOAT(0.5f, fabsf(kek_area_triangle_signed(t)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mat3_from_zero_euler_is_the_identity);
    RUN_TEST(test_mat3_from_euler_agrees_with_kek_3d_rotate);
    RUN_TEST(test_mat3_from_euler_is_a_rotation);
    RUN_TEST(test_normalize_gives_unit_length_in_the_same_direction);
    RUN_TEST(test_normalize_leaves_a_unit_vector_alone);
    RUN_TEST(test_normalize_leaves_the_zero_vector_zero);
    RUN_TEST(test_normalize_copy_leaves_its_argument_alone);
    RUN_TEST(test_signed_area_of_a_right_triangle);
    RUN_TEST(test_signed_area_flips_with_the_winding);
    RUN_TEST(test_signed_area_ignores_translation);
    RUN_TEST(test_signed_area_of_a_degenerate_triangle_is_zero);
    RUN_TEST(test_signed_area_is_exact_for_odd_doubled_areas);
    return UNITY_END();
}
