#ifndef KEK_FILE_IMAGE_H
#define KEK_FILE_IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "kek.h"

typedef struct KEK_FileImage_Header {
    char magic[4];      // "KIMG"
    uint16_t version;   // 1
    uint16_t width;     // 1..1024
    uint16_t height;    // 1..1024
    uint8_t encoding;   // 0 = raw indexed8, 1 = RLE indexed8
    uint8_t reserved[5];
} KEK_FileImage_Header;

/*
Loads an image into the engine texture pool and returns a handle.
*/
KEK_TextureHandle kek_file_image_load(KEK_engine* e, const char* path);

#ifdef __cplusplus
}
#endif

#endif // KEK_FILE_IMAGE_H
