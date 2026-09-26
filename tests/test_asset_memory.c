/* The in-memory KEK_AssetProvider. The parser suites stand on it, so it gets
   its own suite first: a parser test that fails because the provider misread
   a truncated file would be chasing the wrong bug. */

#include <string.h>
#include "unity.h"
#include "kek_asset.h"
#include "kek_asset_memory.h"

static const unsigned char TEN_BYTES[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const unsigned char THREE_BYTES[3] = { 0xAA, 0xBB, 0xCC };

static const KEK_MemoryAsset ASSETS[] = {
    { "ten.bin", TEN_BYTES, sizeof(TEN_BYTES) },
    { "dir/three.bin", THREE_BYTES, sizeof(THREE_BYTES) },
    { "empty.bin", 0, 0 },
    { "broken.bin", 0, 5 },
    { "ten.bin", THREE_BYTES, sizeof(THREE_BYTES) }
};

static KEK_MemoryAssetProvider provider;
static KEK_AssetProvider* assets;

void setUp(void) {
    kek_asset_memory_init(&provider, ASSETS, sizeof(ASSETS) / sizeof(ASSETS[0]));
    assets = &provider.base;
}

void tearDown(void) {
}

void test_stat_reports_the_size_of_a_known_path(void) {
    KEK_AssetInfo info;

    TEST_ASSERT_TRUE(assets->stat(assets, "dir/three.bin", &info));
    TEST_ASSERT_EQUAL_size_t(3, info.size);
    TEST_ASSERT_TRUE(assets->stat(assets, "empty.bin", &info));
    TEST_ASSERT_EQUAL_size_t(0, info.size);
}

void test_paths_match_exactly(void) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;

    TEST_ASSERT_FALSE(assets->stat(assets, "missing.bin", &info));
    TEST_ASSERT_FALSE(assets->stat(assets, "ten", &info));
    TEST_ASSERT_FALSE(assets->stat(assets, "ten.bin ", &info));
    TEST_ASSERT_FALSE(assets->stat(assets, "three.bin", &info));
    TEST_ASSERT_FALSE(assets->stat(assets, "", &info));
    TEST_ASSERT_FALSE(assets->open(assets, "missing.bin", &stream));
    TEST_ASSERT_NULL(stream.vt);
}

void test_the_first_of_two_entries_with_one_path_wins(void) {
    KEK_AssetInfo info;

    TEST_ASSERT_TRUE(assets->stat(assets, "ten.bin", &info));
    TEST_ASSERT_EQUAL_size_t(10, info.size);
}

void test_an_entry_with_a_size_but_no_bytes_is_not_served(void) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;

    TEST_ASSERT_FALSE(assets->stat(assets, "broken.bin", &info));
    TEST_ASSERT_FALSE(assets->open(assets, "broken.bin", &stream));
}

void test_null_arguments_are_refused(void) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;
    KEK_MemoryAssetProvider empty;

    TEST_ASSERT_FALSE(assets->stat(assets, 0, &info));
    TEST_ASSERT_FALSE(assets->stat(assets, "ten.bin", 0));
    TEST_ASSERT_FALSE(assets->open(assets, 0, &stream));
    TEST_ASSERT_FALSE(assets->open(assets, "ten.bin", 0));

    kek_asset_memory_init(&empty, 0, 5);
    TEST_ASSERT_FALSE(empty.base.stat(&empty.base, "ten.bin", &info));
    kek_asset_memory_init(0, ASSETS, 1);
}

void test_reads_in_chunks_and_stops_at_the_end(void) {
    KEK_AssetStream stream;
    unsigned char buffer[16];

    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &stream));
    TEST_ASSERT_EQUAL_size_t(10, kek_asset_size(&stream));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_tell(&stream));

    memset(buffer, 0xEE, sizeof(buffer));
    TEST_ASSERT_EQUAL_size_t(4, kek_asset_read(&stream, buffer, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEN_BYTES, buffer, 4);
    TEST_ASSERT_EQUAL_size_t(4, kek_asset_tell(&stream));

    /* A read past the end is short, not a failure, and writes no further
       than what it returns. */
    memset(buffer, 0xEE, sizeof(buffer));
    TEST_ASSERT_EQUAL_size_t(6, kek_asset_read(&stream, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEN_BYTES + 4, buffer, 6);
    TEST_ASSERT_EQUAL_HEX8(0xEE, buffer[6]);
    TEST_ASSERT_EQUAL_size_t(10, kek_asset_tell(&stream));

    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, buffer, 1));
    kek_asset_close(&stream);
}

