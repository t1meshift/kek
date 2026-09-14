#ifndef KEK_PALETTE_H
#define KEK_PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef union KEK_palette_item {
    struct KEK_palette_channels {
        uint8_t r:6;
        uint8_t g:6;
        uint8_t b:6;
        uint16_t filler:14;
    } channels;
    uint32_t color:18;
} KEK_palette_item;

extern KEK_palette_item KEK_DEFAULT_PALETTE[256];

uint8_t kek_palette_nearest_color(const KEK_palette_item* palette, KEK_palette_item color);
void kek_palette_calculate_shading(uint8_t* shading_palette, const KEK_palette_item* palette);
uint8_t kek_palette_shade(const uint8_t* sahdes, uint8_t base, int shade);

#ifdef __cplusplus
}
#endif

#endif
