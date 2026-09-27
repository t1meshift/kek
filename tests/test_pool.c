/* The model and texture pools, through the public API only.

   Tier 2 replaces the fixed slots with an arena, and these tests are what that
   swap has to pass. So nothing here reads a slot, a capacity or the bits of a
   handle: a handle is an opaque value that is valid until it is destroyed and
   never valid again after, and the pool is measured by what it will still hand
   out. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_model.h"
#include "kek_texture.h"
#include "test_support.h"

static KEK_engine e;

static uint8_t TEXTURE_PIXELS[4 * 2] = { 1, 2, 3, 4, 5, 6, 7, 8 };
static KEK_texture SMALL_TEXTURE = { TEXTURE_PIXELS, 4, 2 };

void setUp(void) {
    kek_test_init(&e);
}

void tearDown(void) {
}

/* A texture in the pool with known pixels, and a model that owns it — the
   shape kek_file_model_load leaves behind for a textured model. */
static KEK_ModelHandle make_owning_model(KEK_TextureHandle* out_texture) {
    KEK_TextureHandle texture = kek_texture_clone(&e, &SMALL_TEXTURE);
    KEK_ModelHandle model = kek_model_clone(&e, &KEK_CUBE_MODEL);
    KEK_model* mdl = kek_model_get(&e, model);

    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, texture);
    TEST_ASSERT_NOT_NULL(mdl);
    mdl->texture = texture;
    mdl->owns_texture = 1;
    *out_texture = texture;
    return model;
}

void test_init_provides_the_default_cube_and_texture(void) {
    KEK_ModelHandle cube = kek_default_cube_model_handle(&e);
    KEK_TextureHandle texture = kek_default_texture_handle(&e);
    KEK_model* mdl = kek_model_get(&e, cube);
    KEK_texture* tex = kek_texture_get(&e, texture);

    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_NOT_NULL(tex);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.verts_count, mdl->verts_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.faces_count, mdl->faces_count);
    TEST_ASSERT_EQUAL_UINT16(KEK_DEFAULT_TEXTURE.width, tex->width);
    TEST_ASSERT_EQUAL_UINT16(KEK_DEFAULT_TEXTURE.height, tex->height);
    TEST_ASSERT_EQUAL_UINT32(texture, mdl->texture);
    TEST_ASSERT_EQUAL_UINT8(0, mdl->owns_texture);
}

void test_the_defaults_cannot_be_destroyed(void) {
    KEK_ModelHandle cube = kek_default_cube_model_handle(&e);
    KEK_TextureHandle texture = kek_default_texture_handle(&e);

    kek_model_destroy(&e, cube);
    kek_texture_destroy(&e, texture);

    TEST_ASSERT_NOT_NULL(kek_model_get(&e, cube));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, texture));
}

void test_live_handles_are_valid_and_distinct(void) {
    KEK_ModelHandle a = kek_model_create(&e, 0, 0, 0);
    KEK_ModelHandle b = kek_model_create(&e, 0, 0, 0);
    KEK_TextureHandle ta = kek_texture_create(&e, 1, 1);
    KEK_TextureHandle tb = kek_texture_create(&e, 1, 1);

    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, a);
    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, b);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NOT_EQUAL(kek_default_cube_model_handle(&e), a);
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, a));
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, b));
    TEST_ASSERT_TRUE(kek_model_get(&e, a) != kek_model_get(&e, b));

    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, ta);
    TEST_ASSERT_NOT_EQUAL(ta, tb);
    TEST_ASSERT_NOT_EQUAL(kek_default_texture_handle(&e), ta);
    TEST_ASSERT_TRUE(kek_texture_get(&e, ta) != kek_texture_get(&e, tb));
}

void test_a_new_model_is_empty(void) {
    KEK_model* mdl = kek_model_get(&e, kek_model_create(&e, 0, 0, 0));

    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->verts_count);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->faces_count);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->colors_count);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->textures_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, mdl->texture);
    TEST_ASSERT_EQUAL_UINT8(0, mdl->owns_texture);
}

