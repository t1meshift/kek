#include <string.h>
#include "test_support.h"
#include "unity.h"
#include "kek_model.h"
#include "kek_texture.h"

void kek_test_init_arena(KEK_engine* e, size_t arena) {
    static unsigned char memory[KEK_MEMORY_SIZE(KEK_TEST_WIDTH, KEK_TEST_HEIGHT) + KEK_TEST_ARENA];
    const KEK_desc desc = { KEK_TEST_WIDTH, KEK_TEST_HEIGHT, 0, 0 };

    TEST_ASSERT_TRUE_MESSAGE(arena <= KEK_TEST_ARENA, "test arena larger than its storage");
    TEST_ASSERT_TRUE_MESSAGE(kek_init(e, &desc, memory, KEK_MEMORY_SIZE(KEK_TEST_WIDTH, KEK_TEST_HEIGHT) + arena),
                             "kek_init failed");
}

void kek_test_init(KEK_engine* e) {
    kek_test_init_arena(e, KEK_TEST_ARENA);
}

/* Far past any pool this engine configures; hitting it means the pool never
   said no, which is a failure in its own right. */
#define KEK_TEST_MAX_HANDLES 4096

int kek_test_free_models(KEK_engine* e) {
    static KEK_ModelHandle handles[KEK_TEST_MAX_HANDLES];
    int count = 0;
    int i;

    while (count < KEK_TEST_MAX_HANDLES) {
        KEK_ModelHandle handle = kek_model_create(e, 0, 0, 0);
        if (handle == KEK_MODEL_HANDLE_INVALID) {
            break;
        }
        handles[count++] = handle;
    }
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(KEK_TEST_MAX_HANDLES, count, "the model pool never refused");

    for (i = 0; i < count; ++i) {
        kek_model_destroy(e, handles[i]);
    }
    return count;
}

int kek_test_free_textures(KEK_engine* e) {
    static KEK_TextureHandle handles[KEK_TEST_MAX_HANDLES];
    int count = 0;
    int i;

    while (count < KEK_TEST_MAX_HANDLES) {
        KEK_TextureHandle handle = kek_texture_create(e, 1, 1);
        if (handle == KEK_TEXTURE_HANDLE_INVALID) {
            break;
        }
        handles[count++] = handle;
    }
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(KEK_TEST_MAX_HANDLES, count, "the texture pool never refused");

    for (i = 0; i < count; ++i) {
        kek_texture_destroy(e, handles[i]);
    }
    return count;
}

void kek_test_file_clear(KEK_TestFile* file) {
    file->size = 0;
}

static uint8_t* kek_test_file_reserve(KEK_TestFile* file, size_t count) {
    uint8_t* at;
    TEST_ASSERT_TRUE_MESSAGE(count <= sizeof(file->bytes) - file->size, "KEK_TestFile is full");
    at = file->bytes + file->size;
    file->size += count;
    return at;
}

void kek_test_file_u8(KEK_TestFile* file, uint8_t value) {
    *kek_test_file_reserve(file, 1) = value;
}

void kek_test_file_u16(KEK_TestFile* file, uint16_t value) {
    uint8_t* at = kek_test_file_reserve(file, 2);
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)(value >> 8);
}

/* binary32, little-endian: the bit pattern goes through a uint32_t so the
   byte order is chosen here rather than inherited from the host. */
void kek_test_file_f32(KEK_TestFile* file, float value) {
    uint32_t bits;
    uint8_t* at = kek_test_file_reserve(file, 4);
    memcpy(&bits, &value, sizeof(bits));
    at[0] = (uint8_t)(bits & 0xFFu);
    at[1] = (uint8_t)((bits >> 8) & 0xFFu);
    at[2] = (uint8_t)((bits >> 16) & 0xFFu);
    at[3] = (uint8_t)(bits >> 24);
}

