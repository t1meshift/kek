#include "platform_assets_fs.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FS_ASSET_PATH_CAPACITY 260

typedef struct FS_AssetStreamState {
    FILE* file;
    size_t size;
} FS_AssetStreamState;

static_assert(sizeof(FS_AssetStreamState) <= sizeof(((KEK_AssetStream*)0)->impl), "");
static_assert(offsetof(FS_AssetProvider, base) == 0, "");

static size_t _fs_asset_read(KEK_AssetStream* stream, void* dst, size_t size);
static int _fs_asset_seek(KEK_AssetStream* stream, size_t offset);
static size_t _fs_asset_tell(KEK_AssetStream* stream);
static size_t _fs_asset_size(KEK_AssetStream* stream);
static void _fs_asset_close(KEK_AssetStream* stream);
static int _fs_asset_stat(KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info);
static int _fs_asset_open(KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream);

static const KEK_AssetStreamVTable FS_ASSET_STREAM_VTABLE = {
    _fs_asset_read,
    _fs_asset_seek,
    _fs_asset_tell,
    _fs_asset_size,
    _fs_asset_close
};

static FS_AssetStreamState* _fs_asset_stream_state(KEK_AssetStream* stream) {
    return (FS_AssetStreamState*)stream->impl;
}

static const FS_AssetStreamState* _fs_asset_stream_state_const(const KEK_AssetStream* stream) {
    return (const FS_AssetStreamState*)stream->impl;
}

static int _fs_asset_build_path(char* dst, size_t dst_size, const char* root_path, const char* path) {
    size_t root_len;
    size_t path_len;

    if (!dst || !dst_size || !path) {
        return 0;
    }

    if (!root_path) {
        root_path = "";
    }

    root_len = strlen(root_path);
    path_len = strlen(path);
    if (root_len + path_len + 1 > dst_size) {
        return 0;
    }

    memcpy(dst, root_path, root_len);
    memcpy(dst + root_len, path, path_len + 1);
    return 1;
}

static FILE* _fs_asset_fopen_rb(const char* path) {
#if defined(_MSC_VER)
    FILE* file = NULL;
    return fopen_s(&file, path, "rb") == 0 ? file : NULL;
#else
    return fopen(path, "rb");
#endif
}

static int _fs_asset_query_size(FILE* file, size_t* out_size) {
    long file_size;

    if (!file || !out_size) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        return 0;
    }

    *out_size = (size_t)file_size;
    return 1;
}

static int _fs_asset_stat(KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info) {
    FS_AssetProvider* fs_provider;
    char full_path[FS_ASSET_PATH_CAPACITY];
    FILE* file;

    if (!provider || !path || !out_info) {
        return 0;
    }

    fs_provider = (FS_AssetProvider*)provider;
    if (!_fs_asset_build_path(full_path, sizeof(full_path), fs_provider->root_path, path)) {
        return 0;
    }

    file = _fs_asset_fopen_rb(full_path);
    if (!file) {
        return 0;
    }

    if (!_fs_asset_query_size(file, &out_info->size)) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

static int _fs_asset_open(KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream) {
    FS_AssetProvider* fs_provider;
    FS_AssetStreamState* state;
    char full_path[FS_ASSET_PATH_CAPACITY];
    FILE* file;
    size_t size;

    if (!provider || !path || !out_stream) {
        return 0;
    }

    memset(out_stream, 0, sizeof(*out_stream));

    fs_provider = (FS_AssetProvider*)provider;
    if (!_fs_asset_build_path(full_path, sizeof(full_path), fs_provider->root_path, path)) {
        return 0;
    }

    file = _fs_asset_fopen_rb(full_path);
    if (!file) {
        return 0;
    }

    if (!_fs_asset_query_size(file, &size)) {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    state = _fs_asset_stream_state(out_stream);
    state->file = file;
    state->size = size;
    out_stream->vt = &FS_ASSET_STREAM_VTABLE;
    return 1;
}

static size_t _fs_asset_read(KEK_AssetStream* stream, void* dst, size_t size) {
    FS_AssetStreamState* state;

    if (!stream || !dst) {
        return 0;
    }

    state = _fs_asset_stream_state(stream);
    if (!state->file) {
        return 0;
    }

    return fread(dst, 1, size, state->file);
}

static int _fs_asset_seek(KEK_AssetStream* stream, size_t offset) {
    FS_AssetStreamState* state;

    if (!stream) {
        return 0;
    }

    state = _fs_asset_stream_state(stream);
    if (!state->file || offset > state->size) {
        return 0;
    }

    return fseek(state->file, (long)offset, SEEK_SET) == 0;
}

static size_t _fs_asset_tell(KEK_AssetStream* stream) {
    FS_AssetStreamState* state;
    long offset;

    if (!stream) {
        return 0;
    }

    state = _fs_asset_stream_state(stream);
    if (!state->file) {
        return 0;
    }

    offset = ftell(state->file);
    if (offset < 0) {
        return 0;
    }

    return (size_t)offset;
}

static size_t _fs_asset_size(KEK_AssetStream* stream) {
    const FS_AssetStreamState* state;

    if (!stream) {
        return 0;
    }

    state = _fs_asset_stream_state_const(stream);
    return state->size;
}

static void _fs_asset_close(KEK_AssetStream* stream) {
    FS_AssetStreamState* state;

    if (!stream) {
        return;
    }

    state = _fs_asset_stream_state(stream);
    if (state->file) {
        fclose(state->file);
    }
    memset(state, 0, sizeof(*state));
}

void fs_asset_provider_init(FS_AssetProvider* provider, const char* root_path) {
    if (!provider) {
        return;
    }

    memset(provider, 0, sizeof(*provider));
    provider->base.stat = _fs_asset_stat;
    provider->base.open = _fs_asset_open;
    provider->root_path = root_path;
}
