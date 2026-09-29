/* The block kek_init lays an engine out in. kek_memory_size promises a size;
   these check that it is enough at every alignment the block can arrive with,
   that nothing is written outside it, and that kek_init turns down what it
   cannot use without writing anything at all. Then the arena in the rest of
   it: what it gives back, in what order, and that one engine's arena is not
   another's.

   61x37 rather than 320x200: neither the frame nor the depth buffer comes out
   a multiple of KEK_MEMORY_ALIGN, so the rounding between pieces is exercised
   rather than lined up by luck. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_2d.h"
#include "kek_3d.h"
#include "kek_model.h"
#include "kek_texture.h"

#define W 61
#define H 37
#define GUARD 64
#define GUARD_BYTE 0xA5
/* An arena of this much on top of the least block, for the tests that use it. */
#define ARENA 8192

static const KEK_desc DESC = { .width = W, .height = H };

/* Room for the largest misalignment on either side of a block with the arena. */
static unsigned char storage[GUARD + KEK_MEMORY_ALIGN + KEK_MEMORY_SIZE(W, H) + ARENA + GUARD];

void setUp(void) {
    memset(storage, GUARD_BYTE, sizeof(storage));
}

void tearDown(void) {
}

static void assert_untouched(const unsigned char* bytes, size_t count, const char* message) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (bytes[i] != GUARD_BYTE) {
            TEST_FAIL_MESSAGE(message);
        }
    }
}

static int inside(const void* p, size_t size, const unsigned char* block, size_t block_size) {
    const unsigned char* b = (const unsigned char*)p;
    return b >= block && b + size <= block + block_size;
}

static int disjoint(const void* a, size_t a_size, const void* b, size_t b_size) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    return x + a_size <= y || y + b_size <= x;
}

static void init(KEK_engine* e) {
    TEST_ASSERT_TRUE(kek_init(e, &DESC, storage + GUARD, KEK_MEMORY_SIZE(W, H) + ARENA));
}

/* ---- The block ---- */

void test_the_macro_and_the_function_agree(void) {
    static const KEK_desc descs[] = {
        { .width = 1, .height = 1 }, { .width = W, .height = H }, { .width = 320, .height = 200 },
        { .width = 65535, .height = 1 }, { .width = 1, .height = 65535 },
        /* The defaults spelt out are the defaults. */
        { .width = W, .height = H, .models = KEK_DEFAULT_MODELS, .textures = KEK_DEFAULT_TEXTURES }
    };
    size_t i;

    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        TEST_ASSERT_EQUAL_size_t(KEK_MEMORY_SIZE(descs[i].width, descs[i].height), kek_memory_size(&descs[i]));
    }
}

void test_bigger_handle_tables_take_more_block(void) {
    const KEK_desc more = { .width = W, .height = H, .models = 200, .textures = 300 };
    const KEK_desc fewer = { .width = W, .height = H, .models = 2, .textures = 2 };

    TEST_ASSERT_GREATER_THAN_size_t(kek_memory_size(&DESC), kek_memory_size(&more));
    TEST_ASSERT_LESS_THAN_size_t(kek_memory_size(&DESC), kek_memory_size(&fewer));
}

/* The least block is enough, which is to say KEK_MEMORY_BUILTIN_ covers the
   default cube and texture, at every alignment and whatever the tables. */
void test_init_takes_exactly_the_least_block_and_not_a_byte_less(void) {
    static const KEK_desc descs[] = {
        { .width = W, .height = H },
        { .width = 1, .height = 1, .models = 1, .textures = 1 },
        { .width = W, .height = H, .models = 300, .textures = 7 }
    };
    size_t d, offset;
    KEK_engine e;

    for (d = 0; d < sizeof(descs) / sizeof(descs[0]); ++d) {
        size_t size = kek_memory_size(&descs[d]);
        static unsigned char big[GUARD + KEK_MEMORY_ALIGN + 64 * 1024];

        TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(big) - GUARD - KEK_MEMORY_ALIGN, size);
        for (offset = 0; offset < KEK_MEMORY_ALIGN; ++offset) {
            TEST_ASSERT_TRUE(kek_init(&e, &descs[d], big + GUARD + offset, size));
            TEST_ASSERT_NOT_NULL(kek_model_get(&e, kek_default_cube_model_handle(&e)));
            TEST_ASSERT_NOT_NULL(kek_texture_get(&e, kek_default_texture_handle(&e)));
            TEST_ASSERT_FALSE(kek_init(&e, &descs[d], big + GUARD + offset, size - 1));
        }
    }
}

/* Everything written goes through every piece in full: the flush covers frame
   and depth, a palette change rewrites the palette and every row of the
   shading table, a rect over the whole screen covers the frame again, a
   texture as big as the arena will hand out covers the arena to its top, and
   drawing the cube with it gone takes a temporary from the top. */
