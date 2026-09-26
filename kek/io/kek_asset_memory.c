#include <stddef.h>
#include <string.h>
#include "kek_asset_memory.h"
#include "kek_macro.h"

typedef struct KEK_MemoryAssetStreamState {
    const unsigned char* bytes;
    size_t size;
    size_t position;
} KEK_MemoryAssetStreamState;

KEK_STATIC_ASSERT_DECL(asset_memory_state_fits,
    sizeof(KEK_MemoryAssetStreamState) <= sizeof(((KEK_AssetStream*)0)->impl));

/* The state goes in and out of stream->impl by memcpy rather than through a
   cast pointer. impl is an unsigned char array: nothing promises it is aligned
   for a pointer member, and reading it through a struct type is an aliasing
   violation the optimiser is entitled to act on. Three words per call is not
   worth that bet. */
static KEK_MemoryAssetStreamState kek_asset_memory_load(const KEK_AssetStream* stream) {
    KEK_MemoryAssetStreamState state;
    memcpy(&state, stream->impl, sizeof(state));
    return state;
}

static void kek_asset_memory_store(KEK_AssetStream* stream, const KEK_MemoryAssetStreamState* state) {
    memcpy(stream->impl, state, sizeof(*state));
}

static size_t kek_asset_memory_read(KEK_AssetStream* stream, void* dst, size_t size) {
    KEK_MemoryAssetStreamState state;
    size_t remaining;

    if (!stream || !dst || size == 0) {
        return 0;
    }

    state = kek_asset_memory_load(stream);
    remaining = state.size - state.position;
    if (size > remaining) {
        size = remaining;
    }
    if (size == 0) {
        return 0;
    }

    memcpy(dst, state.bytes + state.position, size);
    state.position += size;
    kek_asset_memory_store(stream, &state);
    return size;
}

static int kek_asset_memory_seek(KEK_AssetStream* stream, size_t offset) {
    KEK_MemoryAssetStreamState state;

    if (!stream) {
        return 0;
    }

    state = kek_asset_memory_load(stream);
    /* Seeking to exactly the end is allowed, as it is for a file: the next
       read just returns 0. */
    if (offset > state.size) {
        return 0;
    }

    state.position = offset;
    kek_asset_memory_store(stream, &state);
    return 1;
}

static size_t kek_asset_memory_tell(KEK_AssetStream* stream) {
    if (!stream) {
        return 0;
    }
    return kek_asset_memory_load(stream).position;
}

static size_t kek_asset_memory_size(KEK_AssetStream* stream) {
    if (!stream) {
        return 0;
    }
    return kek_asset_memory_load(stream).size;
}

static void kek_asset_memory_close(KEK_AssetStream* stream) {
    if (!stream) {
        return;
    }
    memset(stream->impl, 0, sizeof(stream->impl));
}

static const KEK_AssetStreamVTable KEK_ASSET_MEMORY_STREAM_VTABLE = {
    kek_asset_memory_read,
    kek_asset_memory_seek,
    kek_asset_memory_tell,
    kek_asset_memory_size,
    kek_asset_memory_close
};

static const KEK_MemoryAsset* kek_asset_memory_find(KEK_AssetProvider* provider, const char* path) {
    /* base is the first member, so the provider the engine holds is the
       KEK_MemoryAssetProvider it was carved out of. */
    const KEK_MemoryAssetProvider* memory = (const KEK_MemoryAssetProvider*)provider;
    size_t i;

    if (!provider || !path || !memory->assets) {
        return 0;
    }

    for (i = 0; i < memory->count; ++i) {
        const KEK_MemoryAsset* asset = &memory->assets[i];
        /* An entry claiming bytes it does not point at is skipped, not served:
           the alternative is a read through a null pointer later on. */
        if (!asset->path || (!asset->bytes && asset->size > 0)) {
            continue;
        }
        if (strcmp(asset->path, path) == 0) {
            return asset;
        }
    }

    return 0;
}

static int kek_asset_memory_stat(KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info) {
    const KEK_MemoryAsset* asset;

    if (!out_info) {
        return 0;
    }

    asset = kek_asset_memory_find(provider, path);
    if (!asset) {
        return 0;
    }

    out_info->size = asset->size;
    return 1;
}

static int kek_asset_memory_open(KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream) {
    const KEK_MemoryAsset* asset;
    KEK_MemoryAssetStreamState state;

    if (!out_stream) {
        return 0;
    }

    memset(out_stream, 0, sizeof(*out_stream));

    asset = kek_asset_memory_find(provider, path);
    if (!asset) {
        return 0;
    }

    state.bytes = (const unsigned char*)asset->bytes;
    state.size = asset->size;
    state.position = 0;
    kek_asset_memory_store(out_stream, &state);
    out_stream->vt = &KEK_ASSET_MEMORY_STREAM_VTABLE;
    return 1;
}

void kek_asset_memory_init(KEK_MemoryAssetProvider* provider, const KEK_MemoryAsset* assets, size_t count) {
    if (!provider) {
        return;
    }

    provider->base.stat = kek_asset_memory_stat;
    provider->base.open = kek_asset_memory_open;
    provider->assets = assets;
    provider->count = assets ? count : 0;
}