void test_a_zero_size_read_reads_nothing(void) {
    KEK_AssetStream stream;
    unsigned char byte = 0xEE;

    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &stream));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, &byte, 0));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, 0, 4));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_tell(&stream));
    TEST_ASSERT_EQUAL_HEX8(0xEE, byte);
    kek_asset_close(&stream);
}

void test_an_empty_asset_opens_and_reads_nothing(void) {
    KEK_AssetStream stream;
    unsigned char byte = 0;

    TEST_ASSERT_TRUE(assets->open(assets, "empty.bin", &stream));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_size(&stream));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, &byte, 1));
    kek_asset_close(&stream);
}

void test_seek_moves_within_the_asset_and_refuses_past_its_end(void) {
    KEK_AssetStream stream;
    unsigned char byte = 0;

    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &stream));

    TEST_ASSERT_TRUE(kek_asset_seek(&stream, 7));
    TEST_ASSERT_EQUAL_size_t(7, kek_asset_tell(&stream));
    TEST_ASSERT_EQUAL_size_t(1, kek_asset_read(&stream, &byte, 1));
    TEST_ASSERT_EQUAL_UINT8(7, byte);

    TEST_ASSERT_TRUE(kek_asset_seek(&stream, 10));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, &byte, 1));

    TEST_ASSERT_FALSE(kek_asset_seek(&stream, 11));
    TEST_ASSERT_EQUAL_size_t(10, kek_asset_tell(&stream));

    TEST_ASSERT_TRUE(kek_asset_seek(&stream, 0));
    TEST_ASSERT_EQUAL_size_t(1, kek_asset_read(&stream, &byte, 1));
    TEST_ASSERT_EQUAL_UINT8(0, byte);
    kek_asset_close(&stream);
}

void test_two_streams_on_one_asset_are_independent(void) {
    KEK_AssetStream a, b;
    unsigned char byte = 0;

    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &a));
    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &b));
    TEST_ASSERT_TRUE(kek_asset_seek(&a, 5));

    TEST_ASSERT_EQUAL_size_t(1, kek_asset_read(&b, &byte, 1));
    TEST_ASSERT_EQUAL_UINT8(0, byte);
    TEST_ASSERT_EQUAL_size_t(1, kek_asset_read(&a, &byte, 1));
    TEST_ASSERT_EQUAL_UINT8(5, byte);

    kek_asset_close(&a);
    kek_asset_close(&b);
}

void test_a_closed_stream_reads_nothing(void) {
    KEK_AssetStream stream;
    unsigned char byte = 0;

    TEST_ASSERT_TRUE(assets->open(assets, "ten.bin", &stream));
    kek_asset_close(&stream);
    TEST_ASSERT_NULL(stream.vt);
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_read(&stream, &byte, 1));
    TEST_ASSERT_EQUAL_size_t(0, kek_asset_size(&stream));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stat_reports_the_size_of_a_known_path);
    RUN_TEST(test_paths_match_exactly);
    RUN_TEST(test_the_first_of_two_entries_with_one_path_wins);
    RUN_TEST(test_an_entry_with_a_size_but_no_bytes_is_not_served);
    RUN_TEST(test_null_arguments_are_refused);
    RUN_TEST(test_reads_in_chunks_and_stops_at_the_end);
    RUN_TEST(test_a_zero_size_read_reads_nothing);
    RUN_TEST(test_an_empty_asset_opens_and_reads_nothing);
    RUN_TEST(test_seek_moves_within_the_asset_and_refuses_past_its_end);
    RUN_TEST(test_two_streams_on_one_asset_are_independent);
    RUN_TEST(test_a_closed_stream_reads_nothing);
    return UNITY_END();
}