void test_handles_that_were_never_issued_are_rejected(void) {
    TEST_ASSERT_NULL(kek_model_get(&e, KEK_MODEL_HANDLE_INVALID));
    TEST_ASSERT_NULL(kek_texture_get(&e, KEK_TEXTURE_HANDLE_INVALID));
    TEST_ASSERT_NULL(kek_model_get(&e, 0xFFFFFFFFu));
    TEST_ASSERT_NULL(kek_texture_get(&e, 0xFFFFFFFFu));

    /* Destroying one is a no-op, not a crash, and releases nothing. */
    kek_model_destroy(&e, 0xFFFFFFFFu);
    kek_texture_destroy(&e, 0xFFFFFFFFu);
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, kek_default_cube_model_handle(&e)));
}

void test_a_null_engine_is_refused(void) {
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_create(0, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_clone(0, &KEK_CUBE_MODEL));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_create(0, 1, 1));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_clone(0, &SMALL_TEXTURE));
    TEST_ASSERT_NULL(kek_model_get(0, kek_default_cube_model_handle(&e)));
    TEST_ASSERT_NULL(kek_texture_get(0, kek_default_texture_handle(&e)));
    kek_model_destroy(0, kek_default_cube_model_handle(&e));
    kek_texture_destroy(0, kek_default_texture_handle(&e));
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_default_cube_model_handle(0));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_default_texture_handle(0));
}

void test_a_destroyed_handle_is_stale_even_after_its_storage_is_reused(void) {
    int free_before = kek_test_free_models(&e);
    KEK_ModelHandle first = kek_model_create(&e, 0, 0, 0);
    KEK_ModelHandle second;

    kek_model_destroy(&e, first);
    TEST_ASSERT_NULL(kek_model_get(&e, first));
    TEST_ASSERT_EQUAL_INT(free_before, kek_test_free_models(&e));

    /* Whatever the allocator hands out next — very likely the same storage —
       must not answer to the old handle. */
    second = kek_model_create(&e, 0, 0, 0);
    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, second);
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_NULL(kek_model_get(&e, first));
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, second));

    /* And destroying through the stale handle must not release the new one. */
    kek_model_destroy(&e, first);
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, second));
}

void test_a_destroyed_texture_handle_is_stale_even_after_its_storage_is_reused(void) {
    KEK_TextureHandle first = kek_texture_create(&e, 1, 1);
    KEK_TextureHandle second;

    kek_texture_destroy(&e, first);
    TEST_ASSERT_NULL(kek_texture_get(&e, first));

    second = kek_texture_create(&e, 1, 1);
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_NULL(kek_texture_get(&e, first));

    kek_texture_destroy(&e, first);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, second));
}

void test_no_stale_handle_comes_back_over_many_reuses(void) {
    enum { CYCLES = 1000 };
    static KEK_ModelHandle seen[CYCLES];
    int i, j;

    for (i = 0; i < CYCLES; ++i) {
        seen[i] = kek_model_create(&e, 0, 0, 0);
        TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, seen[i]);
        kek_model_destroy(&e, seen[i]);
    }

    for (i = 0; i < CYCLES; ++i) {
        TEST_ASSERT_NULL(kek_model_get(&e, seen[i]));
        for (j = i + 1; j < CYCLES; ++j) {
            if (seen[i] == seen[j]) {
                TEST_FAIL_MESSAGE("a handle was issued twice");
            }
        }
    }
}

void test_the_pool_refuses_past_capacity_and_recovers(void) {
    int free_models = kek_test_free_models(&e);
    int free_textures = kek_test_free_textures(&e);
    KEK_ModelHandle last = KEK_MODEL_HANDLE_INVALID;
    KEK_TextureHandle last_texture = KEK_TEXTURE_HANDLE_INVALID;
    int i;

    TEST_ASSERT_GREATER_THAN_INT(0, free_models);
    TEST_ASSERT_GREATER_THAN_INT(0, free_textures);

    for (i = 0; i < free_models; ++i) {
        last = kek_model_create(&e, 0, 0, 0);
        TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, last);
    }
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_create(&e, 0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_clone(&e, &KEK_CUBE_MODEL));

    for (i = 0; i < free_textures; ++i) {
        last_texture = kek_texture_create(&e, 1, 1);
        TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, last_texture);
    }
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_create(&e, 1, 1));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_clone(&e, &SMALL_TEXTURE));

    /* A refusal must not have disturbed anything that was live. */
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, last));
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, kek_default_cube_model_handle(&e)));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, last_texture));

    kek_model_destroy(&e, last);
    kek_texture_destroy(&e, last_texture);
    TEST_ASSERT_EQUAL_INT(1, kek_test_free_models(&e));
    TEST_ASSERT_EQUAL_INT(1, kek_test_free_textures(&e));
}

