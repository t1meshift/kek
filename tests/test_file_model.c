/* The KMF loader, through the in-memory provider. Inputs are built here from
   the layout in docs/formats.md; nothing is read from disk.

   Every rejection is also a leak check: a failed load must leave the model
   pool and the texture pool with exactly the free capacity they had before —
   the loader takes a model slot before it has finished validating, and a
   texture slot on its way out, and both have to be given back on every path.
   The arena has to have exactly the free bytes it had, too, which is also
   what shows the staging taken from its top was given back on every path. */

#include <math.h>
#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_asset_memory.h"
#include "kek_file_model.h"
#include "kek_model.h"
#include "kek_texture.h"
#include "test_support.h"

#define KMF_HEADER_SIZE 20
#define KMF_OFFSET_VERSION 4

#define NONE KEK_FILEMODEL_INDEX_NONE
#define HAS_COLORS KEK_FILEMODEL_HAS_FACE_COLORS
#define HAS_TEXTURE KEK_FILEMODEL_HAS_TEXTURE

/* A large model: the most of everything a model used to be allowed before the
   arena. BIG leaves room for one past it. */
#define LARGE 1024
#define BIG (2 * LARGE + 1)

typedef struct Corner {
    uint16_t vertex, normal, uv;
} Corner;

typedef struct Face {
    Corner c[3];
} Face;

/* What gets written. The header counts decide how much of each array goes in
   the body, so a variant is the good model with a field or two changed. */
static struct {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint16_t vertices_count;
    uint16_t faces_count;
    uint16_t normals_count;
    uint16_t uv_count;
    uint8_t name_size;
    char name[256];
    KEK_FVec3 vertices[BIG];
    KEK_FVec3 normals[BIG];
    KEK_FVec2 uvs[BIG];
    Face faces[BIG];
    uint8_t colors[BIG];
} kmf;

static KEK_engine e;
static KEK_TestFile model_file;
static KEK_TestFile image_file;
static KEK_MemoryAsset assets[2];
static KEK_MemoryAssetProvider provider;
static int free_models;
static int free_textures;
static size_t free_bytes;

static const uint8_t TEXTURE_PIXELS[2 * 2] = { 11, 12, 13, 14 };

static void write_kmf(void) {
    uint16_t i;
    int j;

    kek_test_file_clear(&model_file);
    kek_test_file_bytes(&model_file, kmf.magic, 4);
    kek_test_file_u16(&model_file, kmf.version);
    kek_test_file_u16(&model_file, kmf.flags);
    kek_test_file_u16(&model_file, kmf.vertices_count);
    kek_test_file_u16(&model_file, kmf.faces_count);
    kek_test_file_u16(&model_file, kmf.normals_count);
    kek_test_file_u16(&model_file, kmf.uv_count);
    kek_test_file_u16(&model_file, 0);
    kek_test_file_u8(&model_file, kmf.name_size);
    kek_test_file_u8(&model_file, 0);
    TEST_ASSERT_EQUAL_size_t(KMF_HEADER_SIZE, model_file.size);

    kek_test_file_bytes(&model_file, kmf.name, kmf.name_size);
    for (i = 0; i < kmf.vertices_count; ++i) {
        kek_test_file_f32(&model_file, kmf.vertices[i].x);
        kek_test_file_f32(&model_file, kmf.vertices[i].y);
        kek_test_file_f32(&model_file, kmf.vertices[i].z);
    }
    for (i = 0; i < kmf.normals_count; ++i) {
        kek_test_file_f32(&model_file, kmf.normals[i].x);
        kek_test_file_f32(&model_file, kmf.normals[i].y);
        kek_test_file_f32(&model_file, kmf.normals[i].z);
    }
    for (i = 0; i < kmf.uv_count; ++i) {
        kek_test_file_f32(&model_file, kmf.uvs[i].x);
        kek_test_file_f32(&model_file, kmf.uvs[i].y);
    }
    for (i = 0; i < kmf.faces_count; ++i) {
        for (j = 0; j < 3; ++j) {
            kek_test_file_u16(&model_file, kmf.faces[i].c[j].vertex);
            kek_test_file_u16(&model_file, kmf.faces[i].c[j].normal);
            kek_test_file_u16(&model_file, kmf.faces[i].c[j].uv);
        }
    }
    if (kmf.flags & HAS_COLORS) {
        kek_test_file_bytes(&model_file, kmf.colors, kmf.faces_count);
    }

    assets[0].size = model_file.size;
}

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

