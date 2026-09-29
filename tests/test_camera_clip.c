/* Public camera and model paths: extreme geometry must be clipped before any
   integer conversion, without sacrificing the visible part of the face. */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_3d.h"
#include "kek_model.h"
#include "test_support.h"

static KEK_engine e;
static KEK_camera camera;

void setUp(void) {
    kek_test_init(&e);
    kek_test_frame_attach(&e, KEK_TEST_WIDTH, KEK_TEST_HEIGHT);
    camera = KEK_DEFAULT_CAMERA;
    kek_3d_set_light(&e, (KEK_FVec3){0.f, 0.f, 1.f}, 1.f);
}

void tearDown(void) {
    kek_test_frame_assert_guards();
}

static void triangle(const KEK_FVec3 positions[3], int reverse, int textured) {
    KEK_model model;
    KEK_model_vertex vertices[3];
    KEK_model_face face = {0, 1, 2};
    KEK_FVec2 uvs[3] = {{0.f, 0.f}, {0.5f, 1.f}, {1.f, 0.f}};
    KEK_model_face_uv face_uv = {0, 1, 2};
    size_t available = kek_arena_available(&e);
    if (reverse) { face.b = 2; face.c = 1; }
    memset(&model, 0, sizeof(model));
    model.verts = vertices;
    model.verts_count = 3;
    model.faces = &face;
    model.faces_count = 1;
    if (textured) {
        model.texture = kek_default_texture_handle(&e);
        model.uvs = uvs;
        model.uvs_count = 3;
        model.face_uvs = &face_uv;
        model.face_uvs_count = 1;
    }
    kek_model_quantise(&model, positions);
    kek_3d_begin_view(&e, &camera);
    kek_3d_draw_model(&e, &model, (KEK_Transform3D){(KEK_FVec3){0.f, 0.f, 0.f}, (KEK_FVec3){0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
}

void test_huge_triangle_preserves_visible_part_and_winding(void) {
    const KEK_FVec3 positions[3] = {{-1e8f, -1e8f, 3.f}, {0.f, 1e8f, 3.f}, {1e8f, -1e8f, 3.f}};
    triangle(positions, 0, 0);
    TEST_ASSERT_EQUAL_INT(e.w * e.h, kek_test_frame_painted(&e));
    kek_flush_buffers(&e);
    triangle(positions, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
}

void test_extreme_finite_geometry_keeps_depth_and_arena_safe(void) {
    const KEK_FVec3 positions[3] = {{-1e20f, -1e20f, 3.f}, {-1e20f, 1e20f, 3.f}, {1e20f, -1e20f, 3.f}};
    size_t i;
    triangle(positions, 0, 0);
    TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    for (i = 0; i < (size_t)e.w * e.h; ++i) {
        if (e.fb[i]) TEST_ASSERT_EQUAL_UINT16((uint16_t)(KEK_3D_DEPTH_SCALE / 3.f), e.db[i]);
    }
}

void test_huge_triangle_clips_each_side_and_near_plane(void) {
    const KEK_FVec3 original[3] = {{-1e8f, -1e8f, -1.f}, {0.f, 1e8f, 3.f}, {1e8f, -1e8f, 3.f}};
    int side, i;
    for (side = 0; side < 4; ++side) {
        KEK_FVec3 positions[3];
        for (i = 0; i < 3; ++i) {
            positions[i] = original[i];
            if (side & 1) {
                positions[i].x = -original[i].y;
                positions[i].y = original[i].x;
            }
            if (side & 2) { positions[i].x = -positions[i].x; positions[i].y = -positions[i].y; }
        }
        kek_flush_buffers(&e);
        triangle(positions, 0, 0);
        TEST_ASSERT_GREATER_THAN_INT(0, kek_test_frame_painted(&e));
    }
}

void test_huge_triangles_outside_each_side_draw_nothing(void) {
    const KEK_FVec3 original[3] = {{1e8f, -1e8f, 3.f}, {1e8f, 1e8f, 3.f}, {2e8f, -1e8f, 3.f}};
    int side, i;
    for (side = 0; side < 4; ++side) {
        KEK_FVec3 positions[3];
        for (i = 0; i < 3; ++i) {
            positions[i] = original[i];
            if (side & 1) { positions[i].x = -original[i].y; positions[i].y = original[i].x; }
            if (side & 2) { positions[i].x = -positions[i].x; positions[i].y = -positions[i].y; }
        }
        triangle(positions, 0, 0);
    }
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
}

void test_clipped_texture_and_fog_keep_their_attributes(void) {
    const KEK_FVec3 positions[3] = {{-1e8f, -1e8f, 3.f}, {0.f, 1e8f, 3.f}, {1e8f, -1e8f, 3.f}};
    static uint8_t unshaded[KEK_TEST_WIDTH * KEK_TEST_HEIGHT];
    size_t i;
    int fogged = 0, textured = 0;
    triangle(positions, 0, 1);
    memcpy(unshaded, e.fb, sizeof(unshaded));
    /* All depths are three: every covered pixel must have the same depth. */
    for (i = 0; i < sizeof(unshaded); ++i) TEST_ASSERT_GREATER_THAN_UINT16(0, e.db[i]);
    kek_flush_buffers(&e);
    kek_3d_set_fog(&e, 2.f, 4.f);
    kek_3d_set_fog_color(&e, 77);
    triangle(positions, 0, 1);
    for (i = 0; i < sizeof(unshaded); ++i) {
        TEST_ASSERT_TRUE(e.fb[i] == 77 || e.fb[i] == unshaded[i]);
        fogged += e.fb[i] == 77;
        textured += e.fb[i] == unshaded[i] && e.fb[i] != 77;
    }
    TEST_ASSERT_GREATER_THAN_INT(0, fogged);
    TEST_ASSERT_GREATER_THAN_INT(0, textured);
}

void test_near_clipping_recomputes_fog_at_new_vertices(void) {
    const KEK_FVec3 positions[3] = {{-1.f, -1.f, 0.05f}, {0.f, 1.f, 3.f}, {1.f, -1.f, 3.f}};
    int i, painted = 0;
    kek_3d_set_fog(&e, -1.f, camera.near_plane);
    kek_3d_set_fog_color(&e, 77);
    triangle(positions, 0, 0);
    for (i = 0; i < e.w * e.h; ++i) {
        if (e.db[i]) {
            ++painted;
            TEST_ASSERT_EQUAL_UINT8(77, e.fb[i]);
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(0, painted);
}

void test_public_projection_rejects_invalid_input_without_writing(void) {
    KEK_3D_ProjectedVertex output, before;
    const KEK_FVec3 points[] = {{1e8f, 0.f, 1.f}, {NAN, 0.f, 1.f}, {0.f, INFINITY, 1.f},
        {0.f, 0.f, -1.f}, {FLT_MAX, FLT_MAX, 1.f}};
    size_t i;
    memset(&before, 0x5a, sizeof(before));
    for (i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        output = before;
        TEST_ASSERT_FALSE(kek_3d_project_vertex(&e, &camera, points[i], &output));
        TEST_ASSERT_EQUAL_MEMORY(&before, &output, sizeof(output));
    }
}

void test_invalid_cameras_and_models_leave_frame_and_arena_unchanged(void) {
    KEK_camera invalid[9];
    KEK_model* model = kek_model_get(&e, kek_default_cube_model_handle(&e));
    KEK_3D_ProjectedVertex output, before;
    KEK_FVec3 zero = {0.f, 0.f, 0.f};
    size_t i, available = kek_arena_available(&e);
    for (i = 0; i < 9; ++i) invalid[i] = camera;
    invalid[0].fov = 0.f; invalid[1].fov = 180.f; invalid[2].fov = NAN;
    invalid[3].near_plane = 0.f; invalid[4].far_plane = invalid[4].near_plane;
    invalid[5].position.x = INFINITY; invalid[6].rotation.y = NAN;
    invalid[7].far_plane = INFINITY; invalid[8].near_plane = NAN;
    memset(&before, 0x5a, sizeof(before));
    for (i = 0; i < 9; ++i) {
        output = before;
        TEST_ASSERT_FALSE(kek_3d_project_vertex(&e, &invalid[i], (KEK_FVec3){0.f, 0.f, 3.f}, &output));
        TEST_ASSERT_EQUAL_MEMORY(&before, &output, sizeof(output));
        TEST_ASSERT_FALSE(kek_3d_begin_view(&e, &invalid[i]));
        kek_3d_draw_model(&e, model, (KEK_Transform3D){(KEK_FVec3){0.f, 0.f, 3.f}, zero, {1.f, 1.f, 1.f}});
        TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
    }
    kek_3d_begin_view(&e, &camera);
    kek_3d_draw_model(&e, model, (KEK_Transform3D){(KEK_FVec3){NAN, 0.f, 3.f}, zero, {1.f, 1.f, 1.f}});
    kek_3d_draw_model(&e, model, (KEK_Transform3D){zero, (KEK_FVec3){0.f, INFINITY, 0.f}, {1.f, 1.f, 1.f}});
    kek_3d_draw_model(&e, model, (KEK_Transform3D){zero, zero, {1.f, NAN, 1.f}});
    {
        KEK_model bad = *model;
        bad.scale.x = INFINITY;
        kek_3d_draw_model(&e, &bad, (KEK_Transform3D){zero, zero, {1.f, 1.f, 1.f}});
        bad.scale.x = FLT_MAX;
        kek_3d_draw_model(&e, &bad, (KEK_Transform3D){zero, zero, {1.f, 1.f, 1.f}});
    }
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    for (i = 0; i < (size_t)e.w * e.h; ++i) TEST_ASSERT_EQUAL_UINT16(0, e.db[i]);
}

void test_direct_rasterizers_reject_unbounded_screen_coordinates(void) {
    KEK_3D_ProjectedVertex v[3] = {
        {{INT_MIN, 0}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f},
        {{INT_MAX, INT_MAX}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f},
        {{0, INT_MIN}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f}};
    kek_3d_triangle(&e, v, 15);
    kek_3d_triangle_textured(&e, v, kek_texture_get(&e, kek_default_texture_handle(&e)));
    kek_3d_triangle_border(&e, v, 15, 15);
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
}

void test_composed_camera_projection_agrees_with_model_and_light(void) {
    KEK_FVec3 positions[3] = {{-1.f, -1.f, 3.f}, {0.f, 1.f, 3.f}, {1.f, -1.f, 3.f}};
    KEK_FVec3 rotation = {0.4f, 0.7f, 0.2f};
    KEK_FVec3 offset = {3.f, -2.f, 1.f};
    KEK_3D_ProjectedVertex expected[3], actual;
    size_t i;
    for (i = 0; i < 3; ++i) TEST_ASSERT_TRUE(kek_3d_project_vertex(&e, &camera, positions[i], &expected[i]));
    camera.rotation = rotation;
    camera.position = offset;
    for (i = 0; i < 3; ++i) {
        positions[i] = kek_3d_translate(kek_3d_rotate(positions[i], rotation), offset);
        TEST_ASSERT_TRUE(kek_3d_project_vertex(&e, &camera, positions[i], &actual));
        TEST_ASSERT_INT_WITHIN(1, expected[i].screen.x, actual.screen.x);
        TEST_ASSERT_INT_WITHIN(1, expected[i].screen.y, actual.screen.y);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected[i].inv_z, actual.inv_z);
    }
    kek_3d_set_light(&e, kek_3d_rotate((KEK_FVec3){0.f, 0.f, 1.f}, rotation), 0.f);
    triangle(positions, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(15, e.fb[(size_t)(e.h / 2) * e.w + e.w / 2]);
    TEST_ASSERT_UINT16_WITHIN(30, (uint16_t)(KEK_3D_DEPTH_SCALE / 3.f), e.db[(size_t)(e.h / 2) * e.w + e.w / 2]);
}

void test_side_clipping_interpolates_uvs_in_view_space(void) {
    KEK_model model;
    KEK_model_vertex vertices[3] = {{0, 0, 0}, {0, 255, 0}, {255, 0, 0}};
    KEK_model_face face = {0, 1, 2};
    KEK_model_face_uv face_uv = {0, 1, 2};
    KEK_FVec2 uvs[3] = {{-12500000.f, -12500000.f}, {-12500000.f, 12500000.f}, {12500000.f, -12500000.f}};
    KEK_TextureHandle handle = kek_texture_create(&e, 3, 5);
    KEK_texture* texture = kek_texture_get(&e, handle);
    float focal = 1.f / tanf(camera.fov * 3.14159265358979323846f / 360.f);
    int mode, x, y, i;
    TEST_ASSERT_NOT_NULL(texture);
    for (i = 0; i < 15; ++i) texture->data[i] = (uint8_t)(i + 1);
    memset(&model, 0, sizeof(model));
    model.verts = vertices; model.verts_count = 3;
    model.scale = (KEK_FVec3){2e8f / 255.f, 2e8f / 255.f, 0.f};
    model.offset = (KEK_FVec3){-1e8f, -1e8f, 3.f};
    model.faces = &face; model.faces_count = 1;
    model.uvs = uvs; model.uvs_count = 3;
    model.face_uvs = &face_uv; model.face_uvs_count = 1;
    model.texture = handle;
    for (mode = 0; mode < 2; ++mode) {
        kek_texture_set_warp_mode(&e, (KEK_TextureWarpMode)mode);
        kek_flush_buffers(&e);
        kek_3d_begin_view(&e, &camera);
        kek_3d_draw_model(&e, &model, (KEK_Transform3D){(KEK_FVec3){0.f, 0.f, 0.f}, (KEK_FVec3){0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}});
        /* Interior samples, away from the diagonal and texel boundaries.
           On the z=3 plane, UV=(view.x, view.y)/8, independently of clipping. */
        for (y = e.h / 2 + 11; y < e.h - 10; y += 23) {
            for (x = 11; x < e.w / 2 - 10; x += 29) {
                float u = ((2.f * (float)x / e.w - 1.f) * 3.f * ((float)e.w / e.h) / focal) / 8.f;
                float v = ((1.f - 2.f * (float)y / e.h) * 3.f / focal) / 8.f;
                TEST_ASSERT_EQUAL_UINT8(kek_texture_sample(&e, texture, u, v), e.fb[(size_t)y * e.w + x]);
            }
        }
    }
    kek_texture_destroy(&e, handle);
}

void test_guard_band_boundary_and_degenerate_border(void) {
    KEK_3D_ProjectedVertex v[3] = {
        {{-1048576, -1048576}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f},
        {{1048576, -1048576}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f},
        {{0, 1048576}, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f}};
    kek_3d_triangle(&e, v, 15);
    TEST_ASSERT_EQUAL_INT(e.w * e.h, kek_test_frame_painted(&e));
    kek_flush_buffers(&e);
    v[0].screen.x = -1048577;
    kek_3d_triangle(&e, v, 15);
    TEST_ASSERT_EQUAL_INT(0, kek_test_frame_painted(&e));
    v[0].screen = v[1].screen = v[2].screen = (KEK_IVec2){10, 10};
    kek_3d_triangle_border(&e, v, 15, 15);
    TEST_ASSERT_EQUAL_UINT8(15, e.fb[10 * e.w + 10]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_side_clipping_interpolates_uvs_in_view_space);
    RUN_TEST(test_guard_band_boundary_and_degenerate_border);
    RUN_TEST(test_huge_triangle_preserves_visible_part_and_winding);
    RUN_TEST(test_extreme_finite_geometry_keeps_depth_and_arena_safe);
    RUN_TEST(test_huge_triangle_clips_each_side_and_near_plane);
    RUN_TEST(test_huge_triangles_outside_each_side_draw_nothing);
    RUN_TEST(test_clipped_texture_and_fog_keep_their_attributes);
    RUN_TEST(test_near_clipping_recomputes_fog_at_new_vertices);
    RUN_TEST(test_public_projection_rejects_invalid_input_without_writing);
    RUN_TEST(test_invalid_cameras_and_models_leave_frame_and_arena_unchanged);
    RUN_TEST(test_direct_rasterizers_reject_unbounded_screen_coordinates);
    RUN_TEST(test_composed_camera_projection_agrees_with_model_and_light);
    return UNITY_END();
}
