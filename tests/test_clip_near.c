/* kek_3d_clip_near, the Sutherland–Hodgman pass against z = near.

   It is static in kek_3d.c, and making it public only so a test can call it
   would put an internal helper in kek_3d.h. So the file is compiled into this
   test directly instead. Every symbol it defines is then defined here, which
   means the linker never needs kek_3d.o from the kek archive and there is no
   clash; everything else still comes from the library. clang-tidy flags
   including a .c file, rightly in general and deliberately here. */
#include "../kek/kek_3d.c" // NOLINT(bugprone-suspicious-include)

#include "unity.h"

#define NEAR_PLANE 1.f
#define EPS 1e-5f

static KEK_FVec3 out_v[KEK_3D_CLIP_NEAR_MAX_VERTS];
static KEK_FVec2 out_uv[KEK_3D_CLIP_NEAR_MAX_VERTS];

void setUp(void) {
    int i;
    for (i = 0; i < KEK_3D_CLIP_NEAR_MAX_VERTS; ++i) {
        out_v[i] = (KEK_FVec3){ -999.f, -999.f, -999.f };
        out_uv[i] = (KEK_FVec2){ -999.f, -999.f };
    }
}

void tearDown(void) {
}

static KEK_FVec3 v3(float x, float y, float z) {
    KEK_FVec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static KEK_FVec2 v2(float x, float y) {
    KEK_FVec2 v;
    v.x = x;
    v.y = y;
    return v;
}

static int clip(KEK_FVec3 a, KEK_FVec3 b, KEK_FVec3 c) {
    KEK_FVec3 v[3];
    KEK_FVec2 uv[3];
    v[0] = a;
    v[1] = b;
    v[2] = c;
    /* UV = (x, y) of the vertex, so wherever an output vertex lands its UV
       can be checked against its own position. */
    uv[0] = v2(a.x, a.y);
    uv[1] = v2(b.x, b.y);
    uv[2] = v2(c.x, c.y);
    return kek_3d_clip_near(v, uv, NEAR_PLANE, out_v, out_uv);
}

static void assert_vertex(float x, float y, float z, int i) {
    TEST_ASSERT_FLOAT_WITHIN(EPS, x, out_v[i].x);
    TEST_ASSERT_FLOAT_WITHIN(EPS, y, out_v[i].y);
    TEST_ASSERT_FLOAT_WITHIN(EPS, z, out_v[i].z);
    TEST_ASSERT_FLOAT_WITHIN(EPS, x, out_uv[i].x);
    TEST_ASSERT_FLOAT_WITHIN(EPS, y, out_uv[i].y);
}

void test_a_triangle_wholly_in_front_is_kept_as_it_is(void) {
    TEST_ASSERT_EQUAL_INT(3, clip(v3(0, 0, 2), v3(1, 0, 3), v3(0, 1, 5)));
    assert_vertex(0, 0, 2, 0);
    assert_vertex(1, 0, 3, 1);
    assert_vertex(0, 1, 5, 2);
}

void test_a_triangle_wholly_behind_is_dropped(void) {
    TEST_ASSERT_EQUAL_INT(0, clip(v3(0, 0, 0.5f), v3(1, 0, -3), v3(0, 1, 0)));
    TEST_ASSERT_EQUAL_INT(0, clip(v3(0, 0, -1), v3(1, 0, -1), v3(0, 1, -1)));
}

/* z > near is in front; a vertex exactly on the plane is not. */
void test_a_triangle_lying_on_the_plane_is_dropped(void) {
    TEST_ASSERT_EQUAL_INT(0, clip(v3(0, 0, NEAR_PLANE), v3(1, 0, NEAR_PLANE), v3(0, 1, NEAR_PLANE)));
}

/* One vertex behind: the two edges leaving it are cut, which leaves a quad. */
void test_one_vertex_behind_gives_four(void) {
    TEST_ASSERT_EQUAL_INT(4, clip(v3(0, 0, 2), v3(2, 0, 2), v3(0, 2, 0)));
    assert_vertex(0, 0, 2, 0);
    assert_vertex(2, 0, 2, 1);
    assert_vertex(1, 1, NEAR_PLANE, 2);
    assert_vertex(0, 1, NEAR_PLANE, 3);
}

/* Two behind: only the corner in front survives, with its two edges cut. */
void test_two_vertices_behind_gives_three(void) {
    TEST_ASSERT_EQUAL_INT(3, clip(v3(0, 0, 3), v3(4, 0, -1), v3(0, 4, -1)));
    assert_vertex(0, 0, 3, 0);
    assert_vertex(2, 0, NEAR_PLANE, 1);
    assert_vertex(0, 2, NEAR_PLANE, 2);
}

void test_a_vertex_on_the_plane_with_the_rest_in_front_gives_four(void) {
    /* The on-plane vertex counts as behind, so both of its edges are cut —
       at the vertex itself. The quad is degenerate but still a quad. */
    TEST_ASSERT_EQUAL_INT(4, clip(v3(0, 0, 2), v3(1, 0, 2), v3(0, 1, NEAR_PLANE)));
    assert_vertex(0, 1, NEAR_PLANE, 2);
    assert_vertex(0, 1, NEAR_PLANE, 3);
}

/* ---- Sweep ---- */

static unsigned sweep_state = 777u;

static float sweep_float(float lo, float hi) {
    sweep_state = sweep_state * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(sweep_state >> 8) / 16777216.f;
}

/* Every output vertex has to lie on the input triangle — z at or in front of
   the plane, and within the triangle's bounds — and the count has to follow
   from how many vertices were in front: 0 -> 0, 1 -> 3, 2 -> 4, 3 -> 3. */
void test_sweep_counts_and_positions(void) {
    const int expected[4] = { 0, 3, 4, 3 };
    int n;

    for (n = 0; n < 5000; ++n) {
        KEK_FVec3 a = v3(sweep_float(-5, 5), sweep_float(-5, 5), sweep_float(-3, 5));
        KEK_FVec3 b = v3(sweep_float(-5, 5), sweep_float(-5, 5), sweep_float(-3, 5));
        KEK_FVec3 c = v3(sweep_float(-5, 5), sweep_float(-5, 5), sweep_float(-3, 5));
        int in_front = (a.z > NEAR_PLANE) + (b.z > NEAR_PLANE) + (c.z > NEAR_PLANE);
        float min_x = KEK_MIN(a.x, KEK_MIN(b.x, c.x)), max_x = KEK_MAX(a.x, KEK_MAX(b.x, c.x));
        float min_y = KEK_MIN(a.y, KEK_MIN(b.y, c.y)), max_y = KEK_MAX(a.y, KEK_MAX(b.y, c.y));
        float max_z = KEK_MAX(a.z, KEK_MAX(b.z, c.z));
        int count = clip(a, b, c);
        int i;

        TEST_ASSERT_EQUAL_INT(expected[in_front], count);
        for (i = 0; i < count; ++i) {
            TEST_ASSERT_TRUE(out_v[i].z >= NEAR_PLANE - EPS && out_v[i].z <= max_z + EPS);
            TEST_ASSERT_TRUE(out_v[i].x >= min_x - EPS && out_v[i].x <= max_x + EPS);
            TEST_ASSERT_TRUE(out_v[i].y >= min_y - EPS && out_v[i].y <= max_y + EPS);
            /* The UV was interpolated with the same t as the position. */
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, out_v[i].x, out_uv[i].x);
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, out_v[i].y, out_uv[i].y);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_triangle_wholly_in_front_is_kept_as_it_is);
    RUN_TEST(test_a_triangle_wholly_behind_is_dropped);
    RUN_TEST(test_a_triangle_lying_on_the_plane_is_dropped);
    RUN_TEST(test_one_vertex_behind_gives_four);
    RUN_TEST(test_two_vertices_behind_gives_three);
    RUN_TEST(test_a_vertex_on_the_plane_with_the_rest_in_front_gives_four);
    RUN_TEST(test_sweep_counts_and_positions);
    return UNITY_END();
}
