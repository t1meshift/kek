#include <stdio.h>
#include <math.h>
#include <kek_2d.h>
#include <kek_3d.h>
#include <kek_model.h>
#include "demo_tag.h"
#include "kek.h"
#include "kek_file_image.h"
#include "kek_file_model.h"
#include "kek_keyboard.h"
#include "kek_math.h"
#include "kek_texture.h"
#include "demo_scenes/demo_scene_entry.h"

#define DEMO_MIRROR_Z 6.f
#define DEMO_MIRROR_SIZE 128u
#define DEMO_SKY_COLOR 77u
#define DEMO_FLOOR_SIZE 50.f
#define DEMO_FLOOR_TILE 10.f
#define DEMO_FOG_START 15.f
#define DEMO_FOG_END 45.f

static const KEK_FVec3 DEMO_MODEL_POSITION = { 1.5f, -1.f, 4.5f };
static const KEK_FVec3 DEMO_FLOOR_POSITION = {0.f, -2.f, 0.f};
static const KEK_FVec3 DEMO_MIRROR_POSITION = {-1.f, -0.5f, DEMO_MIRROR_Z};

/* The engine applies Euler rotations in X, Y, Z order. Convert a camera's
   yaw-then-local-pitch orientation to those angles so pitching still looks
   up/down after turning sideways. */
static KEK_FVec3 DEMO_EntryScene_camera_rotation_(float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float horizontal = sqrtf(cy * cy + sy * sy * sp * sp);
    return (KEK_FVec3){
        atan2f(sp, cy * cp),
        atan2f(sy * cp, horizontal),
        atan2f(sy * sp, cy)
    };
}

/* +1, -1 or 0 when both or neither are held, as one input axis. */
static float key_axis(const KEK_engine* e, KEK_scancode positive, KEK_scancode negative) {
    return (float)kek_key_held(e, positive) - (float)kek_key_held(e, negative);
}

static void DEMO_EntryScene_reset_(DEMO_EntryScene* s) {
    s->base = (KEK_scene){
        .enter = &DEMO_EntryScene_enter,
        .exit = &DEMO_EntryScene_exit,
        .update = &DEMO_EntryScene_update,
        .render = &DEMO_EntryScene_render,
        /* No event handlers: update polls the engine's keyboard state. */
        .key_up = 0,
        .key_down = 0,
        .tag = DEMO_TAG_SCENE_ENTRY
    };
    s->camera = KEK_DEFAULT_CAMERA;
    s->camera_yaw = 0.f;
    s->camera_pitch = 0.f;
    s->elapsed = 0.f;
    s->fps_frames = 0;
    s->fps_seconds = 0.f;
    s->fps = 0.f;
    s->model = KEK_MODEL_HANDLE_INVALID;
    s->texture = KEK_TEXTURE_HANDLE_INVALID;
    s->floor_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_frame_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_texture = KEK_TEXTURE_HANDLE_INVALID;
}

DEMO_EntryScene DEMO_EntryScene_init(void) {
    DEMO_EntryScene result;
    DEMO_EntryScene_reset_(&result);
    return result;
}

void DEMO_EntryScene_load_assets_(DEMO_EntryScene* s, KEK_engine* e) {
    KEK_model* mdl;

    if (!e) {
        return;
    }

    s->model = kek_file_model_load(e, "cat.kmf"); //kek_model_clone(e, &KEK_CUBE_MODEL);
    if (s->model == KEK_MODEL_HANDLE_INVALID) {
        s->model = kek_default_cube_model_handle(e);

        s->texture = kek_file_image_load(e, "test.kif");
        if (s->texture == KEK_TEXTURE_HANDLE_INVALID) {
            return;
        }

        mdl = kek_model_get(e, s->model);
        if (!mdl) {
            return;
        }

        /* The scene keeps ownership — it releases the texture in exit(), and
           the model here is the default cube, which is never destroyed. */
        mdl->texture = s->texture;
    }

    if (!e->assets) {
        return;
    }
} 

/* One upward-facing quad; UVs repeat the built-in brick texture every ten
   world units without adding more geometry. */
