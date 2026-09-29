#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_2d.h"
#include "kek_3d.h"
#include "kek_model.h"
#include "kek_texture.h"
#include "test_support.h"

static KEK_engine e;

void setUp(void) {
    kek_test_init(&e);
}

void tearDown(void) {
}

static KEK_3D_ProjectedVertex vertex(int x, int y, float depth) {
    KEK_3D_ProjectedVertex v = {0};
    v.screen = (KEK_IVec2){ x, y };
    v.depth = depth;
    v.inv_z = 1.f / depth;
    return v;
}

static void triangle(uint8_t color, float depth) {
    KEK_3D_ProjectedVertex v[3] = {
        vertex(2, 2, depth), vertex(16, 2, depth), vertex(2, 16, depth)
    };
    kek_3d_triangle(&e, v, color);
}

void test_2d_and_3d_draw_into_the_texture_and_restore_the_main_frame(void) {
    KEK_TextureHandle handle = kek_texture_create(&e, 37, 19);
    KEK_texture* texture = kek_texture_get(&e, handle);
    uint8_t* main_fb = e.fb;
    uint16_t* main_db = e.db;
    size_t available = kek_arena_available(&e);
    KEK_IVec2 centre;

    TEST_ASSERT_NOT_NULL(texture);
    main_fb[3 * e.w + 3] = 11;
    main_db[3 * e.w + 3] = 1234;
    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, KEK_TARGET_CLEAR_COLOR));
    TEST_ASSERT_EQUAL_PTR(texture->data, e.fb);
    TEST_ASSERT_EQUAL_UINT16(37, e.w);
    TEST_ASSERT_EQUAL_UINT16(19, e.h);
    TEST_ASSERT_NOT_EQUAL(main_db, e.db);
    centre = kek_2d_to_screen(&e, (KEK_FVec2){0.f, 0.f});
    TEST_ASSERT_EQUAL_INT(18, centre.x);
    TEST_ASSERT_EQUAL_INT(9, centre.y);
    kek_2d_rect(&e, (KEK_IVec2){30, 10}, (KEK_IVec2){36, 15}, 6);
    triangle(9, 2.f);
    TEST_ASSERT_EQUAL_UINT8(6, texture->data[12 * 37 + 32]);
    TEST_ASSERT_EQUAL_UINT8(9, texture->data[3 * 37 + 3]);
    TEST_ASSERT_GREATER_THAN_UINT16(0, e.db[3 * 37 + 3]);
    kek_target_restore(&e);
    TEST_ASSERT_EQUAL_PTR(main_fb, e.fb);
    TEST_ASSERT_EQUAL_PTR(main_db, e.db);
    TEST_ASSERT_EQUAL_UINT16(KEK_TEST_WIDTH, e.w);
    TEST_ASSERT_EQUAL_UINT16(KEK_TEST_HEIGHT, e.h);
    TEST_ASSERT_EQUAL_UINT8(11, main_fb[3 * e.w + 3]);
    TEST_ASSERT_EQUAL_UINT16(1234, main_db[3 * e.w + 3]);
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
}

void test_colour_can_survive_a_bind_but_depth_starts_clear_each_time(void) {
    KEK_TextureHandle handle = kek_texture_create(&e, 20, 20);
    KEK_texture* texture = kek_texture_get(&e, handle);

    TEST_ASSERT_NOT_NULL(texture);
    memset(texture->data, 7, 400);
    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, 0));
    TEST_ASSERT_EQUAL_UINT8(7, e.fb[3 * 20 + 3]);
    triangle(9, 2.f);
    TEST_ASSERT_EQUAL_UINT8(9, texture->data[3 * 20 + 3]);
    kek_target_restore(&e);

    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, 0));
    TEST_ASSERT_EQUAL_UINT16(0, e.db[3 * 20 + 3]);
    triangle(5, 10.f);
    TEST_ASSERT_EQUAL_UINT8(5, texture->data[3 * 20 + 3]);
    kek_target_restore(&e);

    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, KEK_TARGET_CLEAR_COLOR));
    TEST_ASSERT_EQUAL_UINT8(0, texture->data[3 * 20 + 3]);
    kek_target_restore(&e);
}

void test_invalid_and_second_bind_leave_the_active_view_untouched(void) {
    KEK_TextureHandle handle = kek_texture_create(&e, 20, 20);
    KEK_TextureHandle empty = kek_texture_create(&e, 0, 4);
    uint8_t* original_fb = e.fb;
    size_t available = kek_arena_available(&e);

    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, handle);
    TEST_ASSERT_FALSE(kek_target_bind(&e, KEK_TEXTURE_HANDLE_INVALID, 0));
    TEST_ASSERT_FALSE(kek_target_bind(&e, empty, 0));
    TEST_ASSERT_FALSE(kek_target_bind(&e, handle, 2u));
    TEST_ASSERT_EQUAL_PTR(original_fb, e.fb);
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));

    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, 0));
    available = kek_arena_available(&e);
    TEST_ASSERT_FALSE(kek_target_bind(&e, handle, KEK_TARGET_CLEAR_COLOR));
    TEST_ASSERT_EQUAL_PTR(kek_texture_get(&e, handle)->data, e.fb);
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
    kek_target_restore(&e);
    kek_target_restore(&e);
    TEST_ASSERT_EQUAL_PTR(original_fb, e.fb);
}

