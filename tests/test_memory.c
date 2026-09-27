/* The block kek_init lays an engine out in. KEK_MEMORY_SIZE promises a size;
   these check that it is enough at every alignment the block can arrive with,
   that nothing is written outside it, and that kek_init turns down what it
   cannot use without writing anything at all.

   61x37 rather than 320x200: neither the frame nor the depth buffer comes out
   a multiple of KEK_MEMORY_ALIGN, so the rounding between pieces is exercised
   rather than lined up by luck. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_2d.h"

#define W 61
#define H 37
#define GUARD 64
#define GUARD_BYTE 0xA5

static const KEK_desc DESC = { W, H };

/* Room for the largest misalignment on either side of an exact-size block. */
static unsigned char arena[GUARD + KEK_MEMORY_ALIGN + KEK_MEMORY_SIZE(W, H) + GUARD];

void setUp(void) {
    memset(arena, GUARD_BYTE, sizeof(arena));
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

void test_the_macro_and_the_function_agree(void) {
    static const KEK_desc descs[] = { { 1, 1 }, { W, H }, { 320, 200 }, { 65535, 1 }, { 1, 65535 } };
    size_t i;

    TEST_ASSERT_EQUAL_size_t(KEK_MEMORY_SIZE(W, H), kek_memory_size(&DESC));
    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        TEST_ASSERT_EQUAL_size_t(KEK_MEMORY_SIZE(descs[i].width, descs[i].height), kek_memory_size(&descs[i]));
    }
}

/* Everything written goes through the four pieces in full: the flush covers
   frame and depth, a palette change rewrites the palette and every row of the
   shading table, and a rect over the whole screen covers the frame again. */
void test_the_engine_stays_inside_its_block_at_any_alignment(void) {
    KEK_palette_item white[256];
    size_t size = KEK_MEMORY_SIZE(W, H);
    size_t pixels = (size_t)W * H;
    size_t offset;
    KEK_engine e;

    memset(white, 63, sizeof(white));

    for (offset = 0; offset < KEK_MEMORY_ALIGN; ++offset) {
        unsigned char* block = arena + GUARD + offset;

        memset(arena, GUARD_BYTE, sizeof(arena));
        TEST_ASSERT_TRUE(kek_init(&e, &DESC, block, size));

        TEST_ASSERT_EQUAL_UINT16(W, e.w);
        TEST_ASSERT_EQUAL_UINT16(H, e.h);
        TEST_ASSERT_TRUE(inside(e.fb, pixels, block, size));
        TEST_ASSERT_TRUE(inside(e.db, pixels * sizeof(float), block, size));
        TEST_ASSERT_TRUE(inside(e.palette, 256 * sizeof(KEK_palette_item), block, size));
        TEST_ASSERT_TRUE(inside(e.shading_palette, (size_t)256 * KEK_PALETTE_SHADING_LEVELS, block, size));
        TEST_ASSERT_TRUE(disjoint(e.fb, pixels, e.db, pixels * sizeof(float)));
        TEST_ASSERT_TRUE(disjoint(e.db, pixels * sizeof(float), e.palette, 256 * sizeof(KEK_palette_item)));
        TEST_ASSERT_TRUE(disjoint(e.palette, 256 * sizeof(KEK_palette_item),
                                  e.shading_palette, (size_t)256 * KEK_PALETTE_SHADING_LEVELS));
        TEST_ASSERT_EQUAL_UINT(0, (unsigned)((uintptr_t)e.db % KEK_MEMORY_ALIGN));

        kek_flush_buffers(&e);
        kek_set_palette(&e, white);
        kek_2d_rect(&e, (KEK_IVec2){ 0, 0 }, (KEK_IVec2){ W - 1, H }, 7);

        assert_untouched(arena, GUARD + offset, "written before the block");
        assert_untouched(block + size, sizeof(arena) - (GUARD + offset + size), "written past the block");
    }
}

void test_init_turns_down_what_it_cannot_use_and_writes_nothing(void) {
    static const KEK_desc no_width = { 0, H };
    static const KEK_desc no_height = { W, 0 };
    size_t size = KEK_MEMORY_SIZE(W, H);
    size_t offset;
    KEK_engine e;
    KEK_engine untouched;

    memset(&untouched, 0x5A, sizeof(untouched));

    for (offset = 0; offset < KEK_MEMORY_ALIGN; ++offset) {
        unsigned char* block = arena + GUARD + offset;

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
    assert_untouched(arena, sizeof(arena), "a rejected init wrote to the block");
}

/* The block is whatever was lying there; the engine must not show it. */
void test_the_frame_starts_cleared_and_the_palette_default(void) {
    KEK_engine e;
    size_t i;

    TEST_ASSERT_TRUE(kek_init(&e, &DESC, arena + GUARD, KEK_MEMORY_SIZE(W, H)));
    for (i = 0; i < (size_t)W * H; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, e.fb[i]);
        TEST_ASSERT_EQUAL_FLOAT(0.f, e.db[i]);
    }
    TEST_ASSERT_EQUAL_MEMORY(KEK_DEFAULT_PALETTE, e.palette, 256 * sizeof(KEK_palette_item));
}

/* Frames and palettes, that is. The pools are still the library's own until
   they move into the block too, so a second kek_init resets them for both. */
void test_two_engines_share_neither_frame_nor_palette(void) {
    static unsigned char other[KEK_MEMORY_SIZE(W, H)];
    KEK_palette_item white[256];
    KEK_engine a;
    KEK_engine b;
    size_t i;

    memset(white, 63, sizeof(white));
    TEST_ASSERT_TRUE(kek_init(&a, &DESC, arena + GUARD, KEK_MEMORY_SIZE(W, H)));
    TEST_ASSERT_TRUE(kek_init(&b, &DESC, other, sizeof(other)));

    kek_set_palette(&a, white);
    kek_2d_rect(&a, (KEK_IVec2){ 0, 0 }, (KEK_IVec2){ W - 1, H }, 7);

    TEST_ASSERT_EQUAL_MEMORY(KEK_DEFAULT_PALETTE, b.palette, 256 * sizeof(KEK_palette_item));
    for (i = 0; i < (size_t)W * H; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, b.fb[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_macro_and_the_function_agree);
    RUN_TEST(test_the_engine_stays_inside_its_block_at_any_alignment);
    RUN_TEST(test_init_turns_down_what_it_cannot_use_and_writes_nothing);
    RUN_TEST(test_the_frame_starts_cleared_and_the_palette_default);
    RUN_TEST(test_two_engines_share_neither_frame_nor_palette);
    return UNITY_END();
}