static void set_face(uint16_t index, Corner a, Corner b, Corner c) {
    kmf.faces[index].c[0] = a;
    kmf.faces[index].c[1] = b;
    kmf.faces[index].c[2] = c;
}

static Corner corner(uint16_t vertex, uint16_t normal, uint16_t uv) {
    Corner result;
    result.vertex = vertex;
    result.normal = normal;
    result.uv = uv;
    return result;
}

/* A unit quad in z = 0 as two faces, textured and coloured, with a stored
   normal that is deliberately not unit length (the loader renormalises), a
   corner with no normal (it falls back to the face normal) and a corner with
   no UV (it becomes NaN). */
static void good_model(void) {
    memcpy(kmf.magic, "KMDL", 4);
    kmf.version = 1;
    kmf.flags = HAS_COLORS | HAS_TEXTURE;
    kmf.vertices_count = 4;
    kmf.faces_count = 2;
    kmf.normals_count = 2;
    kmf.uv_count = 4;
    memset(kmf.name, 0, sizeof(kmf.name));
    strcpy(kmf.name, "tex.kif");
    kmf.name_size = 8;

    kmf.vertices[0] = (KEK_FVec3){ 0.f, 0.f, 0.f };
    kmf.vertices[1] = (KEK_FVec3){ 1.f, 0.f, 0.f };
    kmf.vertices[2] = (KEK_FVec3){ 1.f, 1.f, 0.f };
    kmf.vertices[3] = (KEK_FVec3){ 0.f, 1.f, 0.f };
    kmf.normals[0] = (KEK_FVec3){ 0.f, 0.f, 2.f };
    kmf.normals[1] = (KEK_FVec3){ 0.f, 3.f, 4.f };
    kmf.uvs[0] = (KEK_FVec2){ 0.f, 0.f };
    kmf.uvs[1] = (KEK_FVec2){ 1.f, 0.f };
    kmf.uvs[2] = (KEK_FVec2){ 1.f, 1.f };
    kmf.uvs[3] = (KEK_FVec2){ 0.f, 1.f };
    set_face(0, corner(0, 0, 0), corner(1, 0, 1), corner(2, 1, 2));
    set_face(1, corner(0, 0, 0), corner(2, NONE, 2), corner(3, 1, NONE));
    kmf.colors[0] = 40;
    kmf.colors[1] = 41;
}

/* The same quad with nothing optional: no texture, no colours, no normals. */
static void bare_model(void) {
    uint16_t i;
    int j;

    good_model();
    kmf.flags = 0;
    kmf.normals_count = 0;
    kmf.uv_count = 0;
    kmf.name_size = 0;
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 3; ++j) {
            kmf.faces[i].c[j].normal = NONE;
            kmf.faces[i].c[j].uv = NONE;
        }
    }
}

/* `vertices` vertices and `faces` faces, all over the first three, for the
   cases that are about size. */
static void large_model(uint16_t vertices, uint16_t faces) {
    uint16_t i;

    bare_model();
    kmf.vertices_count = vertices;
    kmf.faces_count = faces;
    for (i = 0; i < vertices; ++i) {
        kmf.vertices[i] = (KEK_FVec3){ (float)i, (float)(i % 2), 0.f };
    }
    for (i = 0; i < faces; ++i) {
        set_face(i, corner(0, NONE, NONE), corner(1, NONE, NONE), corner(2, NONE, NONE));
    }
}

/* An engine with an arena of `arena` bytes, the asset provider attached, and
   the baseline every rejection is compared against. */
static void start(size_t arena) {
    kek_test_init_arena(&e, arena);
    e.assets = &provider.base;
    free_models = kek_test_free_models(&e);
    free_textures = kek_test_free_textures(&e);
    free_bytes = kek_arena_available(&e);
}

