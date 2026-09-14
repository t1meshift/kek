#include <math.h>
#include <string.h>
#include "kek_file_model.h"
#include "kek_file_image.h"
#include "kek_math.h"
#include "kek_pool.h"

static int kek_file_model_read_exact(KEK_AssetStream* stream, void* out_data, size_t size) {
    return kek_asset_read(stream, out_data, size) == size;
}

static KEK_FVec3 kek_file_model_subtract(KEK_FVec3 a, KEK_FVec3 b) {
    return (KEK_FVec3) {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z
    };
}

static KEK_FVec3 kek_file_model_cross(KEK_FVec3 a, KEK_FVec3 b) {
    return (KEK_FVec3) {
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x
    };
}

static KEK_FVec3 kek_file_model_calculate_face_normal(
    const KEK_FileModel_Vertex* vertices,
    const KEK_FileModel_Face* face
) {
    KEK_FVec3 edge_ab = kek_file_model_subtract(vertices[face->v[1].vertex], vertices[face->v[0].vertex]);
    KEK_FVec3 edge_ac = kek_file_model_subtract(vertices[face->v[2].vertex], vertices[face->v[0].vertex]);
    KEK_FVec3 face_normal = kek_file_model_cross(edge_ab, edge_ac);
    kek_normalize_fvec3(&face_normal);
    return face_normal;
}

static void kek_file_model_generate_normals(
    KEK_model* out_model,
    const KEK_FileModel_Vertex* vertices,
    const KEK_FileModel_Face* faces,
    uint16_t faces_count
) {
    for (uint16_t i = 0; i < faces_count; ++i) {
        KEK_FVec3 face_normal = kek_file_model_calculate_face_normal(vertices, &faces[i]);
        out_model->face_normals[i] = (KEK_model_face_normal) {
            .a = face_normal,
            .b = face_normal,
            .c = face_normal
        };
    }

    out_model->face_normals_count = faces_count;
}

KEK_ModelHandle kek_file_model_load(KEK_engine *e, const char *path) {
    KEK_AssetInfo info;
    KEK_AssetStream stream;
    KEK_FileModel_Header hdr;
    KEK_ModelHandle model_handle;
    KEK_model* out_model;
    KEK_FileModel_Vertex vertices[KEK_POOL_MODEL_VERTS_MAX];
    KEK_FileModel_Normal normals[KEK_POOL_MODEL_VERTS_MAX];
    KEK_FileModel_UV uvs[KEK_POOL_MODEL_UVS_MAX];
    KEK_FileModel_Face faces[KEK_POOL_MODEL_FACES_MAX];
    char texture_name[256];
    size_t texture_name_size;

    if (!e || !e->assets || !path) {
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

    if (hdr.magic[0] != 'K' || hdr.magic[1] != 'M' || hdr.magic[2] != 'D' || hdr.magic[3] != 'L' ||
        hdr.version != 1 ||
        hdr.vertices_count == 0 || hdr.faces_count == 0 ||
        hdr.vertices_count > KEK_POOL_MODEL_VERTS_MAX ||
        hdr.faces_count > KEK_POOL_MODEL_FACES_MAX ||
        hdr.uv_count > KEK_POOL_MODEL_UVS_MAX) {
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

    if (hdr.normals_count > 0) {
        if (hdr.normals_count > KEK_POOL_MODEL_VERTS_MAX ||
            !kek_file_model_read_exact(&stream, normals, sizeof(normals[0]) * hdr.normals_count)) {
            kek_asset_close(&stream);
            return KEK_MODEL_HANDLE_INVALID;
        }
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

    model_handle = kek_model_create(e);
    out_model = kek_model_get(e, model_handle);
    if (model_handle == KEK_MODEL_HANDLE_INVALID || !out_model) {
        kek_asset_close(&stream);
        return KEK_MODEL_HANDLE_INVALID;
    }

    memcpy(out_model->verts, vertices, sizeof(vertices[0]) * hdr.vertices_count);
    out_model->verts_count = hdr.vertices_count;
    out_model->faces_count = hdr.faces_count;
    out_model->face_normals_count = 0;
    out_model->colors_count = 0;
    out_model->textures_count = 0;
    out_model->texture = 0;

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

    if (hdr.normals_count > 0) {
        for (uint16_t i = 0; i < hdr.faces_count; ++i) {
            KEK_model_face_normal* face_normal = &out_model->face_normals[i];
            KEK_FVec3 fallback_normal = kek_file_model_calculate_face_normal(vertices, &faces[i]);
            for (uint16_t j = 0; j < 3; ++j) {
                uint16_t normal_index = faces[i].v[j].normal;
                KEK_FVec3 normal;

                if (normal_index == KEK_FILEMODEL_INDEX_NONE) {
                    normal = fallback_normal;
                } else {
                    normal = normals[normal_index];
                    kek_normalize_fvec3(&normal);
                }

                if (j == 0) face_normal->a = normal;
                if (j == 1) face_normal->b = normal;
                if (j == 2) face_normal->c = normal;
            }
        }

        out_model->face_normals_count = hdr.faces_count;
    } else {
        kek_file_model_generate_normals(out_model, vertices, faces, hdr.faces_count);
    }

    if ((hdr.flags & KEK_FILEMODEL_HAS_FACE_COLORS) != 0) {
        if (hdr.faces_count > KEK_POOL_MODEL_COLORS_MAX ||
            !kek_file_model_read_exact(&stream, out_model->face_colors, hdr.faces_count)) {
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

        for (uint16_t i = 0; i < hdr.faces_count; ++i) {
            KEK_model_face_uv* face_uv = &out_model->face_textures[i];
            for (uint16_t j = 0; j < 3; ++j) {
                KEK_FVec2 uv = {NAN, NAN};

                if (faces[i].v[j].uv != KEK_FILEMODEL_INDEX_NONE) {
                    uv = uvs[faces[i].v[j].uv];
                }

                if (j == 0) face_uv->a = uv;
                if (j == 1) face_uv->b = uv;
                if (j == 2) face_uv->c = uv;
            }
        }
        out_model->textures_count = hdr.faces_count;

        texture_handle = kek_file_image_load(e, texture_name);
        if (texture_handle == KEK_TEXTURE_HANDLE_INVALID) {
            kek_asset_close(&stream);
            kek_model_destroy(e, model_handle);
            return KEK_MODEL_HANDLE_INVALID;
        }
        out_model->texture = kek_texture_get(e, texture_handle);
    }

    kek_asset_close(&stream);
    return model_handle;
}
