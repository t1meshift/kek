#ifndef KEK_INTERNAL_H
#define KEK_INTERNAL_H

/*
Engine-internal declarations. Deliberately not under kek/include/ — nothing
outside the engine gets to see this.
*/

#include <stdint.h>
#include <string.h>
#include "kek.h"

/*
Unclipped fast paths. Every caller — kek_2d.c and kek_3d.c — has already
clipped against the framebuffer, so these skip the bounds check entirely.
That is the whole point of them, and it is also why they have no business
in the public header: an out-of-range x or y writes wherever it lands.
*/

static inline void kek_blit(KEK_engine* e, uint16_t x, uint16_t y, uint8_t pixel) {
    e->fb[y * e->w + x] = pixel;
}

static inline void kek_line(KEK_engine* e, uint16_t y, uint16_t x0, uint16_t x1, uint8_t pixel) {
    memset(e->fb + (y * e->w) + x0, pixel, x1 - x0 + 1);
}

#endif // KEK_INTERNAL_H