void setUp(void) {
    assets[0].path = "model.kmf";
    assets[0].bytes = model_file.bytes;
    assets[0].size = 0;
    assets[1].path = "tex.kif";
    assets[1].bytes = image_file.bytes;
    assets[1].size = 0;
    kek_asset_memory_init(&provider, assets, 2);

    good_model();
    write_kif();
    start(KEK_TEST_ARENA);
}

void tearDown(void) {
}

static KEK_ModelHandle load(void) {
    return kek_file_model_load(&e, "model.kmf");
}

static void assert_rejected_cleanly(KEK_ModelHandle handle) {
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(free_models, kek_test_free_models(&e), "a failed load kept a model slot");
    TEST_ASSERT_EQUAL_INT_MESSAGE(free_textures, kek_test_free_textures(&e), "a failed load kept a texture slot");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(free_bytes, kek_arena_available(&e), "a failed load kept arena memory");
}

static void write_and_reject(void) {
    write_kmf();
    assert_rejected_cleanly(load());
}

static void assert_vec3(float x, float y, float z, KEK_FVec3 actual) {
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, x, actual.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, y, actual.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, z, actual.z);
}

static void assert_face(uint32_t a, uint32_t b, uint32_t c, KEK_model_face actual) {
    TEST_ASSERT_EQUAL_UINT32(a, actual.a);
    TEST_ASSERT_EQUAL_UINT32(b, actual.b);
    TEST_ASSERT_EQUAL_UINT32(c, actual.c);
}

static void assert_uv(float u, float v, KEK_FVec2 actual) {
    TEST_ASSERT_EQUAL_FLOAT(u, actual.x);
    TEST_ASSERT_EQUAL_FLOAT(v, actual.y);
}

/* ---- Files that load ---- */

void test_the_documented_header_is_twenty_bytes(void) {
    write_kmf();
    /* Name, 4 vertices, 2 normals, 4 UVs, 2 faces, 2 colours. */
    TEST_ASSERT_EQUAL_size_t(20 + 8 + 4 * 12 + 2 * 12 + 4 * 8 + 2 * 18 + 2, model_file.size);
}

void test_a_textured_model_round_trips(void) {
    KEK_ModelHandle handle;
    KEK_model* mdl;
    KEK_texture* texture;
    uint32_t i;

    write_kmf();
    handle = load();
    mdl = kek_model_get(&e, handle);
    TEST_ASSERT_NOT_NULL(mdl);

    TEST_ASSERT_EQUAL_UINT32(4, mdl->verts_count);
    for (i = 0; i < 4; ++i) {
        assert_vec3(kmf.vertices[i].x, kmf.vertices[i].y, kmf.vertices[i].z, mdl->verts[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(2, mdl->faces_count);
    assert_face(0, 1, 2, mdl->faces[0]);
    assert_face(0, 2, 3, mdl->faces[1]);

    TEST_ASSERT_EQUAL_UINT32(2, mdl->colors_count);
    TEST_ASSERT_EQUAL_UINT8(40, mdl->face_colors[0]);
    TEST_ASSERT_EQUAL_UINT8(41, mdl->face_colors[1]);

    /* Three normals per face whatever the file stored: indexed ones
       renormalised, and the missing one the flat face normal. */
    TEST_ASSERT_EQUAL_UINT32(2, mdl->face_normals_count);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[0].a);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[0].b);
    assert_vec3(0.f, 0.6f, 0.8f, mdl->face_normals[0].c);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[1].a);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[1].b);
    assert_vec3(0.f, 0.6f, 0.8f, mdl->face_normals[1].c);

    /* UVs expanded per face corner; the corner without one is NaN, so it
       reads as wrong rather than as the texture's corner. */
    TEST_ASSERT_EQUAL_UINT32(2, mdl->textures_count);
    assert_uv(0.f, 0.f, mdl->face_textures[0].a);
    assert_uv(1.f, 0.f, mdl->face_textures[0].b);
    assert_uv(1.f, 1.f, mdl->face_textures[0].c);
    assert_uv(0.f, 0.f, mdl->face_textures[1].a);
    assert_uv(1.f, 1.f, mdl->face_textures[1].b);
    TEST_ASSERT_TRUE(isnan(mdl->face_textures[1].c.x) && isnan(mdl->face_textures[1].c.y));

    texture = kek_texture_get(&e, mdl->texture);
    TEST_ASSERT_NOT_NULL(texture);
    TEST_ASSERT_EQUAL_UINT16(2, texture->width);
    TEST_ASSERT_EQUAL_UINT16(2, texture->height);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEXTURE_PIXELS, texture->data, sizeof(TEXTURE_PIXELS));
    TEST_ASSERT_EQUAL_UINT8(1, mdl->owns_texture);

    TEST_ASSERT_EQUAL_INT(free_models - 1, kek_test_free_models(&e));
    TEST_ASSERT_EQUAL_INT(free_textures - 1, kek_test_free_textures(&e));
}