void test_the_engine_stays_inside_its_block_at_any_alignment(void) {
    KEK_palette_item white[256];
    size_t size = KEK_MEMORY_SIZE(W, H) + ARENA;
    size_t pixels = (size_t)W * H;
    size_t offset;
    KEK_engine e;

    memset(white, 63, sizeof(white));

    for (offset = 0; offset < KEK_MEMORY_ALIGN; ++offset) {
        unsigned char* block = storage + GUARD + offset;
        KEK_TextureHandle all;
        KEK_camera camera = KEK_DEFAULT_CAMERA;

        memset(storage, GUARD_BYTE, sizeof(storage));
        TEST_ASSERT_TRUE(kek_init(&e, &DESC, block, size));

        TEST_ASSERT_EQUAL_UINT16(W, e.w);
        TEST_ASSERT_EQUAL_UINT16(H, e.h);
        TEST_ASSERT_TRUE(inside(e.fb, pixels, block, size));
        TEST_ASSERT_TRUE(inside(e.db, pixels * sizeof(uint16_t), block, size));
        TEST_ASSERT_TRUE(inside(e.palette, 256 * sizeof(KEK_palette_item), block, size));
        TEST_ASSERT_TRUE(inside(e.shading_palette, (size_t)256 * KEK_PALETTE_SHADING_LEVELS, block, size));
        TEST_ASSERT_TRUE(disjoint(e.fb, pixels, e.db, pixels * sizeof(uint16_t)));
        TEST_ASSERT_TRUE(disjoint(e.db, pixels * sizeof(uint16_t), e.palette, 256 * sizeof(KEK_palette_item)));
        TEST_ASSERT_TRUE(disjoint(e.palette, 256 * sizeof(KEK_palette_item),
                                  e.shading_palette, (size_t)256 * KEK_PALETTE_SHADING_LEVELS));
        TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)e.db % KEK_MEMORY_ALIGN));

        kek_flush_buffers(&e);
        kek_set_palette(&e, white);
        kek_2d_rect(&e, (KEK_IVec2){ 0, 0 }, (KEK_IVec2){ W - 1, H }, 7);

        /* One allocation costs its size plus KEK_MEMORY_ALIGN. */
        all = kek_texture_create(&e, 1, (uint16_t)(kek_arena_available(&e) - KEK_MEMORY_ALIGN));
        TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, all);
        TEST_ASSERT_EQUAL_size_t(0, kek_arena_available(&e));
        memset(kek_texture_get(&e, all)->data, 3, (size_t)kek_texture_get(&e, all)->height);
        kek_texture_destroy(&e, all);

        camera.position = (KEK_FVec3){ 0.f, 0.f, -4.f };
        kek_3d_begin_view(&e, &camera);
        kek_3d_draw_model(&e, kek_model_get(&e, kek_default_cube_model_handle(&e)), (KEK_Transform3D){(KEK_FVec3){ 0.f, 0.f, 0.f }, (KEK_FVec3){ 0.f, 0.f, 0.f }, {1.f, 1.f, 1.f}});

        assert_untouched(storage, GUARD + offset, "written before the block");
        assert_untouched(block + size, sizeof(storage) - (GUARD + offset + size), "written past the block");
    }
}

void test_init_turns_down_what_it_cannot_use_and_writes_nothing(void) {
    static const KEK_desc no_width = { .width = 0, .height = H };
    static const KEK_desc no_height = { .width = W, .height = 0 };
    size_t size = KEK_MEMORY_SIZE(W, H);
    size_t offset;
    KEK_engine e;
    KEK_engine untouched;

    memset(&untouched, 0x5A, sizeof(untouched));

    for (offset = 0; offset < KEK_MEMORY_ALIGN; ++offset) {
        unsigned char* block = storage + GUARD + offset;

        memcpy(&e, &untouched, sizeof(e));
        TEST_ASSERT_FALSE(kek_init(&e, &DESC, block, size - 1));
        TEST_ASSERT_FALSE(kek_init(&e, &DESC, block, 0));
        TEST_ASSERT_FALSE(kek_init(&e, &no_width, block, size));
        TEST_ASSERT_FALSE(kek_init(&e, &no_height, block, size));
        TEST_ASSERT_FALSE(kek_init(&e, &DESC, NULL, size));
        TEST_ASSERT_FALSE(kek_init(&e, NULL, block, size));
        TEST_ASSERT_FALSE(kek_init(NULL, &DESC, block, size));
        TEST_ASSERT_EQUAL_MEMORY(&untouched, &e, sizeof(e));
    }
    assert_untouched(storage, sizeof(storage), "a rejected init wrote to the block");
}

