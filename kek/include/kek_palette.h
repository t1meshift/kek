#ifndef KEK_PALETTE_H
#define KEK_PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek_macro.h"

/* Every member is char-aligned, so no padding is possible and sizeof is 3 by
   the standard rather than by observation. 256 entries are then exactly 768
   bytes — the VGA palette block, which a DOS port can push straight at port
   0x3C9 with no repacking. */
typedef struct KEK_palette_item {
    uint8_t r, g, b; /* 0..63 — VGA DAC range */
} KEK_palette_item;

KEK_STATIC_ASSERT_DECL(palette_item_size, sizeof(KEK_palette_item) == 3);
KEK_STATIC_ASSERT_DECL(palette_block_size, sizeof(KEK_palette_item[256]) == 768);

extern KEK_palette_item KEK_DEFAULT_PALETTE[256];

uint8_t kek_palette_nearest_color(const KEK_palette_item* palette, KEK_palette_item color);
void kek_palette_calculate_shading(uint8_t* shading_palette, const KEK_palette_item* palette);
uint8_t kek_palette_shade(const uint8_t* shades, uint8_t base, int shade);

#ifdef __cplusplus
}
#endif

#endif
