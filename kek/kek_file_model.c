#include <string.h>
#include "kek_file_model.h"
#include "kek_file_image.h"
#include "kek_math.h"
#include "kek_pool.h"
#include "kek_internal.h"

/* A zero-size read is a failure, not a trivially satisfied success: kek_asset_read
   returns 0 for a stream with no vtable, and 0 == 0 would report that out_data
   had been filled. Every caller already guards its count, so this refuses a call
   that should not happen rather than changing one that does. */
static int kek_file_model_read_exact(KEK_AssetStream* stream, void* out_data, size_t size) {
    return size > 0 && kek_asset_read(stream, out_data, size) == size;
}

/* Reads past count records the model has no use for. Read rather than seek:
   a short file then fails here, as it would reading them, and a stream without
   seek still loads. */
static int kek_file_model_skip_normals(KEK_AssetStream* stream, uint16_t count) {
    KEK_FileModel_Normal scratch[16];

    while (count > 0) {
        uint16_t n = count < 16u ? count : 16u;
        if (!kek_file_model_read_exact(stream, scratch, sizeof(scratch[0]) * n)) {
            return 0;
        }
        count = (uint16_t)(count - n);
    }
    return 1;
}

/* The file is staged whole before any of it reaches the model, because it has
   to be validated — face indices against the vertex count, UV indices against
   the UV count — first. The staging is temporaries off the top of the arena,
   sized by the header: kek_file_model_load takes a mark before this runs and
   releases to it after, on every path out. The texture load nested in here
   takes its pixels from the low end and never touches them. */
