#include <string.h>
#include "test_support.h"
#include "unity.h"
#include "kek_model.h"
#include "kek_texture.h"

/* Far past any pool this engine configures; hitting it means the pool never
   said no, which is a failure in its own right. */
#define KEK_TEST_MAX_HANDLES 4096

int kek_test_free_models(KEK_engine* e) {
    static KEK_ModelHandle handles[KEK_TEST_MAX_HANDLES];
    int count = 0;
    int i;

    while (count < KEK_TEST_MAX_HANDLES) {
        KEK_ModelHandle handle = kek_model_create(e);
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
        KEK_TextureHandle handle = kek_texture_create(e);
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