static void DEMO_EntryScene_make_floor_(DEMO_EntryScene* s, KEK_engine* e) {
    const float half = DEMO_FLOOR_SIZE * 0.5f;
    const float tiles = DEMO_FLOOR_SIZE / DEMO_FLOOR_TILE;
    const KEK_FVec3 positions[4] = {
        {-half, 0.f, -half}, {-half, 0.f, half},
        {half, 0.f, half}, {half, 0.f, -half}
    };
    KEK_model* floor;

    s->floor_model = kek_model_create(e, 4, 2, 4, KEK_MODEL_FACE_UVS);
    floor = kek_model_get(e, s->floor_model);
    if (!floor) {
        return;
    }
    kek_model_quantise(floor, positions);
    floor->faces[0] = (KEK_model_face){0, 1, 2};
    floor->faces[1] = (KEK_model_face){0, 2, 3};
    floor->uvs[0] = (KEK_FVec2){0.f, 0.f};
    floor->uvs[1] = (KEK_FVec2){0.f, tiles};
    floor->uvs[2] = (KEK_FVec2){tiles, tiles};
    floor->uvs[3] = (KEK_FVec2){tiles, 0.f};
    floor->face_uvs[0] = (KEK_model_face_uv){0, 1, 2};
    floor->face_uvs[1] = (KEK_model_face_uv){0, 2, 3};
    floor->texture = kek_default_texture_handle(e);
}

static void DEMO_EntryScene_draw_floor_(DEMO_EntryScene* s, KEK_engine* e, KEK_camera* camera) {
    KEK_model* floor = kek_model_get(e, s->floor_model);
    if (floor) {
        KEK_TextureWarpMode warp = kek_texture_get_warp_mode(e);
        kek_texture_set_warp_mode(e, KEK_TEXTURE_WARP_REPEAT);
        kek_3d_draw_model(e, floor, camera, DEMO_FLOOR_POSITION,
                          (KEK_FVec3){0.f, 0.f, 0.f});
        kek_texture_set_warp_mode(e, warp);
    }
}

/* A front-facing square. U is reversed because the camera behind the mirror
   looks toward -z, reversing its screen's horizontal axis. */
static void DEMO_EntryScene_make_mirror_(DEMO_EntryScene* s, KEK_engine* e) {
    static const KEK_FVec3 positions[4] = {
        {-1.2f,  1.2f, 0.f}, { 1.2f,  1.2f, 0.f},
        { 1.2f, -1.2f, 0.f}, {-1.2f, -1.2f, 0.f}
    };
    static const KEK_FVec3 frame_positions[4] = {
        {-1.35f,  1.35f, 0.f}, { 1.35f,  1.35f, 0.f},
        { 1.35f, -1.35f, 0.f}, {-1.35f, -1.35f, 0.f}
    };
    KEK_model* mirror;
    KEK_model* frame;

    s->mirror_texture = kek_texture_create(e, DEMO_MIRROR_SIZE, DEMO_MIRROR_SIZE);
    if (s->mirror_texture == KEK_TEXTURE_HANDLE_INVALID) {
        return;
    }
    s->mirror_model = kek_model_create(e, 4, 2, 4, KEK_MODEL_FACE_UVS);
    mirror = kek_model_get(e, s->mirror_model);
    if (!mirror) {
        kek_texture_destroy(e, s->mirror_texture);
        s->mirror_texture = KEK_TEXTURE_HANDLE_INVALID;
        return;
    }

    kek_model_quantise(mirror, positions);
    mirror->faces[0] = (KEK_model_face){1, 2, 3};
    mirror->faces[1] = (KEK_model_face){1, 3, 0};
    mirror->uvs[0] = (KEK_FVec2){1.f, 0.f};
    mirror->uvs[1] = (KEK_FVec2){0.f, 0.f};
    mirror->uvs[2] = (KEK_FVec2){0.f, 1.f};
    mirror->uvs[3] = (KEK_FVec2){1.f, 1.f};
    mirror->face_uvs[0] = (KEK_model_face_uv){1, 2, 3};
    mirror->face_uvs[1] = (KEK_model_face_uv){1, 3, 0};
    mirror->texture = s->mirror_texture;

    s->mirror_frame_model = kek_model_create(e, 4, 2, 0, KEK_MODEL_FACE_COLORS);
    frame = kek_model_get(e, s->mirror_frame_model);
    if (frame) {
        kek_model_quantise(frame, frame_positions);
        frame->faces[0] = (KEK_model_face){1, 2, 3};
        frame->faces[1] = (KEK_model_face){1, 3, 0};
        frame->face_colors[0] = 7;
        frame->face_colors[1] = 7;
    }
}