void test_active_texture_cannot_be_destroyed_or_released(void) {
    KEK_ArenaMark mark = kek_arena_mark(&e);
    KEK_TextureHandle handle = kek_texture_create(&e, 16, 16);
    size_t available;

    TEST_ASSERT_TRUE(kek_target_bind(&e, handle, 0));
    available = kek_arena_available(&e);
    kek_texture_destroy(&e, handle);
    kek_arena_release(&e, mark);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, handle));
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
    kek_target_restore(&e);
    kek_arena_release(&e, mark);
    TEST_ASSERT_NULL(kek_texture_get(&e, handle));
}

void test_owning_model_cannot_destroy_an_active_texture(void) {
    KEK_TextureHandle texture = kek_texture_create(&e, 8, 8);
    KEK_ModelHandle model = kek_model_create(&e, 0, 0, 0, 0);
    KEK_model* owner = kek_model_get(&e, model);

    TEST_ASSERT_NOT_NULL(owner);
    owner->texture = texture;
    owner->owns_texture = 1;
    TEST_ASSERT_TRUE(kek_target_bind(&e, texture, 0));
    kek_model_destroy(&e, model);
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, model));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, texture));
    kek_target_restore(&e);
    kek_model_destroy(&e, model);
    TEST_ASSERT_NULL(kek_model_get(&e, model));
    TEST_ASSERT_NULL(kek_texture_get(&e, texture));
}

void test_depth_allocation_failure_leaves_everything_untouched(void) {
    KEK_TextureHandle handle;
    uint8_t* fb;
    uint16_t* db;
    size_t available;

    kek_test_init_arena(&e, 2048);
    handle = kek_texture_create(&e, 32, 32);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, handle);
    fb = e.fb;
    db = e.db;
    available = kek_arena_available(&e);
    TEST_ASSERT_FALSE(kek_target_bind(&e, handle, KEK_TARGET_CLEAR_COLOR));
    TEST_ASSERT_EQUAL_PTR(fb, e.fb);
    TEST_ASSERT_EQUAL_PTR(db, e.db);
    TEST_ASSERT_EQUAL_UINT16(KEK_TEST_WIDTH, e.w);
    TEST_ASSERT_EQUAL_UINT16(KEK_TEST_HEIGHT, e.h);
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
}

static KEK_TextureHandle forgotten_target;

static void render_without_restore(KEK_scene* scene, KEK_engine* engine) {
    (void)scene;
    TEST_ASSERT_TRUE(kek_target_bind(engine, forgotten_target, KEK_TARGET_CLEAR_COLOR));
    kek_2d_rect(engine, (KEK_IVec2){0, 0}, (KEK_IVec2){7, 8}, 12);
}

void test_render_recovers_the_main_frame_if_a_scene_forgets_restore(void) {
    KEK_scene scene = {0};
    uint8_t* main_fb = e.fb;
    uint16_t* main_db = e.db;
    size_t available;

    forgotten_target = kek_texture_create(&e, 8, 8);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, forgotten_target);
    available = kek_arena_available(&e);
    scene.render = render_without_restore;
    kek_set_scene(&e, &scene);
    kek_render(&e);
    TEST_ASSERT_EQUAL_PTR(main_fb, e.fb);
    TEST_ASSERT_EQUAL_PTR(main_db, e.db);
    TEST_ASSERT_EQUAL_UINT16(KEK_TEST_WIDTH, e.w);
    TEST_ASSERT_EQUAL_size_t(available, kek_arena_available(&e));
    TEST_ASSERT_EQUAL_UINT8(12, kek_texture_get(&e, forgotten_target)->data[0]);
    kek_set_scene(&e, 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_2d_and_3d_draw_into_the_texture_and_restore_the_main_frame);
    RUN_TEST(test_colour_can_survive_a_bind_but_depth_starts_clear_each_time);
    RUN_TEST(test_invalid_and_second_bind_leave_the_active_view_untouched);
    RUN_TEST(test_active_texture_cannot_be_destroyed_or_released);
    RUN_TEST(test_owning_model_cannot_destroy_an_active_texture);
    RUN_TEST(test_depth_allocation_failure_leaves_everything_untouched);
    RUN_TEST(test_render_recovers_the_main_frame_if_a_scene_forgets_restore);
    return UNITY_END();
}