static KEK_ModelHandle kek_file_model_load_(KEK_engine *e, const char *path) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;
    KEK_FileModel_Header hdr;
    KEK_ModelHandle model_handle;
    KEK_model* out_model;
    KEK_FileModel_Vertex* vertices;
    KEK_FileModel_UV* uvs;
    KEK_FileModel_Face* faces;
    char texture_name[256];
    size_t texture_name_size;
    unsigned model_flags = 0;

    if (!e->assets || !path) {
        return KEK_MODEL_HANDLE_INVALID;
    }

    if (!e->assets->stat(e->assets, path, &info) || info.size < sizeof(hdr)) {
        return KEK_MODEL_HANDLE_INVALID;
    }

    if (!e->assets->open(e->assets, path, &stream)) {
        return KEK_MODEL_HANDLE_INVALID;
    }

    if (!kek_file_model_read_exact(&stream, &hdr, sizeof(hdr))) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    /* No upper limits: every count is a uint16_t, and what the arena cannot
       hold is refused where the staging or the model is taken. */
    if (hdr.magic[0] != 'K' || hdr.magic[1] != 'M' || hdr.magic[2] != 'D' || hdr.magic[3] != 'L' ||
        hdr.version != 1 ||
        hdr.vertices_count == 0 || hdr.faces_count == 0) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    vertices = (KEK_FileModel_Vertex*)kek_arena_temp(&e->arena, sizeof(vertices[0]) * hdr.vertices_count);
    uvs = (KEK_FileModel_UV*)kek_arena_temp(&e->arena, sizeof(uvs[0]) * hdr.uv_count);
    faces = (KEK_FileModel_Face*)kek_arena_temp(&e->arena, sizeof(faces[0]) * hdr.faces_count);
    if (!vertices || !uvs || !faces) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    texture_name_size = hdr.texture_name_size;
    if (texture_name_size > 0) {
        if (texture_name_size > sizeof(texture_name)) {
            kek_asset_close(&stream);
            return KEK_MODEL_HANDLE_INVALID;
        }
        if (!kek_file_model_read_exact(&stream, texture_name, texture_name_size)) {
            kek_asset_close(&stream);
            return KEK_MODEL_HANDLE_INVALID;
        }
        if (texture_name[texture_name_size - 1] != '\0') {
            kek_asset_close(&stream);
            return KEK_MODEL_HANDLE_INVALID;
        }
    }

    if (!kek_file_model_read_exact(&stream, vertices, sizeof(vertices[0]) * hdr.vertices_count)) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    /* Normals are in the format for smooth shading, which the engine does not
       do: their indices are still checked below, but the normals themselves
       are not kept. */
    if (!kek_file_model_skip_normals(&stream, hdr.normals_count)) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    if (hdr.uv_count > 0 &&
        !kek_file_model_read_exact(&stream, uvs, sizeof(uvs[0]) * hdr.uv_count)) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    if (!kek_file_model_read_exact(&stream, faces, sizeof(faces[0]) * hdr.faces_count)) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    /* The UVs are kept as the file has them, all uv_count of them, and each
       face keeps its three indices into them. */
    if ((hdr.flags & KEK_FILEMODEL_HAS_FACE_COLORS) != 0) {
        model_flags |= KEK_MODEL_FACE_COLORS;
    }
    if ((hdr.flags & KEK_FILEMODEL_HAS_TEXTURE) != 0) {
        model_flags |= KEK_MODEL_FACE_UVS;
    }
    model_handle = kek_model_create(e, hdr.vertices_count, hdr.faces_count, hdr.uv_count, model_flags);
    out_model = kek_model_get(e, model_handle);
    if (model_handle == KEK_MODEL_HANDLE_INVALID || !out_model) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    kek_model_quantise(out_model, vertices);
    out_model->colors_count = 0;
    out_model->face_uvs_count = 0;
    out_model->texture = KEK_TEXTURE_HANDLE_INVALID;
    out_model->owns_texture = 0;

    for (uint16_t i = 0; i < hdr.faces_count; ++i) {
        for (uint16_t j = 0; j < 3; ++j) {
            if (faces[i].v[j].vertex >= hdr.vertices_count) {
                kek_asset_close(&stream);
                kek_model_destroy(e, model_handle);
                return KEK_MODEL_HANDLE_INVALID;
            }
            if ((hdr.flags & KEK_FILEMODEL_HAS_TEXTURE) != 0 &&
                faces[i].v[j].uv != KEK_FILEMODEL_INDEX_NONE &&
                faces[i].v[j].uv >= hdr.uv_count) {
                kek_asset_close(&stream);
                kek_model_destroy(e, model_handle);
                return KEK_MODEL_HANDLE_INVALID;
            }
            if (faces[i].v[j].normal != KEK_FILEMODEL_INDEX_NONE &&
                faces[i].v[j].normal >= hdr.normals_count) {
                kek_asset_close(&stream);
                kek_model_destroy(e, model_handle);
                return KEK_MODEL_HANDLE_INVALID;
            }
        }

        out_model->faces[i] = (KEK_model_face) {
            .a = faces[i].v[0].vertex,
            .b = faces[i].v[1].vertex,
            .c = faces[i].v[2].vertex
        };
    }

    if ((hdr.flags & KEK_FILEMODEL_HAS_FACE_COLORS) != 0) {
        if (!kek_file_model_read_exact(&stream, out_model->face_colors, hdr.faces_count)) {
            kek_asset_close(&stream);
            kek_model_destroy(e, model_handle);
            return KEK_MODEL_HANDLE_INVALID;
        }
        out_model->colors_count = hdr.faces_count;
    }

    if ((hdr.flags & KEK_FILEMODEL_HAS_TEXTURE) != 0) {
        KEK_TextureHandle texture_handle;

        if (texture_name_size == 0) {
            kek_asset_close(&stream);
            kek_model_destroy(e, model_handle);
            return KEK_MODEL_HANDLE_INVALID;
        }

        /* KEK_FILEMODEL_INDEX_NONE is KEK_MODEL_UV_NONE, so a corner without
           a UV stays without one. */
        if (hdr.uv_count > 0) {
            memcpy(out_model->uvs, uvs, sizeof(uvs[0]) * hdr.uv_count);
        }
        for (uint16_t i = 0; i < hdr.faces_count; ++i) {
            out_model->face_uvs[i] = (KEK_model_face_uv) {
                .a = faces[i].v[0].uv,
                .b = faces[i].v[1].uv,
                .c = faces[i].v[2].uv
            };
        }
        out_model->face_uvs_count = hdr.faces_count;

        texture_handle = kek_file_image_load(e, texture_name);
        if (texture_handle == KEK_TEXTURE_HANDLE_INVALID) {
            kek_asset_close(&stream);
            kek_model_destroy(e, model_handle);
            return KEK_MODEL_HANDLE_INVALID;
        }
        /* The model loaded this texture, so the model releases it. */
        out_model->texture = texture_handle;
        out_model->owns_texture = 1;
    }

    kek_asset_close(&stream);
    return model_handle;
}

KEK_ModelHandle kek_file_model_load(KEK_engine *e, const char *path) {
    size_t mark;
    KEK_ModelHandle handle;

    if (!e) {
        return KEK_MODEL_HANDLE_INVALID;
    }
    mark = kek_arena_temp_mark(&e->arena);
    handle = kek_file_model_load_(e, path);
    kek_arena_temp_release(&e->arena, mark);
    return handle;
}
