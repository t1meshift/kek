/* The KIF loader, through the in-memory provider. Inputs are built here from
   the layout in docs/formats.md; nothing is read from disk.

   Every rejection is also a leak check: a failed load must leave the texture
   pool with exactly the free handles it had before, and the arena with
   exactly the free bytes. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_asset_memory.h"
#include "kek_file_image.h"
#include "kek_texture.h"
#include "test_support.h"

#define KIF_HEADER_SIZE 16
#define KIF_OFFSET_VERSION 4
#define KIF_OFFSET_WIDTH 6
#define KIF_OFFSET_HEIGHT 8
#define KIF_OFFSET_ENCODING 10

static KEK_engine e;
static KEK_TestFile file;
static KEK_MemoryAsset asset;
static KEK_MemoryAssetProvider provider;
static int free_textures;
static size_t free_bytes;

/* An engine with an arena of `arena` bytes, the asset provider attached, and
   the baseline every rejection is compared against. */
static void start(size_t arena) {
    kek_test_init_arena(&e, arena);
    e.assets = &provider.base;
    free_textures = kek_test_free_textures(&e);
    free_bytes = kek_arena_available(&e);
}

void setUp(void) {
    kek_test_file_clear(&file);
    asset.path = "image.kif";
    asset.bytes = file.bytes;
    asset.size = 0;
    kek_asset_memory_init(&provider, &asset, 1);
    start(KEK_TEST_ARENA);
}

void tearDown(void) {
}

static void header(uint16_t width, uint16_t height, uint8_t encoding) {
    kek_test_file_bytes(&file, "KIMG", 4);
    kek_test_file_u16(&file, 1);
    kek_test_file_u16(&file, width);
    kek_test_file_u16(&file, height);
    kek_test_file_u8(&file, encoding);
    kek_test_file_fill(&file, 0, 5);
    TEST_ASSERT_EQUAL_size_t(KIF_HEADER_SIZE, file.size);
}

static const uint8_t RAW_PIXELS[4 * 3] = {
    1, 2, 3, 4,
    5, 6, 7, 8,
    9, 10, 11, 12
};

static void raw_image(void) {
    header(4, 3, 0);
    kek_test_file_bytes(&file, RAW_PIXELS, sizeof(RAW_PIXELS));
}

/* 3x3 in four runs, two of which cross from one row into the next — the
   format allows that, and a decoder that stopped runs at row ends would
   produce a different picture. */
static const uint8_t RLE_PIXELS[3 * 3] = {
    7, 7, 9,
    9, 9, 9,
    9, 4, 5
};

static void rle_image(void) {
    header(3, 3, 1);
    kek_test_file_u8(&file, 2); kek_test_file_u8(&file, 7);
    kek_test_file_u8(&file, 5); kek_test_file_u8(&file, 9);
    kek_test_file_u8(&file, 1); kek_test_file_u8(&file, 4);
    kek_test_file_u8(&file, 1); kek_test_file_u8(&file, 5);
}

/* Serves the first `size` bytes of the file. */
static KEK_TextureHandle load_prefix(size_t size) {
    asset.size = size;
    return kek_file_image_load(&e, "image.kif");
}

static KEK_TextureHandle load(void) {
    return load_prefix(file.size);
}

static void assert_rejected_cleanly(KEK_TextureHandle handle) {
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(free_textures, kek_test_free_textures(&e), "a failed load kept a texture slot");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(free_bytes, kek_arena_available(&e), "a failed load kept arena memory");
}

static void assert_texture(KEK_TextureHandle handle, uint16_t width, uint16_t height, const uint8_t* pixels) {
    KEK_texture* texture = kek_texture_get(&e, handle);

    TEST_ASSERT_NOT_NULL(texture);
    TEST_ASSERT_EQUAL_UINT16(width, texture->width);
    TEST_ASSERT_EQUAL_UINT16(height, texture->height);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pixels, texture->data, (size_t)width * height);
}

void test_a_raw_image_round_trips(void) {
    KEK_TextureHandle handle;

    raw_image();
    handle = load();
    assert_texture(handle, 4, 3, RAW_PIXELS);
    TEST_ASSERT_EQUAL_INT(free_textures - 1, kek_test_free_textures(&e));

    kek_texture_destroy(&e, handle);
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
    TEST_ASSERT_EQUAL_size_t(free_bytes, kek_arena_available(&e));
}

void test_an_rle_image_round_trips_with_runs_across_rows(void) {
    rle_image();
    assert_texture(load(), 3, 3, RLE_PIXELS);
}

/* The largest a texture used to be allowed, before the arena. */
void test_a_256_by_256_image_loads(void) {
    header(256, 256, 0);
    kek_test_file_fill(&file, 42, KEK_TEST_IMAGE_PIXELS_MAX);

    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, load());
}

void test_every_truncation_of_a_raw_image_is_rejected(void) {
    size_t size;

    raw_image();
    for (size = 0; size < file.size; ++size) {
        assert_rejected_cleanly(load_prefix(size));
    }
}

