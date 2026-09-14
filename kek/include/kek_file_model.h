#ifndef KEK_FILE_MODEL_H
#define KEK_FILE_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek.h"
#include "kek_math.h"

#define KEK_FILEMODEL_INDEX_NONE (0xFFFFu)

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

/*
Loads a model into the engine model pool and returns a handle.
*/
KEK_ModelHandle kek_file_model_load(KEK_engine* e, const char* path);

#ifdef __cplusplus
}
#endif

#endif // KEK_FILE_MODEL_H