void kek_test_file_bytes(KEK_TestFile* file, const void* bytes, size_t size) {
    if (size > 0) {
        memcpy(kek_test_file_reserve(file, size), bytes, size);
    }
}

void kek_test_file_fill(KEK_TestFile* file, uint8_t value, size_t count) {
    if (count > 0) {
        memset(kek_test_file_reserve(file, count), value, count);
    }
}

void kek_test_file_patch_u16(KEK_TestFile* file, size_t offset, uint16_t value) {
    TEST_ASSERT_TRUE_MESSAGE(offset + 2 <= file->size, "patch past the end of the file");
    file->bytes[offset] = (uint8_t)(value & 0xFFu);
    file->bytes[offset + 1] = (uint8_t)(value >> 8);
}

#define KEK_TEST_GUARD_BYTE 0xA5u

/* Guards on both sides, plus room for the largest frame a test attaches. */
static uint8_t kek_test_fb[KEK_TEST_FRAME_GUARD + KEK_TEST_FRAME_MAX_PIXELS + KEK_TEST_FRAME_GUARD];
static uint16_t kek_test_db[KEK_TEST_FRAME_GUARD + KEK_TEST_FRAME_MAX_PIXELS + KEK_TEST_FRAME_GUARD];
static size_t kek_test_frame_pixels;

void kek_test_frame_attach(KEK_engine* e, uint16_t w, uint16_t h) {
    TEST_ASSERT_TRUE_MESSAGE((size_t)w * h <= KEK_TEST_FRAME_MAX_PIXELS, "test frame larger than its storage");

    memset(kek_test_fb, KEK_TEST_GUARD_BYTE, sizeof(kek_test_fb));
    memset(kek_test_db, KEK_TEST_GUARD_BYTE, sizeof(kek_test_db));
    kek_test_frame_pixels = (size_t)w * h;

    e->fb = kek_test_fb + KEK_TEST_FRAME_GUARD;
    e->db = kek_test_db + KEK_TEST_FRAME_GUARD;
    e->w = w;
    e->h = h;
    kek_test_frame_clear(e);
}

void kek_test_frame_clear(KEK_engine* e) {
    kek_flush_buffers(e);
}

static void kek_test_assert_guard_bytes(const uint8_t* bytes, size_t count, const char* message) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (bytes[i] != KEK_TEST_GUARD_BYTE) {
            TEST_FAIL_MESSAGE(message);
        }
    }
}

void kek_test_frame_assert_guards(void) {
    const uint8_t* db_bytes = (const uint8_t*)kek_test_db;
    size_t used = KEK_TEST_FRAME_GUARD + kek_test_frame_pixels;

    kek_test_assert_guard_bytes(kek_test_fb, KEK_TEST_FRAME_GUARD,
        "framebuffer written before its first pixel");
    kek_test_assert_guard_bytes(kek_test_fb + used, sizeof(kek_test_fb) - used,
        "framebuffer written past its last pixel");
    kek_test_assert_guard_bytes(db_bytes, KEK_TEST_FRAME_GUARD * sizeof(uint16_t),
        "depth buffer written before its first entry");
    kek_test_assert_guard_bytes(db_bytes + used * sizeof(uint16_t), sizeof(kek_test_db) - used * sizeof(uint16_t),
        "depth buffer written past its last entry");
}

uint8_t kek_test_frame_pixel(const KEK_engine* e, int x, int y) {
    TEST_ASSERT_TRUE(x >= 0 && x < e->w && y >= 0 && y < e->h);
    return e->fb[(size_t)y * e->w + (size_t)x];
}

int kek_test_frame_painted(const KEK_engine* e) {
    return (int)kek_test_frame_pixels - kek_test_frame_count(e, 0);
}

int kek_test_frame_count(const KEK_engine* e, uint8_t color) {
    size_t i;
    int count = 0;
    for (i = 0; i < (size_t)e->w * e->h; ++i) {
        count += e->fb[i] == color;
    }
    return count;
}
