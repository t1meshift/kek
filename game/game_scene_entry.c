#include <stdio.h>
#include <math.h>
#include <kek_2d.h>
#include <kek_3d.h>
#include <kek_model.h>
#include "game_tag.h"
#include "kek.h"
#include "kek_file_image.h"
#include "kek_file_model.h"
#include "kek_keyboard.h"
#include "kek_math.h"
#include "kek_texture.h"
#include "game_scenes/game_scene_entry.h"

/* +1, -1 or 0 when both or neither are held, as one input axis. */
static float key_axis(const KEK_engine* e, KEK_scancode positive, KEK_scancode negative) {
    return (float)kek_key_held(e, positive) - (float)kek_key_held(e, negative);
}

static void GAME_EntryScene_reset_(GAME_EntryScene* s) {
    s->base = (KEK_scene){
        .enter = &GAME_EntryScene_enter,
        .exit = &GAME_EntryScene_exit,
        .update = &GAME_EntryScene_update,
        .render = &GAME_EntryScene_render,
        /* No event handlers: update polls the engine's keyboard state. */
        .key_up = 0,
        .key_down = 0,
        .tag = GAME_TAG_SCENE_ENTRY
    };
    s->camera = KEK_DEFAULT_CAMERA;
    s->elapsed = 0.f;
    s->model = KEK_MODEL_HANDLE_INVALID;
    s->texture = KEK_TEXTURE_HANDLE_INVALID;
}

GAME_EntryScene GAME_EntryScene_init(void) {
    GAME_EntryScene result;
    GAME_EntryScene_reset_(&result);
    return result;
}

void GAME_EntryScene_load_assets_(GAME_EntryScene* s, KEK_engine* e) {
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

void GAME_EntryScene_enter(KEK_scene* scene, KEK_engine* e) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    GAME_EntryScene_reset_(s);
    kek_texture_set_warp_mode(e, KEK_TEXTURE_WARP_CLAMP);
    /* The cat sits 4.5 away; fog starts past it, so backing off shows it. */
    kek_3d_set_fog(e, 6.f, 25.f);

    GAME_EntryScene_load_assets_(s, e);
}

void GAME_EntryScene_exit(KEK_scene* scene, KEK_engine* e) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    if (s->model != KEK_MODEL_HANDLE_INVALID) {
        kek_model_destroy(e, s->model);
    }
    if (s->texture != KEK_TEXTURE_HANDLE_INVALID) {
        kek_texture_destroy(e, s->texture);
    }
    s->model = KEK_MODEL_HANDLE_INVALID;
    s->texture = KEK_TEXTURE_HANDLE_INVALID;
}

void GAME_EntryScene_update(KEK_scene* scene, KEK_engine* e, float dt) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    KEK_FVec3 move_vec = {0, 0, 0};
    KEK_FVec3 rotate_vec = {0, 0, 0};
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
    yaw = s->camera.rotation.y;

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

    rotate_vec.x = key_axis(e, KEK_SCANCODE_UP, KEK_SCANCODE_DOWN);
    rotate_vec.y = key_axis(e, KEK_SCANCODE_LEFT, KEK_SCANCODE_RIGHT);
    kek_normalize_fvec3(&rotate_vec);
    kek_mul_fvec3_n(&rotate_vec, rotate_speed * dt_s);
    kek_add_fvec3(&s->camera.rotation, &rotate_vec);

    s->elapsed += dt_s;
}

void GAME_EntryScene_render(KEK_scene* scene, KEK_engine* e) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    KEK_model* mdl = kek_model_get(e, s->model);

    for (int i = 0; i < 16; ++i) {
        char kal[8] = {0,};
        (void)snprintf(kal, 7, "%d", i);
        kek_2d_rect(e, (KEK_IVec2) {0, i * 8}, (KEK_IVec2) { 8, (i + 1) * 8 }, i);
        kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) {10, i*8}, kal, 15);
    }

    /* One turn every ten seconds, from accumulated seconds rather than a frame
       count over target_fps — which stopped being time once dt became real. */
    float rotate = 3.1415f * s->elapsed / 5.f;
    
    if (mdl) {
            kek_3d_draw_model(e, mdl, &s->camera, (KEK_FVec3) {
                .x = 0.f,
                .y = -1.f,
                .z = 4.5f
            },  (KEK_FVec3) {
                .x = 0,
                .y = rotate,
                .z = 0
            });
        // for (int i = 0; i < 5; ++i) {
        //     kek_3d_draw_model(e, mdl, &s->camera, (KEK_FVec3) {
        //         .x = (float)i * 1.5f,
        //         .y = (float)i,
        //         .z = (float)(i + 1)*2.5f
        //     },  (KEK_FVec3) {
        //         .x = rotate * (i % 2 ? 1.f : -1.f),
        //         .y = rotate * (i % 2 ? -1.f : 1.f),
        //         .z = 0
        //     });
        // }
    }

    char buf[128];
    (void)snprintf(
        buf,
        128,
        "x: %.02f\ny: %.02f\nz: %.02f",
        s->camera.position.x,
        s->camera.position.y,
        s->camera.position.z
    );
    kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) { 30, 8 }, buf, 9);
}
