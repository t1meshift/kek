#include "kek_model.h"
#include "kek_math.h"
#include "kek_texture.h"

/* The unit cube around the origin: every corner is at one end of the box or
   the other, so quantising loses nothing. */
static KEK_model_vertex kek_cube_verts_[] = {
    {  0,   0,   0},
    {  0, 255,   0},
    {255, 255,   0},
    {255,   0,   0},
    {  0,   0, 255},
    {  0, 255, 255},
    {255, 255, 255},
    {255,   0, 255}
};

static KEK_model_face kek_cube_faces_[] = {
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

static uint8_t kek_cube_colors_[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};

static KEK_FVec2 kek_cube_uvs_[] = {
    {0.f, 0.f},
    {1.f, 0.f},
    {1.f, 1.f},
    {0.f, 1.f}
};

/* Each side's two faces split the texture along its diagonal. */
static KEK_model_face_uv kek_cube_face_uvs_[] = {
    {0, 1, 2}, {0, 2, 3},
    {0, 1, 2}, {0, 2, 3},
    {0, 1, 2}, {0, 2, 3},
    {0, 1, 2}, {0, 2, 3},
    {0, 1, 2}, {0, 2, 3},
    {0, 1, 2}, {0, 2, 3}
};

KEK_model KEK_CUBE_MODEL = {
    .verts = kek_cube_verts_,
    .scale = {1.f / 255.f, 1.f / 255.f, 1.f / 255.f},
    .offset = {-0.5f, -0.5f, -0.5f},
    .faces = kek_cube_faces_,
    .face_colors = kek_cube_colors_,
    .uvs = kek_cube_uvs_,
    .face_uvs = kek_cube_face_uvs_,
    /* Texture handles only exist once a pool does; kek_pool_init hands the
       cloned cube the default texture's handle. */
    .texture = KEK_TEXTURE_HANDLE_INVALID,
    .owns_texture = 0,
    .verts_count = 8,
    .faces_count = 12,
    .colors_count = 12,
    .uvs_count = 4,
    .face_uvs_count = 12
};

/* One axis: the step and the start, from the least and greatest coordinate. */
static void kek_model_axis_(float least, float greatest, float* out_scale, float* out_offset) {
    float extent = greatest - least;

    *out_offset = least;
    *out_scale = extent > 0.f ? extent / 255.f : 0.f;
}

/* The step nearest value, written so that NaN fails the first test. */
static uint8_t kek_model_step_(float value, float offset, float scale) {
    float step;

    if (!(scale > 0.f)) {
        return 0;
    }
    step = (value - offset) / scale + 0.5f;
    if (!(step >= 0.f)) {
        return 0;
    }
    return step >= 255.f ? 255 : (uint8_t)step;
}

/* The box around the coordinates that are numbers, one axis at a time, so a
   NaN anywhere — first included — leaves the others' box as it is. */
static void kek_model_extend_(float value, int* found, float* least, float* greatest) {
    if (value != value) {
        return;
    }
    if (!*found) {
        *least = *greatest = value;
        *found = 1;
    } else if (value < *least) {
        *least = value;
    } else if (value > *greatest) {
        *greatest = value;
    }
}

void kek_model_quantise(KEK_model* model, const KEK_FVec3* positions) {
    KEK_FVec3 least = {0.f, 0.f, 0.f}, greatest = {0.f, 0.f, 0.f};
    int found_x = 0, found_y = 0, found_z = 0;
    uint16_t i;

    if (!model || !positions) {
        return;
    }

    for (i = 0; i < model->verts_count; ++i) {
        kek_model_extend_(positions[i].x, &found_x, &least.x, &greatest.x);
        kek_model_extend_(positions[i].y, &found_y, &least.y, &greatest.y);
        kek_model_extend_(positions[i].z, &found_z, &least.z, &greatest.z);
    }
    kek_model_axis_(least.x, greatest.x, &model->scale.x, &model->offset.x);
    kek_model_axis_(least.y, greatest.y, &model->scale.y, &model->offset.y);
    kek_model_axis_(least.z, greatest.z, &model->scale.z, &model->offset.z);

    for (i = 0; i < model->verts_count; ++i) {
        model->verts[i].x = kek_model_step_(positions[i].x, model->offset.x, model->scale.x);
        model->verts[i].y = kek_model_step_(positions[i].y, model->offset.y, model->scale.y);
        model->verts[i].z = kek_model_step_(positions[i].z, model->offset.z, model->scale.z);
    }
}

KEK_FVec3 kek_model_position(const KEK_model* model, uint16_t index) {
    KEK_model_vertex v = model->verts[index];

    return (KEK_FVec3){
        model->offset.x + model->scale.x * (float)v.x,
        model->offset.y + model->scale.y * (float)v.y,
        model->offset.z + model->scale.z * (float)v.z
    };
}
