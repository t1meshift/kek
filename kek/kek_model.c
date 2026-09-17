#include "kek_model.h"
#include "kek_math.h"
#include "kek_texture.h"

static KEK_FVec3 _kek_cube_verts[] = {
    {-0.5, -0.5, -0.5},
    {-0.5, 0.5, -0.5},
    {0.5, 0.5, -0.5},
    {0.5, -0.5, -0.5},
    {-0.5, -0.5, 0.5},
    {-0.5, 0.5, 0.5},
    {0.5, 0.5, 0.5},
    {0.5, -0.5, 0.5}
};

static KEK_model_face _kek_cube_faces[] = {
    {2, 6, 7},
    {2, 7, 3},

    {0, 4, 5},
    {0, 5, 1},

    {6, 2, 1},
    {6, 1, 5},

    {3, 7, 4},
    {3, 4, 0},

    {7, 6, 5},
    {7, 5, 4},

    
    {2, 3, 0},
    {2, 0, 1}
};

static KEK_model_face_normal _kek_cube_face_normals[] = {
    {{1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
    {{1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
    {{-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}},
    {{-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}},
    {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}},
    {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}},
    {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}},
    {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}},
    {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}},
    {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}},
    {{0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}},
    {{0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}}
};

static uint8_t _kek_cube_colors[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};

static KEK_model_face_uv _kek_vts[] = {
    // {2, 6, 7}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {2, 7, 3}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },

    // {0, 4, 5}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {0, 5, 1}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },

    // {6, 2, 1}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {6, 1, 5}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },

    // {3, 7, 4}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {3, 4, 0}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },

    // {7, 6, 5}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {7, 5, 4}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },

    // {2, 3, 0}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 0.f},
        .c = {1.f, 1.f},
    },

    // {2, 0, 1}
    {
        .a = {0.f, 0.f},
        .b = {1.f, 1.f},
        .c = {0.f, 1.f},
    },
};

KEK_model KEK_CUBE_MODEL = {
    .verts = _kek_cube_verts,
    .faces = _kek_cube_faces,
    .face_normals = _kek_cube_face_normals,
    .face_colors = _kek_cube_colors,
    .face_textures = _kek_vts,
    /* Texture handles only exist once a pool does; kek_pool_init hands the
       cloned cube the default texture's handle. */
    .texture = KEK_TEXTURE_HANDLE_INVALID,
    .owns_texture = 0,
    .verts_count = 8,
    .faces_count = 12,
    .face_normals_count = 12,
    .colors_count = 12,
    .textures_count = 12
};
