#include <stddef.h>
#include "kek.h"
#include "kek_arena.h"
#include "kek_internal.h"

/* Every block on the low end ends in one of these, so the top of the stack can
   always be read back: the footer just under `low` says how big the block
   beneath it is and whether anything still uses it. A block is known by its
   footer's offset — anything allocated after a mark has its footer above it,
   anything before has it below. */
typedef struct KEK_ArenaFooter_ {
    size_t size; /* of the block's payload, rounded */
    size_t alive;
} KEK_ArenaFooter_;

#define KEK_ARENA_FOOTER_ KEK_MEMORY_ROUND_(sizeof(KEK_ArenaFooter_))

static KEK_ArenaFooter_* kek_arena_footer_(const KEK_arena* a, size_t block) {
    return (KEK_ArenaFooter_*)(a->base + block);
}

/* Whether size bytes, rounded, and extra more fit between the ends. In this
   order so that neither a size near SIZE_MAX nor available - extra can wrap. */
static int kek_arena_fits_(const KEK_arena* a, size_t size, size_t extra) {
    size_t available = a->high - a->low;
    return extra <= available && size <= available - extra
        && KEK_MEMORY_ROUND_(size) <= available - extra;
}

/* Dead blocks at the top come off: whatever order things are destroyed in,
   the memory is back once the last of a run has gone. Nothing below the floor
   is ever dead, so the walk stops there at the latest. */
static void kek_arena_pop_dead_(KEK_arena* a) {
    while (a->low > a->floor) {
        size_t top = a->low - KEK_ARENA_FOOTER_;
        const KEK_ArenaFooter_* footer = kek_arena_footer_(a, top);
        if (footer->alive) {
            break;
        }
        a->low = top - footer->size;
    }
}

void* kek_arena_take(KEK_arena* a, size_t size) {
    void* result;

    if (!kek_arena_fits_(a, size, 0)) {
        return 0;
    }
    result = a->base + a->low;
    a->low += KEK_MEMORY_ROUND_(size);
    return result;
}

void* kek_arena_alloc(KEK_arena* a, size_t size, size_t* out_block) {
    KEK_ArenaFooter_* footer;
    size_t block;

    if (!kek_arena_fits_(a, size, KEK_ARENA_FOOTER_)) {
        return 0;
    }
    block = a->low + KEK_MEMORY_ROUND_(size);
    footer = kek_arena_footer_(a, block);
    footer->size = KEK_MEMORY_ROUND_(size);
    footer->alive = 1;
    *out_block = block;
    a->low = block + KEK_ARENA_FOOTER_;
    return a->base + (block - footer->size);
}

void kek_arena_free(KEK_arena* a, size_t block) {
    kek_arena_footer_(a, block)->alive = 0;
    kek_arena_pop_dead_(a);
}

int kek_arena_block_after(size_t block, KEK_ArenaMark mark) {
    return block >= mark;
}

size_t kek_arena_temp_mark(const KEK_arena* a) {
    return a->high;
}

void* kek_arena_temp(KEK_arena* a, size_t size) {
    if (!kek_arena_fits_(a, size, 0)) {
        return 0;
    }
    a->high -= KEK_MEMORY_ROUND_(size);
    return a->base + a->high;
}

void kek_arena_temp_release(KEK_arena* a, size_t mark) {
    if (mark > a->high) {
        a->high = mark;
    }
}

KEK_ArenaMark kek_arena_mark(const KEK_engine* e) {
    return e ? e->arena.low : 0;
}

void kek_arena_release(KEK_engine* e, KEK_ArenaMark mark) {
    if (!e) {
        return;
    }
    if (mark < e->arena.floor) {
        mark = e->arena.floor;
    }
    if (mark >= e->arena.low) {
        return;
    }
    kek_pool_release(e, mark);
    e->arena.low = mark;
    /* What was destroyed just under the mark may be on top now. */
    kek_arena_pop_dead_(&e->arena);
}

size_t kek_arena_available(const KEK_engine* e) {
    return e ? e->arena.high - e->arena.low : 0;
}
