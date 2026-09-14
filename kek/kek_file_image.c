#include <stddef.h>
#include "kek_file_image.h"
#include "kek_texture.h"

KEK_TextureHandle kek_file_image_load(KEK_engine* e, const char* path) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;
    KEK_FileImage_Header hdr;
    KEK_TextureHandle handle;
    KEK_texture* out_texture;
    size_t pixel_count;
    size_t bytes_read;
    size_t written;

    if (!e || !e->assets || !path) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    if (!e->assets->stat(e->assets, path, &info) || info.size < sizeof(hdr)) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    if (!e->assets->open(e->assets, path, &stream)) {
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    if (kek_asset_read(&stream, &hdr, sizeof(hdr)) < sizeof(hdr)) {
        kek_asset_close(&stream);
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    if (hdr.magic[0] != 'K' || hdr.magic[1] != 'I' || hdr.magic[2] != 'M' || hdr.magic[3] != 'G' ||
        hdr.version != 1 ||
        hdr.width == 0 || hdr.height == 0) {
        kek_asset_close(&stream);
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    pixel_count = (size_t)hdr.width * (size_t)hdr.height;
    if (pixel_count > KEK_POOL_TEXTURE_PIXELS_MAX) {
        kek_asset_close(&stream);
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    handle = kek_texture_create(e);
    if (handle == KEK_TEXTURE_HANDLE_INVALID) {
        kek_asset_close(&stream);
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    out_texture = kek_texture_get(e, handle);
    if (!out_texture || !out_texture->data) {
        kek_asset_close(&stream);
        kek_texture_destroy(e, handle);
        return KEK_TEXTURE_HANDLE_INVALID;
    }

    if (hdr.encoding == 0) {
        if (info.size < sizeof(hdr) + pixel_count) {
            kek_asset_close(&stream);
            kek_texture_destroy(e, handle);
            return KEK_TEXTURE_HANDLE_INVALID;
        }

        bytes_read = kek_asset_read(&stream, out_texture->data, pixel_count);
        kek_asset_close(&stream);
        if (bytes_read < pixel_count) {
            kek_texture_destroy(e, handle);
            return KEK_TEXTURE_HANDLE_INVALID;
        }
        out_texture->width = hdr.width;
        out_texture->height = hdr.height;
        return handle;
    }

    if (hdr.encoding == 1) {
        written = 0;
        while (written < pixel_count) {
            uint8_t run[2];
            uint8_t count;
            uint8_t value;

            if (kek_asset_read(&stream, run, sizeof(run)) < sizeof(run)) {
                kek_asset_close(&stream);
                kek_texture_destroy(e, handle);
                return KEK_TEXTURE_HANDLE_INVALID;
            }

            count = run[0];
            value = run[1];
            if (count == 0 || written + count > pixel_count) {
                kek_asset_close(&stream);
                kek_texture_destroy(e, handle);
                return KEK_TEXTURE_HANDLE_INVALID;
            }

            for (size_t i = 0; i < count; ++i) {
                out_texture->data[written + i] = value;
            }
            written += count;
        }

        kek_asset_close(&stream);
        out_texture->width = hdr.width;
        out_texture->height = hdr.height;
        return handle;
    }

    kek_asset_close(&stream);
    kek_texture_destroy(e, handle);
    return KEK_TEXTURE_HANDLE_INVALID;
}