void test_destroying_a_loaded_model_gives_back_both_slots_and_their_memory(void) {
    write_kmf();
    kek_model_destroy(&e, load());

    TEST_ASSERT_EQUAL_INT(free_models, kek_test_free_models(&e));
    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
    TEST_ASSERT_EQUAL_size_t(free_bytes, kek_arena_available(&e));
}

void test_a_bare_model_loads_with_derived_normals(void) {
    KEK_model* mdl;

    bare_model();
    write_kmf();
    mdl = kek_model_get(&e, load());
    TEST_ASSERT_NOT_NULL(mdl);

    TEST_ASSERT_EQUAL_UINT32(4, mdl->verts_count);
    TEST_ASSERT_EQUAL_UINT32(2, mdl->faces_count);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->colors_count);
    TEST_ASSERT_EQUAL_UINT32(0, mdl->textures_count);
    TEST_ASSERT_EQUAL_UINT32(KEK_TEXTURE_HANDLE_INVALID, mdl->texture);
    TEST_ASSERT_EQUAL_UINT8(0, mdl->owns_texture);

    /* Counter-clockwise in x/y, so the cross product of the edges is +z. */
    TEST_ASSERT_EQUAL_UINT32(2, mdl->face_normals_count);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[0].a);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[0].b);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[0].c);
    assert_vec3(0.f, 0.f, 1.f, mdl->face_normals[1].c);

    TEST_ASSERT_EQUAL_INT(free_textures, kek_test_free_textures(&e));
}

/* Without HAS_TEXTURE the UV indices are not looked at, so an out-of-range one
   is not an error. docs/formats.md says as much. */
void test_uv_indices_are_ignored_without_a_texture(void) {
    bare_model();
    kmf.faces[0].c[0].uv = 1234;
    write_kmf();
    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, load());
}

void test_a_large_model_loads(void) {
    KEK_model* mdl;

    large_model(LARGE, LARGE);
    kmf.normals_count = LARGE;
    kmf.flags = HAS_COLORS;
    write_kmf();

    mdl = kek_model_get(&e, load());
    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_EQUAL_UINT32(LARGE, mdl->verts_count);
    TEST_ASSERT_EQUAL_UINT32(LARGE, mdl->faces_count);
    TEST_ASSERT_EQUAL_UINT32(LARGE, mdl->colors_count);
}

/* face_textures holds a UV triple per face, not per UV. When one limit bounded
   both, a textured model with more faces than that limit wrote past its slot
   (89c8fb3). Now the array is sized by faces_count, whatever uv_count is. */
void test_a_textured_model_with_more_faces_than_uvs_loads(void) {
    KEK_model* mdl;
    uint16_t i;

    good_model();
    kmf.faces_count = LARGE + 1;
    kmf.uv_count = LARGE;
    kmf.normals_count = 0;
    kmf.flags = HAS_TEXTURE;
    for (i = 0; i < LARGE; ++i) {
        kmf.uvs[i] = (KEK_FVec2){ (float)i, 0.f };
    }
    for (i = 0; i < kmf.faces_count; ++i) {
        set_face(i, corner(0, NONE, 0), corner(1, NONE, 1), corner(2, NONE, (uint16_t)(i % LARGE)));
    }
    write_kmf();

    mdl = kek_model_get(&e, load());
    TEST_ASSERT_NOT_NULL(mdl);
    TEST_ASSERT_EQUAL_UINT32(LARGE + 1, mdl->textures_count);
    assert_uv(0.f, 0.f, mdl->face_textures[LARGE].a);
    assert_uv(1.f, 0.f, mdl->face_textures[LARGE].b);
    assert_uv(0.f, 0.f, mdl->face_textures[LARGE].c); /* uv LARGE % LARGE */
    assert_uv((float)(LARGE - 1), 0.f, mdl->face_textures[LARGE - 1].c);
}