void DEMO_EntryScene_enter(KEK_scene* scene, KEK_engine* e) {
    DEMO_EntryScene* s = (DEMO_EntryScene*)scene;
    DEMO_EntryScene_reset_(s);
    kek_texture_set_warp_mode(e, KEK_TEXTURE_WARP_CLAMP);
    /* Keep the nearby cat and most of the 50-unit floor clear; distant
       geometry still fades into the sky when the camera moves away. */
    kek_3d_set_fog(e, DEMO_FOG_START, DEMO_FOG_END);
    kek_3d_set_fog_color(e, DEMO_SKY_COLOR);

    DEMO_EntryScene_load_assets_(s, e);
    DEMO_EntryScene_make_floor_(s, e);
    DEMO_EntryScene_make_mirror_(s, e);
}

void DEMO_EntryScene_exit(KEK_scene* scene, KEK_engine* e) {
    DEMO_EntryScene* s = (DEMO_EntryScene*)scene;
    if (s->model != KEK_MODEL_HANDLE_INVALID) {
        kek_model_destroy(e, s->model);
    }
    if (s->texture != KEK_TEXTURE_HANDLE_INVALID) {
        kek_texture_destroy(e, s->texture);
    }
    if (s->floor_model != KEK_MODEL_HANDLE_INVALID) {
        kek_model_destroy(e, s->floor_model);
    }
    if (s->mirror_model != KEK_MODEL_HANDLE_INVALID) {
        kek_model_destroy(e, s->mirror_model);
    }
    if (s->mirror_frame_model != KEK_MODEL_HANDLE_INVALID) {
        kek_model_destroy(e, s->mirror_frame_model);
    }
    if (s->mirror_texture != KEK_TEXTURE_HANDLE_INVALID) {
        kek_texture_destroy(e, s->mirror_texture);
    }
    s->model = KEK_MODEL_HANDLE_INVALID;
    s->texture = KEK_TEXTURE_HANDLE_INVALID;
    s->floor_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_frame_model = KEK_MODEL_HANDLE_INVALID;
    s->mirror_texture = KEK_TEXTURE_HANDLE_INVALID;
}

void DEMO_EntryScene_update(KEK_scene* scene, KEK_engine* e, float dt) {
    DEMO_EntryScene* s = (DEMO_EntryScene*)scene;
    KEK_FVec3 move_vec = {0, 0, 0};
    float move_right;
    float move_up;
    float move_forward;
    float yaw;
    KEK_FVec3 forward;
    KEK_FVec3 right;

    const float move_speed = 1.f;
    const float rotate_speed = 3.1415f / 4.f;
    const float dt_s = dt / 1000.f; /* dt arrives in milliseconds */

    move_right = key_axis(e, KEK_SCANCODE_D, KEK_SCANCODE_A);
    move_up = key_axis(e, KEK_SCANCODE_SPACE, KEK_SCANCODE_LSHIFT);
    move_forward = key_axis(e, KEK_SCANCODE_W, KEK_SCANCODE_S);
    yaw = s->camera_yaw;

    forward = (KEK_FVec3) {
        .x = -sinf(yaw),
        .y = 0.f,
        .z = cosf(yaw)
    };
    right = (KEK_FVec3) {
        .x = cosf(yaw),
        .y = 0.f,
        .z = sinf(yaw)
    };

    move_vec.x = right.x * move_right + forward.x * move_forward;
    move_vec.y = move_up;
    move_vec.z = right.z * move_right + forward.z * move_forward;
    kek_normalize_fvec3(&move_vec);
    kek_mul_fvec3_n(&move_vec, move_speed * dt_s);
    kek_add_fvec3(&s->camera.position, &move_vec);

    {
        KEK_FVec3 turn = {
            key_axis(e, KEK_SCANCODE_UP, KEK_SCANCODE_DOWN),
            key_axis(e, KEK_SCANCODE_LEFT, KEK_SCANCODE_RIGHT),
            0.f
        };
        kek_normalize_fvec3(&turn);
        s->camera_yaw += turn.y * rotate_speed * dt_s;
        s->camera_pitch += turn.x * rotate_speed * dt_s;
    }
    if (s->camera_pitch > 1.55f) s->camera_pitch = 1.55f;
    if (s->camera_pitch < -1.55f) s->camera_pitch = -1.55f;
    s->camera.rotation = DEMO_EntryScene_camera_rotation_(s->camera_yaw, s->camera_pitch);

    s->elapsed += dt_s;

    /* One update per frame on both platforms, so updates counted over time
       are frames per second. dt arrives clamped to KEK_MAX_FRAME_MS, so below
       10 fps this reads high. */
    s->fps_frames += 1;
    s->fps_seconds += dt_s;
    if (s->fps_seconds >= 0.5f) {
        s->fps = (float)s->fps_frames / s->fps_seconds;
        s->fps_frames = 0;
        s->fps_seconds = 0.f;
    }
}

