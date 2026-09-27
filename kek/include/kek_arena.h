#ifndef KEK_ARENA_H
#define KEK_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef KEK_ENGINE_DEFINED
#define KEK_ENGINE_DEFINED
typedef struct KEK_engine KEK_engine;
#endif

/* What is left of the engine's block once the frame and the palettes are laid
   out: the memory every model and texture comes from. Quake's hunk, in two
   ends. Assets are allocated from the low end and live until they are
   destroyed or released; temporaries — a loader's staging, a draw call's
   transformed vertices — come from the high end and are gone by the time the
   call that took them returns. Free space is [low, high).

   Nothing here is freed out of order. Destroying the most recent asset gives
   its memory straight back; destroying an older one marks it dead, and its
   memory comes back once everything above it has gone too. */
typedef struct KEK_arena {
    unsigned char* base;
    size_t low;
    size_t high;
    /* Below this, the handle tables and the default cube and texture: what a
       release never takes. */
    size_t floor;
} KEK_arena;

typedef size_t KEK_ArenaMark;

/* The lifetime of a level, or of anything else loaded as a batch: take a mark
   before loading it and release to the mark when done. Every model and texture
   created since is gone, and their handles no longer resolve. A mark taken
   before a later release to below it does nothing. */
KEK_ArenaMark kek_arena_mark(const KEK_engine* engine);
void kek_arena_release(KEK_engine* engine, KEK_ArenaMark mark);

/* Bytes between the two ends. An allocation of n costs n rounded up to
   KEK_MEMORY_ALIGN, plus KEK_MEMORY_ALIGN of bookkeeping. */
size_t kek_arena_available(const KEK_engine* engine);

#ifdef __cplusplus
}
#endif

#endif // KEK_ARENA_H
