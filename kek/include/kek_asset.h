#ifndef KEK_ASSET_H
#define KEK_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct KEK_AssetStream KEK_AssetStream;
typedef struct KEK_AssetProvider KEK_AssetProvider;

typedef struct KEK_AssetInfo {
    size_t size;
} KEK_AssetInfo;

typedef struct KEK_AssetStreamVTable {
    size_t (*read)(KEK_AssetStream* stream, void* dst, size_t size);
    int (*seek)(KEK_AssetStream* stream, size_t offset);
    size_t (*tell)(KEK_AssetStream* stream);
    size_t (*size)(KEK_AssetStream* stream);
    void (*close)(KEK_AssetStream* stream);
} KEK_AssetStreamVTable;

struct KEK_AssetStream {
    const KEK_AssetStreamVTable* vt;
    unsigned char impl[64];
};

struct KEK_AssetProvider {
    int (*stat)(struct KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info);
    int (*open)(struct KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream);
};

static inline size_t kek_asset_read(KEK_AssetStream* stream, void* dst, size_t size) {
    if (!stream || !stream->vt || !stream->vt->read) {
        return 0;
    }

    return stream->vt->read(stream, dst, size);
}

static inline int kek_asset_seek(KEK_AssetStream* stream, size_t offset) {
    if (!stream || !stream->vt || !stream->vt->seek) {
        return 0;
    }

    return stream->vt->seek(stream, offset);
}

static inline size_t kek_asset_tell(KEK_AssetStream* stream) {
    if (!stream || !stream->vt || !stream->vt->tell) {
        return 0;
    }

    return stream->vt->tell(stream);
}

static inline size_t kek_asset_size(KEK_AssetStream* stream) {
    if (!stream || !stream->vt || !stream->vt->size) {
        return 0;
    }

    return stream->vt->size(stream);
}

static inline void kek_asset_close(KEK_AssetStream* stream) {
    if (!stream || !stream->vt) {
        return;
    }

    if (stream->vt->close) {
        stream->vt->close(stream);
    }
    stream->vt = 0;
}

#ifdef __cplusplus
}
#endif

#endif // KEK_ASSET_H