void test_every_truncation_of_an_rle_image_is_rejected(void) {
    size_t size;

    rle_image();
    for (size = 0; size < file.size; ++size) {
        assert_rejected_cleanly(load_prefix(size));
    }
}

void test_a_bad_magic_is_rejected(void) {
    size_t i;

    for (i = 0; i < 4; ++i) {
        kek_test_file_clear(&file);
        raw_image();
        file.bytes[i] ^= 0x20;
        assert_rejected_cleanly(load());
    }

    kek_test_file_clear(&file);
    raw_image();
    memcpy(file.bytes, "KMDL", 4);
    assert_rejected_cleanly(load());
}

void test_a_version_other_than_one_is_rejected(void) {
    const uint16_t versions[] = { 0, 2, 0x0100, 0xFFFF };
    size_t i;

    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        kek_test_file_clear(&file);
        raw_image();
        kek_test_file_patch_u16(&file, KIF_OFFSET_VERSION, versions[i]);
        assert_rejected_cleanly(load());
    }
}

void test_a_zero_dimension_is_rejected(void) {
    raw_image();
    kek_test_file_patch_u16(&file, KIF_OFFSET_WIDTH, 0);
    assert_rejected_cleanly(load());

    kek_test_file_clear(&file);
    raw_image();
    kek_test_file_patch_u16(&file, KIF_OFFSET_HEIGHT, 0);
    assert_rejected_cleanly(load());
}

/* The same whole file loads with room for it and is refused without, so only
   the arena can be what refuses it. */
void test_an_image_larger_than_the_arena_is_rejected(void) {
    header(64, 64, 0);
    kek_test_file_fill(&file, 42, (size_t)64 * 64);
    TEST_ASSERT_NOT_EQUAL(KEK_TEXTURE_HANDLE_INVALID, load());

    start(1024);
    TEST_ASSERT_LESS_THAN_size_t((size_t)64 * 64, kek_arena_available(&e));
    assert_rejected_cleanly(load());
}

void test_an_unknown_encoding_is_rejected(void) {
    raw_image();
    file.bytes[KIF_OFFSET_ENCODING] = 2;
    assert_rejected_cleanly(load());

    file.bytes[KIF_OFFSET_ENCODING] = 0xFF;
    assert_rejected_cleanly(load());
}

void test_an_rle_run_of_zero_is_rejected(void) {
    header(2, 2, 1);
    kek_test_file_u8(&file, 2); kek_test_file_u8(&file, 1);
    kek_test_file_u8(&file, 0); kek_test_file_u8(&file, 1);
    kek_test_file_u8(&file, 2); kek_test_file_u8(&file, 1);
    assert_rejected_cleanly(load());
}

/* Refused rather than truncated: a run that would carry the image past
   width * height would write past the pixels the header promised. */
void test_an_rle_run_past_the_last_pixel_is_rejected(void) {
    header(2, 2, 1);
    kek_test_file_u8(&file, 3); kek_test_file_u8(&file, 1);
    kek_test_file_u8(&file, 2); kek_test_file_u8(&file, 1);
    assert_rejected_cleanly(load());
}

void test_a_missing_file_is_rejected(void) {
    raw_image();
    asset.size = file.size;
    assert_rejected_cleanly(kek_file_image_load(&e, "other.kif"));
}

void test_null_arguments_are_rejected(void) {
    raw_image();
    asset.size = file.size;
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_file_image_load(0, "image.kif"));
    assert_rejected_cleanly(kek_file_image_load(&e, 0));

    e.assets = 0;
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, kek_file_image_load(&e, "image.kif"));
}

void test_a_full_texture_pool_is_a_clean_failure(void) {
    raw_image();
    while (kek_texture_create(&e, 1, 1) != KEK_TEXTURE_HANDLE_INVALID) {
    }
    free_textures = 0;
    free_bytes = kek_arena_available(&e);
    assert_rejected_cleanly(load());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_raw_image_round_trips);
    RUN_TEST(test_an_rle_image_round_trips_with_runs_across_rows);
    RUN_TEST(test_a_256_by_256_image_loads);
    RUN_TEST(test_every_truncation_of_a_raw_image_is_rejected);
    RUN_TEST(test_every_truncation_of_an_rle_image_is_rejected);
    RUN_TEST(test_a_bad_magic_is_rejected);
    RUN_TEST(test_a_version_other_than_one_is_rejected);
    RUN_TEST(test_a_zero_dimension_is_rejected);
    RUN_TEST(test_an_image_larger_than_the_arena_is_rejected);
    RUN_TEST(test_an_unknown_encoding_is_rejected);
    RUN_TEST(test_an_rle_run_of_zero_is_rejected);
    RUN_TEST(test_an_rle_run_past_the_last_pixel_is_rejected);
    RUN_TEST(test_a_missing_file_is_rejected);
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_a_full_texture_pool_is_a_clean_failure);
    return UNITY_END();
}
