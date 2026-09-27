#ifndef KEK_FILE_MODEL_H
#define KEK_FILE_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "kek.h"
#include "kek_macro.h"
#include "kek_math.h"
#include "kek_model.h"

#define KEK_FILEMODEL_INDEX_NONE (0xFFFFu)
/* The loader copies UV indices straight into KEK_model_face_uv. */
KEK_STATIC_ASSERT_DECL(kmf_index_none_is_model_uv_none, KEK_FILEMODEL_INDEX_NONE == KEK_MODEL_UV_NONE);

typedef enum KEK_FileModel_Flags {
    KEK_FILEMODEL_HAS_FACE_COLORS = 1 << 0,
    KEK_FILEMODEL_HAS_TEXTURE     = 1 << 1
} KEK_FileModel_Flags;

typedef struct KEK_FileModel_Header {
    char magic[4];             // "KMDL"
    uint16_t version;          // 1
    uint16_t flags;
    uint16_t vertices_count;
    uint16_t faces_count;
    uint16_t normals_count;
    uint16_t uv_count;
    uint16_t reserved;
    uint8_t texture_name_size; // including \0
} KEK_FileModel_Header;

typedef KEK_FVec3 KEK_FileModel_Vertex;
typedef KEK_FVec3 KEK_FileModel_Normal;
typedef KEK_FVec2 KEK_FileModel_UV;
typedef struct KEK_FileModel_FaceVertex {
    uint16_t vertex;
    uint16_t normal;
    uint16_t uv;
} KEK_FileModel_FaceVertex;
typedef struct KEK_FileModel_Face {
    KEK_FileModel_FaceVertex v[3];
} KEK_FileModel_Face;

/* KMF is read straight into these structs — the header with a single
   read(sizeof(hdr)), the arrays with one read each — so their in-memory layout
   *is* the on-disk layout, and the padding a compiler is free to insert is what
   would break it. The numbers below are the format, and they have to keep
   matching KMDL_HEADER_STRUCT = "<4sHHHHHHHBx" and its neighbours in
   scripts/obj_to_kmf.py, where they are written. See docs/formats.md.

   Nothing here checks byte order or float representation; the format is
   little-endian IEEE-754 binary32 and a big-endian target will need a
   conversion pass rather than a wider assert. */
KEK_STATIC_ASSERT_DECL(kmf_header_size, sizeof(KEK_FileModel_Header) == 20);
KEK_STATIC_ASSERT_DECL(kmf_header_magic, offsetof(KEK_FileModel_Header, magic) == 0);
KEK_STATIC_ASSERT_DECL(kmf_header_version, offsetof(KEK_FileModel_Header, version) == 4);
KEK_STATIC_ASSERT_DECL(kmf_header_flags, offsetof(KEK_FileModel_Header, flags) == 6);
KEK_STATIC_ASSERT_DECL(kmf_header_verts, offsetof(KEK_FileModel_Header, vertices_count) == 8);
KEK_STATIC_ASSERT_DECL(kmf_header_faces, offsetof(KEK_FileModel_Header, faces_count) == 10);
KEK_STATIC_ASSERT_DECL(kmf_header_normals, offsetof(KEK_FileModel_Header, normals_count) == 12);
KEK_STATIC_ASSERT_DECL(kmf_header_uvs, offsetof(KEK_FileModel_Header, uv_count) == 14);
KEK_STATIC_ASSERT_DECL(kmf_header_reserved, offsetof(KEK_FileModel_Header, reserved) == 16);
KEK_STATIC_ASSERT_DECL(kmf_header_texname, offsetof(KEK_FileModel_Header, texture_name_size) == 18);

KEK_STATIC_ASSERT_DECL(kmf_vertex_size, sizeof(KEK_FileModel_Vertex) == 12);
KEK_STATIC_ASSERT_DECL(kmf_normal_size, sizeof(KEK_FileModel_Normal) == 12);
KEK_STATIC_ASSERT_DECL(kmf_uv_size, sizeof(KEK_FileModel_UV) == 8);

KEK_STATIC_ASSERT_DECL(kmf_facevertex_size, sizeof(KEK_FileModel_FaceVertex) == 6);
KEK_STATIC_ASSERT_DECL(kmf_facevertex_vertex, offsetof(KEK_FileModel_FaceVertex, vertex) == 0);
KEK_STATIC_ASSERT_DECL(kmf_facevertex_normal, offsetof(KEK_FileModel_FaceVertex, normal) == 2);
KEK_STATIC_ASSERT_DECL(kmf_facevertex_uv, offsetof(KEK_FileModel_FaceVertex, uv) == 4);
KEK_STATIC_ASSERT_DECL(kmf_face_size, sizeof(KEK_FileModel_Face) == 18);

/*
Loads a model into the engine model pool and returns a handle.
*/
KEK_ModelHandle kek_file_model_load(KEK_engine* e, const char* path);

#ifdef __cplusplus
}
#endif

#endif // KEK_FILE_MODEL_H
