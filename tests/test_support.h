#ifndef KEK_TEST_SUPPORT_H
#define KEK_TEST_SUPPORT_H

/* Shared by the suites. Everything here goes through the engine's public API:
   the pool is measured by what it will hand out and the arena by
   kek_arena_available, never by reading a slot or a footer. */

#include <stddef.h>
#include <stdint.h>
#include "kek.h"

/* The frame kek_test_init gives an engine: VGA's, and the size the render
   suites attach by default. */
#define KEK_TEST_WIDTH 320u
#define KEK_TEST_HEIGHT 200u

/* The arena kek_test_init gives an engine: room for every suite's largest
   asset with plenty to spare. */
#define KEK_TEST_ARENA ((size_t)512u * 1024u)

/* kek_init over a block of the suites' own, sized for KEK_TEST_WIDTH x
   KEK_TEST_HEIGHT plus KEK_TEST_ARENA, with the default handle tables. Fails
   the test if kek_init does. */
void kek_test_init(KEK_engine* e);
/* The same with an arena of arena bytes, at most KEK_TEST_ARENA: for a test
   that needs the arena to run out. */
void kek_test_init_arena(KEK_engine* e, size_t arena);

/* How many more models (textures) the pool will hand out right now. Takes
   them all, counts, and gives them back, so the pool is left as it was apart
   from the generations of the slots it touched. Empty models and 1x1
   textures, so what runs out is the handle table rather than the arena. */
int kek_test_free_models(KEK_engine* e);
int kek_test_free_textures(KEK_engine* e);

/* A file under construction, written byte by byte in the order and byte order
   docs/formats.md gives — never by dumping a struct, which would only prove
   the loader agrees with itself. Sized for the largest test input, a 256x256
   KIF. */
#define KEK_TEST_IMAGE_PIXELS_MAX ((size_t)256u * 256u)
#define KEK_TEST_FILE_CAPACITY (16 + KEK_TEST_IMAGE_PIXELS_MAX + 1024)

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

/* A frame the engine renders into instead of its own. KEK_engine.fb and .db
   are pointers, so the test owns the storage: w * h pixels with guard bytes
   before and after, and anything the frame does not use counts as guard too.
   A render that strays by a byte shows up in kek_test_frame_assert_guards
   even where ASan, seeing one array, would not.

   Attaching also sets e->w and e->h, so a size other than 320x200 catches
   code that assumes one. */
#define KEK_TEST_FRAME_GUARD 256
#define KEK_TEST_FRAME_MAX_PIXELS ((size_t)KEK_TEST_WIDTH * KEK_TEST_HEIGHT)

void kek_test_frame_attach(KEK_engine* e, uint16_t w, uint16_t h);
/* Clears pixels and depth through kek_flush_buffers; guards are left alone. */
void kek_test_frame_clear(KEK_engine* e);
void kek_test_frame_assert_guards(void);
uint8_t kek_test_frame_pixel(const KEK_engine* e, int x, int y);
/* Pixels that are not 0, the colour kek_flush_buffers clears to. */
int kek_test_frame_painted(const KEK_engine* e);
int kek_test_frame_count(const KEK_engine* e, uint8_t color);

#endif // KEK_TEST_SUPPORT_H
