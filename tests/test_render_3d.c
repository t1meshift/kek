/* The 3D rasteriser and depth test against a guarded frame. As in
   test_render_2d, nothing here compares pixels to a reference picture; the
   assertions are the ones float cannot move: nothing outside the frame, the
   nearer surface wins whatever the draw order, what is off screen or behind
   the camera draws nothing, and what covers the screen covers all of it. */

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
    e = kek_init();
    kek_test_frame_attach(&e, KEK_BUFFER_WIDTH, KEK_BUFFER_HEIGHT);
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
    static KEK_FVec3 verts[4] = {
        { 35.f, -2.f, -50.f }, { 35.f, 80.f, -50.f },
        { 35.f, 80.f,  50.f }, { 35.f, -2.f,  50.f }
    };
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_nearer_surface_wins_drawn_last);
    RUN_TEST(test_the_nearer_surface_wins_drawn_first);
    RUN_TEST(test_the_far_surface_shows_where_nothing_nearer_is);
    RUN_TEST(test_a_vertex_blit_respects_depth_and_the_frame);
    RUN_TEST(test_triangles_wholly_off_each_edge_draw_nothing);
    RUN_TEST(test_triangles_hanging_off_every_edge_stay_in_the_frame);
    RUN_TEST(test_degenerate_triangles_stay_in_the_frame);
    RUN_TEST(test_a_triangle_larger_than_the_frame_fills_it);
    RUN_TEST(test_a_textured_triangle_larger_than_the_frame_fills_it_in_both_wrap_modes);
    RUN_TEST(test_a_cube_in_front_of_the_camera_draws);
    RUN_TEST(test_a_cube_behind_the_camera_or_past_the_far_plane_draws_nothing);
    RUN_TEST(test_cubes_through_the_near_plane_and_off_the_edges_stay_in_the_frame);
    RUN_TEST(test_a_huge_wall_across_the_near_plane_is_culled_by_its_winding_alone);
    return UNITY_END();
}
