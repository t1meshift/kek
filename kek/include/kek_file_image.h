#ifndef KEK_FILE_IMAGE_H
#define KEK_FILE_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "kek.h"
#include "kek_macro.h"

typedef struct KEK_FileImage_Header {
    char magic[4];      // "KIMG"
    uint16_t version;   // 1
    uint16_t width;     // 1..1024
    uint16_t height;    // 1..1024
    uint8_t encoding;   // 0 = raw indexed8, 1 = RLE indexed8
    uint8_t reserved[5];
} KEK_FileImage_Header;

/* Read with a single read(sizeof(hdr)), so the layout is the format rather than
   a detail. Has to keep matching KIF_HEADER_STRUCT = "<4sHHHB5x" in
   scripts/bmp_to_kif.py. See docs/formats.md. */
KEK_STATIC_ASSERT_DECL(kif_header_size, sizeof(KEK_FileImage_Header) == 16);
KEK_STATIC_ASSERT_DECL(kif_header_magic, offsetof(KEK_FileImage_Header, magic) == 0);
KEK_STATIC_ASSERT_DECL(kif_header_version, offsetof(KEK_FileImage_Header, version) == 4);
KEK_STATIC_ASSERT_DECL(kif_header_width, offsetof(KEK_FileImage_Header, width) == 6);
KEK_STATIC_ASSERT_DECL(kif_header_height, offsetof(KEK_FileImage_Header, height) == 8);
KEK_STATIC_ASSERT_DECL(kif_header_encoding, offsetof(KEK_FileImage_Header, encoding) == 10);
KEK_STATIC_ASSERT_DECL(kif_header_reserved, offsetof(KEK_FileImage_Header, reserved) == 11);

/*
Loads an image into the engine texture pool and returns a handle.
*/
KEK_TextureHandle kek_file_image_load(KEK_engine* e, const char* path);

#ifdef __cplusplus
}
#endif

#endif // KEK_FILE_IMAGE_H
