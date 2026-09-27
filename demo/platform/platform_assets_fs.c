#include "platform_assets_fs.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <kek_macro.h>

#define FS_ASSET_PATH_CAPACITY 260

typedef struct FS_AssetStreamState {
    FILE* file;
    size_t size;
} FS_AssetStreamState;

/* KEK_STATIC_ASSERT_DECL rather than C11's static_assert: this file is built
   under plain C99 for the DOS target, whose DJGPP headers predate C11's
   assert.h additions. */
KEK_STATIC_ASSERT_DECL(fs_asset_stream_state_fits, sizeof(FS_AssetStreamState) <= sizeof(((KEK_AssetStream*)0)->impl));
KEK_STATIC_ASSERT_DECL(fs_asset_provider_base_first, offsetof(FS_AssetProvider, base) == 0);

static size_t fs_asset_read_(KEK_AssetStream* stream, void* dst, size_t size);
static int fs_asset_seek_(KEK_AssetStream* stream, size_t offset);
static size_t fs_asset_tell_(KEK_AssetStream* stream);
static size_t fs_asset_size_(KEK_AssetStream* stream);
static void fs_asset_close_(KEK_AssetStream* stream);
static int fs_asset_stat_(KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info);
static int fs_asset_open_(KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream);

static const KEK_AssetStreamVTable FS_ASSET_STREAM_VTABLE = {
    fs_asset_read_,
    fs_asset_seek_,
    fs_asset_tell_,
    fs_asset_size_,
    fs_asset_close_
};

static FS_AssetStreamState* fs_asset_stream_state_(KEK_AssetStream* stream) {
    return (FS_AssetStreamState*)stream->impl;
}

static const FS_AssetStreamState* fs_asset_stream_state_const_(const KEK_AssetStream* stream) {
    return (const FS_AssetStreamState*)stream->impl;
}

static int fs_asset_build_path_(char* dst, size_t dst_size, const char* root_path, const char* path) {
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

static FILE* fs_asset_fopen_rb_(const char* path) {
#if defined(_MSC_VER)
    FILE* file = NULL;
    return fopen_s(&file, path, "rb") == 0 ? file : NULL;
#else
    return fopen(path, "rb");
#endif
}

static int fs_asset_query_size_(FILE* file, size_t* out_size) {
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

static int fs_asset_stat_(KEK_AssetProvider* provider, const char* path, KEK_AssetInfo* out_info) {
    FS_AssetProvider* fs_provider;
    char full_path[FS_ASSET_PATH_CAPACITY];
    FILE* file;

    if (!provider || !path || !out_info) {
        return 0;
    }

    fs_provider = (FS_AssetProvider*)provider;
    if (!fs_asset_build_path_(full_path, sizeof(full_path), fs_provider->root_path, path)) {
        return 0;
    }

    file = fs_asset_fopen_rb_(full_path);
    if (!file) {
        return 0;
    }

    if (!fs_asset_query_size_(file, &out_info->size)) {
        (void)fclose(file);
        return 0;
    }

    (void)fclose(file);
    return 1;
}

static int fs_asset_open_(KEK_AssetProvider* provider, const char* path, KEK_AssetStream* out_stream) {
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
    if (!fs_asset_build_path_(full_path, sizeof(full_path), fs_provider->root_path, path)) {
        return 0;
    }

    file = fs_asset_fopen_rb_(full_path);
    if (!file) {
        return 0;
    }

    if (!fs_asset_query_size_(file, &size)) {
        (void)fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return 0;
    }

    state = fs_asset_stream_state_(out_stream);
    state->file = file;
    state->size = size;
    out_stream->vt = &FS_ASSET_STREAM_VTABLE;
    return 1;
}

static size_t fs_asset_read_(KEK_AssetStream* stream, void* dst, size_t size) {
    FS_AssetStreamState* state;

    if (!stream || !dst) {
        return 0;
    }

    state = fs_asset_stream_state_(stream);
    if (!state->file) {
        return 0;
    }

    return fread(dst, 1, size, state->file);
}

static int fs_asset_seek_(KEK_AssetStream* stream, size_t offset) {
    FS_AssetStreamState* state;

    if (!stream) {
        return 0;
    }

    state = fs_asset_stream_state_(stream);
    if (!state->file || offset > state->size) {
        return 0;
    }

    return fseek(state->file, (long)offset, SEEK_SET) == 0;
}

static size_t fs_asset_tell_(KEK_AssetStream* stream) {
    FS_AssetStreamState* state;
    long offset;

    if (!stream) {
        return 0;
    }

    state = fs_asset_stream_state_(stream);
    if (!state->file) {
        return 0;
    }

    offset = ftell(state->file);
    if (offset < 0) {
        return 0;
    }

    return (size_t)offset;
}

static size_t fs_asset_size_(KEK_AssetStream* stream) {
    const FS_AssetStreamState* state;

    if (!stream) {
        return 0;
    }

    state = fs_asset_stream_state_const_(stream);
    return state->size;
}

static void fs_asset_close_(KEK_AssetStream* stream) {
    FS_AssetStreamState* state;

    if (!stream) {
        return;
    }

    state = fs_asset_stream_state_(stream);
    if (state->file) {
        (void)fclose(state->file);
    }
    memset(state, 0, sizeof(*state));
}

void fs_asset_provider_init(FS_AssetProvider* provider, const char* root_path) {
    if (!provider) {
        return;
    }

    memset(provider, 0, sizeof(*provider));
    provider->base.stat = fs_asset_stat_;
    provider->base.open = fs_asset_open_;
    provider->root_path = root_path;
}