/* The same whole file loads with room for it and is refused without: first
   too little for even the staging, then enough for the staging but not for
   the model on top of it. */
void test_a_model_larger_than_the_arena_is_rejected(void) {
    large_model(LARGE, LARGE);
    kmf.normals_count = LARGE;
    kmf.flags = HAS_COLORS;
    write_kmf();
    TEST_ASSERT_NOT_EQUAL(KEK_MODEL_HANDLE_INVALID, load());

    start(4096);
    assert_rejected_cleanly(load());

    start(50000);
    assert_rejected_cleanly(load());
}

/* ---- Files that do not ---- */

void test_every_truncation_is_rejected(void) {
    size_t full, size;

    write_kmf();
    full = model_file.size;
    for (size = 0; size < full; ++size) {
        assets[0].size = size;
        assert_rejected_cleanly(load());
    }
}

void test_every_truncation_of_a_bare_model_is_rejected(void) {
    size_t full, size;

    bare_model();
    write_kmf();
    full = model_file.size;
    for (size = 0; size < full; ++size) {
        assets[0].size = size;
        assert_rejected_cleanly(load());
    }
}

/* The model is complete and the slot already taken when the texture is
   read, so this is the path where the model slot has to be handed back. */
void test_every_truncation_of_its_texture_is_rejected(void) {
    size_t full, size;

    write_kmf();
    full = image_file.size;
    for (size = 0; size < full; ++size) {
        assets[1].size = size;
        assert_rejected_cleanly(load());
    }
}

void test_a_bad_magic_is_rejected(void) {
    int i;

    for (i = 0; i < 4; ++i) {
        good_model();
        kmf.magic[i] = (char)(kmf.magic[i] ^ 0x20);
        write_and_reject();
    }

    good_model();
    memcpy(kmf.magic, "KIMG", 4);
    write_and_reject();
}

void test_a_version_other_than_one_is_rejected(void) {
    const uint16_t versions[] = { 0, 2, 0x0100, 0xFFFF };
    size_t i;

    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        good_model();
        write_kmf();
        kek_test_file_patch_u16(&model_file, KMF_OFFSET_VERSION, versions[i]);
        assert_rejected_cleanly(load());
    }
}

void test_zero_vertices_or_faces_are_rejected(void) {
    bare_model();
    kmf.vertices_count = 0;
    write_and_reject();

    bare_model();
    kmf.faces_count = 0;
    write_and_reject();
}

void test_a_vertex_index_out_of_range_is_rejected(void) {
    const uint16_t bad[] = { 4, 5, 0x7FFF, NONE };
    size_t i;
    int j;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        for (j = 0; j < 3; ++j) {
            good_model();
            kmf.faces[1].c[j].vertex = bad[i];
            write_and_reject();
        }
    }
}

void test_a_normal_index_out_of_range_is_rejected(void) {
    int j;

    for (j = 0; j < 3; ++j) {
        good_model();
        kmf.faces[1].c[j].normal = 2;
        write_and_reject();
    }
}

/* With normals_count == 0 no index is below the count, so every corner has
   to say "none". */
void test_a_normal_index_without_normals_is_rejected(void) {
    bare_model();
    kmf.faces[0].c[1].normal = 0;
    write_and_reject();
}

void test_a_uv_index_out_of_range_is_rejected(void) {
    int j;

    for (j = 0; j < 3; ++j) {
        good_model();
        kmf.faces[0].c[j].uv = 4;
        write_and_reject();
    }

    /* Every UV index the good model uses is now past the count. */
    good_model();
    kmf.uv_count = 0;
    write_and_reject();
}

