#ifndef KEK_ASSET_MEMORY_H
#define KEK_ASSET_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "kek_asset.h"

/*
A KEK_AssetProvider over a table of files already in memory: assets linked
into the executable, a ROM image, or test input built on the fly. Pure C — no
stdio, no allocation — so it works on every target the engine does, including
the ones with no file system at all.

The provider neither copies nor owns anything. The table, every path in it and
every byte it points at have to outlive the provider and every stream opened on
it.
*/

typedef struct KEK_MemoryAsset {
    const char* path;  /* matched exactly, byte for byte */
    const void* bytes; /* may be null only when size is 0 */
    size_t size;
} KEK_MemoryAsset;

typedef struct KEK_MemoryAssetProvider {
    /* First, so that &provider->base is what the engine is handed and the
       callbacks can cast it back. */
    KEK_AssetProvider base;
    const KEK_MemoryAsset* assets;
    size_t count;
} KEK_MemoryAssetProvider;

/* When two entries share a path, the first one wins. */
void kek_asset_memory_init(KEK_MemoryAssetProvider* provider, const KEK_MemoryAsset* assets, size_t count);

#ifdef __cplusplus
}
#endif

#endif // KEK_ASSET_MEMORY_H
