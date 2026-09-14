#include "kek_model.h"
#include "kek_math.h"
#include "kek_texture.h"

static KEK_FVec3 _kek_cube_verts[] = {
    (KEK_FVec3) {-0.5, -0.5, -0.5},
    (KEK_FVec3) {-0.5, 0.5, -0.5},
    (KEK_FVec3) {0.5, 0.5, -0.5},
    (KEK_FVec3) {0.5, -0.5, -0.5},
    (KEK_FVec3) {-0.5, -0.5, 0.5},
    (KEK_FVec3) {-0.5, 0.5, 0.5},
    (KEK_FVec3) {0.5, 0.5, 0.5},
    (KEK_FVec3) {0.5, -0.5, 0.5}
};

static KEK_model_face _kek_cube_faces[] = {
    (KEK_model_face) {2, 6, 7},
    (KEK_model_face) {2, 7, 3},

    (KEK_model_face) {0, 4, 5},
    (KEK_model_face) {0, 5, 1},

    (KEK_model_face) {6, 2, 1},
    (KEK_model_face) {6, 1, 5},

    (KEK_model_face) {3, 7, 4},
    (KEK_model_face) {3, 4, 0},

    (KEK_model_face) {7, 6, 5},
    (KEK_model_face) {7, 5, 4},

    
    (KEK_model_face) {2, 3, 0},
    (KEK_model_face) {2, 0, 1}
};

static KEK_model_face_normal _kek_cube_face_normals[] = {
    (KEK_model_face_normal) {{1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
    (KEK_model_face_normal) {{1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
    (KEK_model_face_normal) {{-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}},
    (KEK_model_face_normal) {{-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}},
    (KEK_model_face_normal) {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}},
    (KEK_model_face_normal) {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}},
    (KEK_model_face_normal) {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}},
    (KEK_model_face_normal) {{0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, -1.f, 0.f}},
    (KEK_model_face_normal) {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}},
    (KEK_model_face_normal) {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}},
    (KEK_model_face_normal) {{0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}},
    (KEK_model_face_normal) {{0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}}
};

static uint8_t _kek_cube_colors[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};

static KEK_model_face_uv _kek_vts[] = {
    // {2, 6, 7}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {2, 7, 3}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },

    // {0, 4, 5}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {0, 5, 1}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },

    // {6, 2, 1}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {6, 1, 5}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },

    // {3, 7, 4}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {3, 4, 0}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },

    // {7, 6, 5}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {7, 5, 4}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },

    // {2, 3, 0}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 0.f},
        .c = (KEK_FVec2) {1.f, 1.f},
    },

    // {2, 0, 1}
    (KEK_model_face_uv) {
        .a = (KEK_FVec2) {0.f, 0.f},
        .b = (KEK_FVec2) {1.f, 1.f},
        .c = (KEK_FVec2) {0.f, 1.f},
    },
};

KEK_model KEK_CUBE_MODEL = (KEK_model) {
    .verts = _kek_cube_verts,
    .faces = _kek_cube_faces,
    .face_normals = _kek_cube_face_normals,
    .face_colors = _kek_cube_colors,
    .texture = &KEK_DEFAULT_TEXTURE,
    .face_textures = _kek_vts,
    .verts_count = 8,
    .faces_count = 12,
    .face_normals_count = 12,
    .colors_count = 12,
    .textures_count = 12
};
