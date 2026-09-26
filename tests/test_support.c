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