/* The block is whatever was lying there; the engine must not show it. */
void test_the_frame_starts_cleared_and_the_palette_default(void) {
    KEK_engine e;
    size_t i;

    init(&e);
    for (i = 0; i < (size_t)W * H; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, e.fb[i]);
        TEST_ASSERT_EQUAL_UINT16(0, e.db[i]);
    }
    TEST_ASSERT_EQUAL_MEMORY(KEK_DEFAULT_PALETTE, e.palette, 256 * sizeof(KEK_palette_item));
}

/* ---- The arena ---- */

void test_what_is_added_to_the_least_block_is_the_arena(void) {
    KEK_engine e;

    init(&e);
    /* At least ARENA: the defaults take less than KEK_MEMORY_BUILTIN_ leaves. */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(ARENA, kek_arena_available(&e));
    TEST_ASSERT_LESS_THAN_size_t(ARENA + KEK_MEMORY_BUILTIN_, kek_arena_available(&e));
}

void test_destroying_in_reverse_gives_everything_back(void) {
    KEK_TextureHandle t[3];
    KEK_engine e;
    size_t before;
    int i;

    init(&e);
    before = kek_arena_available(&e);
    for (i = 0; i < 3; ++i) {
        t[i] = kek_texture_create(&e, 10, (uint16_t)(10 + i));
        TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, t[i]);
    }
    for (i = 2; i >= 0; --i) {
        kek_texture_destroy(&e, t[i]);
    }
    TEST_ASSERT_EQUAL_size_t(before, kek_arena_available(&e));
}

/* Older ones first: nothing comes back until the newest goes, and then all of
   it does. */
void test_destroying_in_order_gives_everything_back_with_the_last(void) {
    KEK_TextureHandle t[3];
    KEK_engine e;
    size_t before;
    size_t full;
    int i;

    init(&e);
    before = kek_arena_available(&e);
    for (i = 0; i < 3; ++i) {
        t[i] = kek_texture_create(&e, 10, 10);
    }
    full = kek_arena_available(&e);

    kek_texture_destroy(&e, t[0]);
    kek_texture_destroy(&e, t[1]);
    TEST_ASSERT_EQUAL_size_t(full, kek_arena_available(&e));
    TEST_ASSERT_NULL(kek_texture_get(&e, t[0]));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, t[2]));

    kek_texture_destroy(&e, t[2]);
    TEST_ASSERT_EQUAL_size_t(before, kek_arena_available(&e));
}

void test_a_full_arena_refuses_cleanly_and_recovers(void) {
    KEK_TextureHandle last = KEK_TEXTURE_HANDLE_INVALID;
    KEK_TextureHandle rest;
    KEK_TextureHandle handle;
    KEK_engine e;
    size_t left;

    init(&e);
    while ((handle = kek_texture_create(&e, 32, 32)) != KEK_TEXTURE_HANDLE_INVALID) {
        last = handle;
    }
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, last);
    left = kek_arena_available(&e);
    TEST_ASSERT_LESS_THAN_size_t(32 * 32 + KEK_MEMORY_ALIGN, left);
    /* Less than another of those, but room for a small model: one texture
       of just that size takes the rest. */
    TEST_ASSERT_GREATER_THAN_size_t(KEK_MEMORY_ALIGN, left);
    rest = kek_texture_create(&e, 1, (uint16_t)(left - KEK_MEMORY_ALIGN));
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, rest);
    TEST_ASSERT_EQUAL_size_t(0, kek_arena_available(&e));

    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_create(&e, 100, 100, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_model_clone(&e, &KEK_CUBE_MODEL));
    TEST_ASSERT_EQUAL_size_t(0, kek_arena_available(&e));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, last));

    kek_texture_destroy(&e, rest);
    kek_texture_destroy(&e, last);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, kek_texture_create(&e, 32, 32));
}

/* A level's worth, loaded after a mark and released to it. */
void test_a_release_takes_what_came_after_the_mark_and_nothing_before(void) {
    KEK_TextureHandle before_mark;
    KEK_TextureHandle after_mark;
    KEK_ModelHandle model_after;
    KEK_ArenaMark mark;
    KEK_engine e;
    size_t at_mark;

    init(&e);
    before_mark = kek_texture_create(&e, 8, 8);
    mark = kek_arena_mark(&e);
    at_mark = kek_arena_available(&e);
    after_mark = kek_texture_create(&e, 8, 8);
    model_after = kek_model_clone(&e, &KEK_CUBE_MODEL);
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, model_after));

    kek_arena_release(&e, mark);
    TEST_ASSERT_EQUAL_size_t(at_mark, kek_arena_available(&e));
    TEST_ASSERT_NULL(kek_texture_get(&e, after_mark));
    TEST_ASSERT_NULL(kek_model_get(&e, model_after));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, before_mark));
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, kek_default_cube_model_handle(&e)));

    /* A stale handle destroys nothing, and a new texture does not answer to it. */
    kek_texture_destroy(&e, after_mark);
    TEST_ASSERT_NOT_EQUAL(after_mark, kek_texture_create(&e, 8, 8));
    TEST_ASSERT_NULL(kek_texture_get(&e, after_mark));
}