void test_init_empties_a_full_pool(void) {
    int free_models = kek_test_free_models(&e);
    int free_textures = kek_test_free_textures(&e);

    while (kek_model_create(&e, 0, 0, 0) != KEK_MODEL_HANDLE_INVALID) {
    }
    while (kek_texture_create(&e, 1, 1) != KEK_TEXTURE_HANDLE_INVALID) {
    }

    kek_test_init(&e);
    TEST_ASSERT_EQUAL_INT(free_models, kek_test_free_models(&e));
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
}

void test_clone_copies_a_model_into_storage_of_its_own(void) {
    KEK_ModelHandle handle = kek_model_clone(&e, &KEK_CUBE_MODEL);
    KEK_model* mdl = kek_model_get(&e, handle);
    uint32_t i;

    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.verts_count, mdl->verts_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.faces_count, mdl->faces_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.colors_count, mdl->colors_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.textures_count, mdl->textures_count);
    for (i = 0; i < mdl->verts_count; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(KEK_CUBE_MODEL.verts[i].x, mdl->verts[i].x);
        TEST_ASSERT_EQUAL_FLOAT(KEK_CUBE_MODEL.verts[i].y, mdl->verts[i].y);
        TEST_ASSERT_EQUAL_FLOAT(KEK_CUBE_MODEL.verts[i].z, mdl->verts[i].z);
    }
    for (i = 0; i < mdl->faces_count; ++i) {
        TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.faces[i].a, mdl->faces[i].a);
        TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.faces[i].b, mdl->faces[i].b);
        TEST_ASSERT_EQUAL_UINT32(KEK_CUBE_MODEL.faces[i].c, mdl->faces[i].c);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(KEK_CUBE_MODEL.face_colors, mdl->face_colors, mdl->colors_count);

    TEST_ASSERT_TRUE(mdl->verts != KEK_CUBE_MODEL.verts);
    mdl->verts[0].x = 1234.f;
    TEST_ASSERT_TRUE(KEK_CUBE_MODEL.verts[0].x != 1234.f);
}

void test_clone_copies_a_texture_into_storage_of_its_own(void) {
    KEK_texture* tex = kek_texture_get(&e, kek_texture_clone(&e, &SMALL_TEXTURE));

    TEST_ASSERT_NOT_NULL(tex);
    TEST_ASSERT_EQUAL_UINT16(4, tex->width);
    TEST_ASSERT_EQUAL_UINT16(2, tex->height);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEXTURE_PIXELS, tex->data, sizeof(TEXTURE_PIXELS));
    TEST_ASSERT_TRUE(tex->data != TEXTURE_PIXELS);
}

void test_a_clone_that_cannot_be_made_takes_nothing(void) {
    int free_models = kek_test_free_models(&e);
    int free_textures = kek_test_free_textures(&e);
    KEK_model too_many_verts = KEK_CUBE_MODEL;
    KEK_texture no_pixels = { 0, 4, 4 };
    KEK_texture too_large = { TEXTURE_PIXELS, 65535, 65535 };

    too_many_verts.verts_count = 0xFFFFFFFFu;

    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_clone(&e, 0));
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_clone(&e, &too_many_verts));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_clone(&e, 0));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_clone(&e, &no_pixels));
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_texture_clone(&e, &too_large));

    TEST_ASSERT_EQUAL_INT(free_models, kek_test_free_models(&e));
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
}

void test_destroying_a_model_releases_the_texture_it_owns(void) {
    KEK_TextureHandle texture;
    KEK_ModelHandle model;
    int free_textures = kek_test_free_textures(&e);

    model = make_owning_model(&texture);
    TEST_ASSERT_EQUAL_INT(free_textures - 1, kek_test_free_textures(&e));

    kek_model_destroy(&e, model);
    TEST_ASSERT_NULL(kek_texture_get(&e, texture));
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
}