void DEMO_EntryScene_render(KEK_scene* scene, KEK_engine* e) {
    DEMO_EntryScene* s = (DEMO_EntryScene*)scene;
    KEK_model* mdl = kek_model_get(e, s->model);
    KEK_model* mirror = kek_model_get(e, s->mirror_model);
    KEK_model* frame = kek_model_get(e, s->mirror_frame_model);
    KEK_camera reflected = s->camera;
    KEK_light saved_light;
    int mirror_ready = 0;
    /* One turn every ten seconds, shared by the reflection and main view. */
    float rotate = 3.1415f * s->elapsed / 5.f;

    kek_2d_rect(e, (KEK_IVec2){0, 0}, (KEK_IVec2){e->w, e->h}, DEMO_SKY_COLOR);

    if (mdl && mirror && s->camera.position.z < DEMO_MIRROR_Z - 0.1f
        && kek_target_bind(e, s->mirror_texture, KEK_TARGET_CLEAR_COLOR)) {
        reflected.position.z = 2.f * DEMO_MIRROR_Z - s->camera.position.z;
        reflected.rotation = DEMO_EntryScene_camera_rotation_(
            3.14159265f - s->camera_yaw, s->camera_pitch);
        kek_2d_rect(e, (KEK_IVec2){0, 0},
                    (KEK_IVec2){DEMO_MIRROR_SIZE, DEMO_MIRROR_SIZE}, DEMO_SKY_COLOR);
        DEMO_EntryScene_draw_floor_(s, e, &reflected);
        kek_3d_draw_model(e, mdl, &reflected, DEMO_MODEL_POSITION,
                          (KEK_FVec3){0.f, rotate, 0.f});
        kek_target_restore(e);
        mirror_ready = 1;
    }

    DEMO_EntryScene_draw_floor_(s, e, &s->camera);

    for (int i = 0; i < 16; ++i) {
        char kal[8] = {0,};
        (void)snprintf(kal, 7, "%d", i);
        kek_2d_rect(e, (KEK_IVec2) {0, i * 8}, (KEK_IVec2) { 8, (i + 1) * 8 }, i);
        kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) {10, i*8}, kal, 15);
    }

    if (mdl) {
        kek_3d_draw_model(e, mdl, &s->camera, DEMO_MODEL_POSITION,
                          (KEK_FVec3){0.f, rotate, 0.f});
    }

    if (mirror_ready) {
        /* An emitting mirror should not receive the directional light and
           fog a second time after those already shaded its texture. */
        saved_light = e->light;
        kek_3d_set_light(e, saved_light.direction, 1.f);
        kek_3d_set_fog(e, 0.f, 0.f);
        if (frame) {
            KEK_FVec3 frame_position = DEMO_MIRROR_POSITION;
            frame_position.z += 0.1f;
            kek_3d_draw_model(e, frame, &s->camera, frame_position,
                              (KEK_FVec3){0.f, 0.f, 0.f});
        }
        kek_3d_draw_model(e, mirror, &s->camera, DEMO_MIRROR_POSITION,
                          (KEK_FVec3){0.f, 0.f, 0.f});
        e->light = saved_light;
    }

    char buf[128];
    (void)snprintf(
        buf,
        128,
        "x: %.02f\ny: %.02f\nz: %.02f\nfps: %.1f",
        s->camera.position.x,
        s->camera.position.y,
        s->camera.position.z,
        s->fps
    );
    kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) { 30, 8 }, buf, 9);
}