/* The defaults are below every mark the application can take. */
void test_a_release_never_takes_the_defaults(void) {
    KEK_engine e;
    size_t fresh;

    init(&e);
    fresh = kek_arena_available(&e);
    kek_texture_create(&e, 8, 8);
    kek_arena_release(&e, 0);

    TEST_ASSERT_EQUAL_size_t(fresh, kek_arena_available(&e));
    TEST_ASSERT_NOT_NULL(kek_model_get(&e, kek_default_cube_model_handle(&e)));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, kek_default_texture_handle(&e)));
}

/* A model after the mark that owns a texture from before it takes the texture
   with it, rather than leaving it behind with nobody to free it. */
void test_a_released_model_takes_the_texture_it_owns(void) {
    KEK_TextureHandle texture;
    KEK_ModelHandle model;
    KEK_ArenaMark mark;
    KEK_engine e;

    init(&e);
    texture = kek_texture_create(&e, 8, 8);
    mark = kek_arena_mark(&e);
    model = kek_model_clone(&e, &KEK_CUBE_MODEL);
    kek_model_get(&e, model)->texture = texture;
    kek_model_get(&e, model)->owns_texture = 1;

    kek_arena_release(&e, mark);
    TEST_ASSERT_NULL(kek_texture_get(&e, texture));
}

/* A mark from before an earlier, deeper release no longer means anything. */
void test_a_mark_above_the_top_is_ignored(void) {
    KEK_ArenaMark low_mark;
    KEK_ArenaMark high_mark;
    KEK_TextureHandle survivor;
    KEK_engine e;

    init(&e);
    low_mark = kek_arena_mark(&e);
    kek_texture_create(&e, 8, 8);
    high_mark = kek_arena_mark(&e);
    kek_arena_release(&e, low_mark);
    survivor = kek_texture_create(&e, 4, 4);

    kek_arena_release(&e, high_mark);
    TEST_ASSERT_NOT_NULL(kek_texture_get(&e, survivor));
}

/* Frames, palettes and pools: nothing of one engine is another's. */
void test_two_engines_share_nothing(void) {
    static unsigned char other[KEK_MEMORY_SIZE(W, H) + ARENA];
    KEK_palette_item white[256];
    KEK_TextureHandle a_texture;
    KEK_engine a;
    KEK_engine b;
    size_t b_free;
    size_t i;

    memset(white, 63, sizeof(white));
    init(&a);
    a_texture = kek_texture_create(&a, 16, 16);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, a_texture);

    /* b's init leaves a's pool alone, which it did not while pools were
       static: a second kek_init reset them for both. */
    TEST_ASSERT_TRUE(kek_init(&b, &DESC, other, sizeof(other)));
    TEST_ASSERT_NOT_NULL(kek_texture_get(&a, a_texture));
    b_free = kek_arena_available(&b);

    kek_set_palette(&a, white);
    kek_2d_rect(&a, (KEK_IVec2){ 0, 0 }, (KEK_IVec2){ W - 1, H }, 7);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, kek_texture_create(&a, 16, 16));

    TEST_ASSERT_EQUAL_MEMORY(KEK_DEFAULT_PALETTE, b.palette, 256 * sizeof(KEK_palette_item));
    for (i = 0; i < (size_t)W * H; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, b.fb[i]);
    }
    TEST_ASSERT_EQUAL_size_t(b_free, kek_arena_available(&b));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_macro_and_the_function_agree);
    RUN_TEST(test_bigger_handle_tables_take_more_block);
    RUN_TEST(test_init_takes_exactly_the_least_block_and_not_a_byte_less);
    RUN_TEST(test_the_engine_stays_inside_its_block_at_any_alignment);
    RUN_TEST(test_init_turns_down_what_it_cannot_use_and_writes_nothing);
    RUN_TEST(test_the_frame_starts_cleared_and_the_palette_default);
    RUN_TEST(test_what_is_added_to_the_least_block_is_the_arena);
    RUN_TEST(test_destroying_in_reverse_gives_everything_back);
    RUN_TEST(test_destroying_in_order_gives_everything_back_with_the_last);
    RUN_TEST(test_a_full_arena_refuses_cleanly_and_recovers);
    RUN_TEST(test_a_release_takes_what_came_after_the_mark_and_nothing_before);
    RUN_TEST(test_a_release_never_takes_the_defaults);
    RUN_TEST(test_a_released_model_takes_the_texture_it_owns);
    RUN_TEST(test_a_mark_above_the_top_is_ignored);
    RUN_TEST(test_two_engines_share_nothing);
    return UNITY_END();
}