void test_destroying_a_model_leaves_a_texture_it_does_not_own(void) {
    KEK_TextureHandle texture = kek_texture_clone(&e, &SMALL_TEXTURE);
    KEK_ModelHandle model = kek_model_clone(&e, &KEK_CUBE_MODEL);

    kek_model_get(&e, model)->texture = texture;
    kek_model_destroy(&e, model);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, texture));
}

void test_a_clone_shares_the_texture_but_not_its_ownership(void) {
    KEK_TextureHandle texture;
    KEK_ModelHandle original = make_owning_model(&texture);
    KEK_ModelHandle clone = kek_model_clone(&e, kek_model_get(&e, original));
    KEK_model* mdl = kek_model_get(&e, clone);

    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_EQUAL_UINT32(texture, mdl->texture);
    TEST_ASSERT_EQUAL_UINT8(0, mdl->owns_texture);
}

void test_destroying_a_clone_does_not_release_the_texture(void) {
    KEK_TextureHandle texture;
    KEK_ModelHandle original = make_owning_model(&texture);
    KEK_ModelHandle clone = kek_model_clone(&e, kek_model_get(&e, original));
    int free_textures = kek_test_free_textures(&e);

    kek_model_destroy(&e, clone);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, texture));
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));

    /* The original still owns it, and still releases it. */
    kek_model_destroy(&e, original);
    TEST_ASSERT_NULL(kek_texture_get(&e, texture));
    TEST_ASSERT_EQUAL_INT(free_textures + 1, kek_test_free_textures(&e));
}

void test_a_clone_outliving_the_original_holds_a_stale_texture_handle(void) {
    KEK_TextureHandle texture;
    KEK_TextureHandle replacement;
    KEK_ModelHandle original = make_owning_model(&texture);
    KEK_ModelHandle clone = kek_model_clone(&e, kek_model_get(&e, original));

    kek_model_destroy(&e, original);
    TEST_ASSERT_NULL(kek_texture_get(&e, kek_model_get(&e, clone)->texture));

    /* A new texture may take the released storage; the clone's handle must
       not resolve to it, and destroying the clone must not release it. */
    replacement = kek_texture_clone(&e, &SMALL_TEXTURE);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, replacement);
    TEST_ASSERT_NULL(kek_texture_get(&e, kek_model_get(&e, clone)->texture));
    kek_model_destroy(&e, clone);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, replacement));
}

void test_an_owner_whose_texture_was_released_elsewhere_does_not_release_its_successor(void) {
    KEK_TextureHandle texture;
    KEK_TextureHandle replacement;
    KEK_ModelHandle model = make_owning_model(&texture);

    kek_texture_destroy(&e, texture);
    replacement = kek_texture_clone(&e, &SMALL_TEXTURE);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, replacement);

    kek_model_destroy(&e, model);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, replacement));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_provides_the_default_cube_and_texture);
    RUN_TEST(test_the_defaults_cannot_be_destroyed);
    RUN_TEST(test_live_handles_are_valid_and_distinct);
    RUN_TEST(test_a_new_model_is_empty);
    RUN_TEST(test_handles_that_were_never_issued_are_rejected);
    RUN_TEST(test_a_null_engine_is_refused);
    RUN_TEST(test_a_destroyed_handle_is_stale_even_after_its_storage_is_reused);
    RUN_TEST(test_a_destroyed_texture_handle_is_stale_even_after_its_storage_is_reused);
    RUN_TEST(test_no_stale_handle_comes_back_over_many_reuses);
    RUN_TEST(test_the_pool_refuses_past_capacity_and_recovers);
    RUN_TEST(test_init_empties_a_full_pool);
    RUN_TEST(test_clone_copies_a_model_into_storage_of_its_own);
    RUN_TEST(test_clone_copies_a_texture_into_storage_of_its_own);
    RUN_TEST(test_a_clone_that_cannot_be_made_takes_nothing);
    RUN_TEST(test_destroying_a_model_releases_the_texture_it_owns);
    RUN_TEST(test_destroying_a_model_leaves_a_texture_it_does_not_own);
    RUN_TEST(test_a_clone_shares_the_texture_but_not_its_ownership);
    RUN_TEST(test_destroying_a_clone_does_not_release_the_texture);
    RUN_TEST(test_a_clone_outliving_the_original_holds_a_stale_texture_handle);
    RUN_TEST(test_an_owner_whose_texture_was_released_elsewhere_does_not_release_its_successor);
    return UNITY_END();
}