void test_a_texture_name_without_its_terminator_is_rejected(void) {
    good_model();
    kmf.name_size = 7; /* "tex.kif" without the \0 */
    write_and_reject();
}

void test_a_texture_flag_without_a_name_is_rejected(void) {
    good_model();
    kmf.name_size = 0;
    write_and_reject();
}

void test_a_texture_that_is_not_there_is_rejected(void) {
    good_model();
    strcpy(kmf.name, "gone.kif");
    kmf.name_size = 9;
    write_and_reject();
}

void test_a_texture_that_is_not_a_kif_is_rejected(void) {
    good_model();
    image_file.bytes[0] = 'X';
    write_and_reject();
}

void test_a_model_naming_itself_as_its_texture_is_rejected(void) {
    good_model();
    strcpy(kmf.name, "model.kmf");
    kmf.name_size = 10;
    write_and_reject();
}

void test_a_missing_file_is_rejected(void) {
    write_kmf();
    assert_rejected_cleanly(kek_file_model_load(&e, "other.kmf"));
}

void test_null_arguments_are_rejected(void) {
    write_kmf();
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, kek_file_model_load(0, "model.kmf"));
    assert_rejected_cleanly(kek_file_model_load(&e, 0));

    e.assets = 0;
    TEST_ASSERT_EQUAL_UINT32(KEK_MODEL_HANDLE_INVALID, load());
}

void test_a_full_model_pool_is_a_clean_failure(void) {
    write_kmf();
    while (kek_model_create(&e, 0, 0, 0) != KEK_MODEL_HANDLE_INVALID) {
    }
    free_models = 0;
    free_bytes = kek_arena_available(&e);
    assert_rejected_cleanly(load());
}

/* The model slot is taken before the texture is loaded, so this is the other
   path that has to hand it back. */
void test_a_full_texture_pool_is_a_clean_failure(void) {
    write_kmf();
    while (kek_texture_create(&e, 1, 1) != KEK_TEXTURE_HANDLE_INVALID) {
    }
    free_textures = 0;
    free_bytes = kek_arena_available(&e);
    assert_rejected_cleanly(load());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_documented_header_is_twenty_bytes);
    RUN_TEST(test_a_textured_model_round_trips);
    RUN_TEST(test_destroying_a_loaded_model_gives_back_both_slots_and_their_memory);
    RUN_TEST(test_a_bare_model_loads_with_derived_normals);
    RUN_TEST(test_uv_indices_are_ignored_without_a_texture);
    RUN_TEST(test_a_large_model_loads);
    RUN_TEST(test_a_textured_model_with_more_faces_than_uvs_loads);
    RUN_TEST(test_a_model_larger_than_the_arena_is_rejected);
    RUN_TEST(test_every_truncation_is_rejected);
    RUN_TEST(test_every_truncation_of_a_bare_model_is_rejected);
    RUN_TEST(test_every_truncation_of_its_texture_is_rejected);
    RUN_TEST(test_a_bad_magic_is_rejected);
    RUN_TEST(test_a_version_other_than_one_is_rejected);
    RUN_TEST(test_zero_vertices_or_faces_are_rejected);
    RUN_TEST(test_a_vertex_index_out_of_range_is_rejected);
    RUN_TEST(test_a_normal_index_out_of_range_is_rejected);
    RUN_TEST(test_a_normal_index_without_normals_is_rejected);
    RUN_TEST(test_a_uv_index_out_of_range_is_rejected);
    RUN_TEST(test_a_texture_name_without_its_terminator_is_rejected);
    RUN_TEST(test_a_texture_flag_without_a_name_is_rejected);
    RUN_TEST(test_a_texture_that_is_not_there_is_rejected);
    RUN_TEST(test_a_texture_that_is_not_a_kif_is_rejected);
    RUN_TEST(test_a_model_naming_itself_as_its_texture_is_rejected);
    RUN_TEST(test_a_missing_file_is_rejected);
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_a_full_model_pool_is_a_clean_failure);
    RUN_TEST(test_a_full_texture_pool_is_a_clean_failure);
    return UNITY_END();
}
