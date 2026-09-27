/* KEK_POOL_MODEL_UVS_MAX bounds two things: the distinct UVs a KMF may
   declare, and — because the pool's face_textures array holds one UV triple
   per face, not per UV — the faces_count of a textured model too. The
   default build config cannot show the second ever going wrong, because
   every KEK_POOL_MODEL_* limit is the same 1024 there, so a faces_count past
   UVS_MAX always fails the FACES_MAX check first. This file links against
   kek_test_small_uvs (see the top-level CMakeLists.txt), a second build of
   the engine with KEK_POOL_MODEL_UVS_MAX set below KEK_POOL_MODEL_FACES_MAX,
   which is the only way to get a faces_count that clears one limit but not
   the other. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_asset_memory.h"
#include "kek_file_model.h"
#include "kek_model.h"
#include "kek_texture.h"
#include "test_support.h"

#define NONE KEK_FILEMODEL_INDEX_NONE
#define HAS_TEXTURE KEK_FILEMODEL_HAS_TEXTURE

/* One more face than KEK_POOL_MODEL_UVS_MAX, comfortably under
   KEK_POOL_MODEL_FACES_MAX — the whole reason this suite exists. */
#define OVER_FACES (KEK_POOL_MODEL_UVS_MAX + 1)

static KEK_engine e;
static KEK_TestFile model_file;
static KEK_TestFile image_file;
static KEK_MemoryAsset assets[2];
static KEK_MemoryAssetProvider provider;
static int free_models;
static int free_textures;

static const uint8_t TEXTURE_PIXELS[2 * 2] = { 1, 2, 3, 4 };

static void write_kif(void) {
    kek_test_file_clear(&image_file);
    kek_test_file_bytes(&image_file, "KIMG", 4);
    kek_test_file_u16(&image_file, 1);
    kek_test_file_u16(&image_file, 2);
    kek_test_file_u16(&image_file, 2);
    kek_test_file_u8(&image_file, 0);
    kek_test_file_fill(&image_file, 0, 5);
    kek_test_file_bytes(&image_file, TEXTURE_PIXELS, sizeof(TEXTURE_PIXELS));
    assets[1].size = image_file.size;
}

/* A textured model over one triangle, repeated faces_count times, with
   KEK_POOL_MODEL_UVS_MAX UVs (uv_count itself stays in range — faces_count is
   the only thing this test varies). No normals, so every corner says NONE. */
static void write_kmf(uint16_t faces_count) {
    uint16_t i;
    int j;

    kek_test_file_clear(&model_file);
    kek_test_file_bytes(&model_file, "KMDL", 4);
    kek_test_file_u16(&model_file, 1);                      /* version */
    kek_test_file_u16(&model_file, HAS_TEXTURE);            /* flags */
    kek_test_file_u16(&model_file, 3);                      /* vertices_count */
    kek_test_file_u16(&model_file, faces_count);
    kek_test_file_u16(&model_file, 0);                      /* normals_count */
    kek_test_file_u16(&model_file, KEK_POOL_MODEL_UVS_MAX); /* uv_count, at its own limit */
    kek_test_file_u16(&model_file, 0);                      /* reserved */
    kek_test_file_u8(&model_file, 8);                       /* texture_name_size */
    kek_test_file_u8(&model_file, 0);                       /* header padding */
    kek_test_file_bytes(&model_file, "tex.kif", 8);

    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 1.f);
    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 0.f);
    kek_test_file_f32(&model_file, 1.f);
    kek_test_file_f32(&model_file, 0.f);

    for (i = 0; i < KEK_POOL_MODEL_UVS_MAX; ++i) {
        kek_test_file_f32(&model_file, (float)i);
        kek_test_file_f32(&model_file, (float)i);
    }

    for (i = 0; i < faces_count; ++i) {
        for (j = 0; j < 3; ++j) {
            kek_test_file_u16(&model_file, (uint16_t)j); /* vertex */
            kek_test_file_u16(&model_file, NONE);         /* normal */
            kek_test_file_u16(&model_file, 0);            /* uv */
        }
    }

    assets[0].size = model_file.size;
}

void setUp(void) {
    kek_test_init(&e);
    assets[0].path = "model.kmf";
    assets[0].bytes = model_file.bytes;
    assets[0].size = 0;
    assets[1].path = "tex.kif";
    assets[1].bytes = image_file.bytes;
    assets[1].size = 0;
    kek_asset_memory_init(&provider, assets, 2);
    e.assets = &provider.base;

    write_kif();
    free_models = kek_test_free_models(&e);
    free_textures = kek_test_free_textures(&e);
}

void tearDown(void) {
}

static KEK_ModelHandle load(void) {
    return kek_file_model_load(&e, "model.kmf");
}

/* Before the fix: the loader only ever checked uv_count against
   KEK_POOL_MODEL_UVS_MAX, so a textured model whose faces_count exceeds it
   writes past the end of the pool slot's face_textures array — and, with the
   small pool this suite links against, past the end of the pool's entire
   static storage, where ASan's global-buffer-overflow catches it every time
   rather than leaving it to corrupt whichever slot happened to be next. */
void test_faces_past_the_uv_limit_are_rejected_when_textured(void) {
    KEK_ModelHandle handle;

    write_kmf(OVER_FACES);
    handle = load();

    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(free_models, kek_test_free_models(&e), "a failed load kept a model slot");
    TEST_ASSERT_EQUAL_INT_MESSAGE(free_textures, kek_test_free_textures(&e), "a failed load kept a texture slot");
}

/* The fix must not reject a textured model whose faces_count sits exactly at
   the limit — only past it. */
void test_faces_at_the_uv_limit_still_load(void) {
    write_kmf(KEK_POOL_MODEL_UVS_MAX);
    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, load());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_faces_past_the_uv_limit_are_rejected_when_textured);
    RUN_TEST(test_faces_at_the_uv_limit_still_load);
    return UNITY_END();
}
