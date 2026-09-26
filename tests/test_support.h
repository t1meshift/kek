#ifndef KEK_TEST_SUPPORT_H
#define KEK_TEST_SUPPORT_H

/* Shared by the suites. Everything here goes through the engine's public API:
   the pool is measured by what it will hand out, not by reading its slots, so
   these helpers keep working when Tier 2 puts an arena underneath. */

#include <stddef.h>
#include <stdint.h>
#include "kek.h"

/* How many more models (textures) the pool will hand out right now. Takes
   them all, counts, and gives them back, so the pool is left as it was apart
   from the generations of the slots it touched. */
int kek_test_free_models(KEK_engine* e);
int kek_test_free_textures(KEK_engine* e);

/* A file under construction, written byte by byte in the order and byte order
   docs/formats.md gives — never by dumping a struct, which would only prove
   the loader agrees with itself. Sized for the largest test input, a KIF at
   the texture pool's pixel limit. */
#define KEK_TEST_FILE_CAPACITY (16 + KEK_POOL_TEXTURE_PIXELS_MAX + 1024)

typedef struct KEK_TestFile {
    uint8_t bytes[KEK_TEST_FILE_CAPACITY];
    size_t size;
} KEK_TestFile;

void kek_test_file_clear(KEK_TestFile* file);
void kek_test_file_u8(KEK_TestFile* file, uint8_t value);
void kek_test_file_u16(KEK_TestFile* file, uint16_t value);
void kek_test_file_f32(KEK_TestFile* file, float value);
void kek_test_file_bytes(KEK_TestFile* file, const void* bytes, size_t size);
void kek_test_file_fill(KEK_TestFile* file, uint8_t value, size_t count);
/* Overwrites what is already there, for corrupting one field of a good file. */
void kek_test_file_patch_u16(KEK_TestFile* file, size_t offset, uint16_t value);

#endif // KEK_TEST_SUPPORT_H
