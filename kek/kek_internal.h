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

/* The (size_t) casts are on the row offset, not decoration: y * e->w is an int
   multiplication that only then widens to an index. Harmless at 320x200, but
   kek_flush_buffers next door already spells it out, and the two should agree. */

static inline void kek_blit(KEK_engine* e, uint16_t x, uint16_t y, uint8_t pixel) {
    e->fb[(size_t)y * e->w + x] = pixel;
}

static inline void kek_line(KEK_engine* e, uint16_t y, uint16_t x0, uint16_t x1, uint8_t pixel) {
    memset(e->fb + (size_t)y * e->w + x0, pixel, (size_t)(x1 - x0) + 1);
}

/*
The arena, kek_arena.c. Every pointer handed out is on a KEK_MEMORY_ALIGN
boundary, and a zero-size request succeeds with a pointer that must not be
dereferenced. Each returns null, and changes nothing, when the two ends would
cross.
*/

/* size bytes the engine keeps for good: kek_init's tables, laid out before any
   block. No bookkeeping, and no way to give them back. */
void* kek_arena_take(KEK_arena* a, size_t size);

/* An asset's storage, from the low end. *out_block is what kek_arena_free and
   kek_arena_block_after need to know it by. */
void* kek_arena_alloc(KEK_arena* a, size_t size, size_t* out_block);
void kek_arena_free(KEK_arena* a, size_t block);
/* Whether a block was allocated at or after a mark: what a release takes. */
int kek_arena_block_after(size_t block, KEK_ArenaMark mark);

/* A temporary from the high end, gone at the kek_arena_temp_release to the mark
   taken before it. */
size_t kek_arena_temp_mark(const KEK_arena* a);
void* kek_arena_temp(KEK_arena* a, size_t size);
void kek_arena_temp_release(KEK_arena* a, size_t mark);

/* kek_pool.c. The handle tables from the arena, then the default texture and
   cube; 0 if the arena cannot hold them, which kek_memory_size rules out. */
int kek_pool_init(KEK_engine* e, uint16_t models, uint16_t textures);
/* Every slot whose storage a release to mark takes goes stale. */
void kek_pool_release(KEK_engine* e, KEK_ArenaMark mark);

#endif // KEK_INTERNAL_H
