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

typedef enum _GAME_EntryScene_Movement {
    MOVE_NONE    = 0,
    MOVE_UP      = 1 << 0,
    MOVE_DOWN    = 1 << 1,
    MOVE_FORWARD = 1 << 2,
    MOVE_BACK    = 1 << 3,
    MOVE_LEFT    = 1 << 4,
    MOVE_RIGHT   = 1 << 5,
    ROTATE_UP    = 1 << 6,
    ROTATE_DOWN  = 1 << 7,
    ROTATE_LEFT  = 1 << 8,
    ROTATE_RIGHT = 1 << 9
} _GAME_EntryScene_Movement;

#define SET_MOVE(m, dir) do { (m) |= (dir); } while(0)
#define UNSET_MOVE(m, dir) do { (m) &= ~(dir); } while(0)

static void _GAME_EntryScene_reset(GAME_EntryScene* s) {
    s->base = (KEK_scene){
        .enter = &GAME_EntryScene_enter,
        .exit = &GAME_EntryScene_exit,
        .update = &GAME_EntryScene_update,
        .render = &GAME_EntryScene_render,
        .key_up = &GAME_EntryScene_key_up,
        .key_down = &GAME_EntryScene_key_down,
        .tag = GAME_TAG_SCENE_ENTRY
    };
    s->camera = KEK_DEFAULT_CAMERA;
    s->ticks = 0;
    s->movement = MOVE_NONE;
    s->model = KEK_MODEL_HANDLE_INVALID;
    s->texture = KEK_TEXTURE_HANDLE_INVALID;
}

GAME_EntryScene GAME_EntryScene_init() {
    GAME_EntryScene result;
    _GAME_EntryScene_reset(&result);
    return result;
}

void _GAME_EntryScene_load_assets(GAME_EntryScene* s, KEK_engine* e) {
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

        mdl->texture = kek_texture_get(e, s->texture);
    }

    if (!e->assets) {
        return;
    }
} 

void GAME_EntryScene_enter(KEK_scene* scene, KEK_engine* e) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    _GAME_EntryScene_reset(s);
    kek_texture_set_warp_mode(e, KEK_TEXTURE_WARP_CLAMP);

    _GAME_EntryScene_load_assets(s, e);
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
    uint16_t m = s->movement;
    float move_right;
    float move_up;
    float move_forward;
    float yaw;
    KEK_FVec3 forward;
    KEK_FVec3 right;

    const float move_speed = 1.f;
    const float rotate_speed = 3.1415f / 4.f;

    move_right = 1.f * ((m & MOVE_RIGHT) != 0) + -1.f * ((m & MOVE_LEFT) != 0);
    move_up = 1.f * ((m & MOVE_UP) != 0) + -1.f * ((m & MOVE_DOWN) != 0);
    move_forward = 1.f * ((m & MOVE_FORWARD) != 0) + -1.f * ((m & MOVE_BACK) != 0);
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
    kek_mul_fvec3_n(&move_vec, move_speed * dt / 1000.f);
    kek_add_fvec3(&s->camera.position, &move_vec);

    rotate_vec.x = 1.f * ((m & ROTATE_UP) != 0) + -1.f * ((m & ROTATE_DOWN) != 0);
    rotate_vec.y = 1.f * ((m & ROTATE_LEFT) != 0) + -1.f * ((m & ROTATE_RIGHT) != 0);
    kek_normalize_fvec3(&rotate_vec);
    kek_mul_fvec3_n(&rotate_vec, rotate_speed * dt / 1000.f);
    kek_add_fvec3(&s->camera.rotation, &rotate_vec);

    ++s->ticks;
}

void GAME_EntryScene_render(KEK_scene* scene, KEK_engine* e) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;
    uint32_t ticks = s->ticks;
    KEK_model* mdl = kek_model_get(e, s->model);

    KEK_IVec2 triangle[3];
    KEK_IVec2 a = {180, 120};
    KEK_IVec2 b = {150, 135};
    KEK_IVec2 c = {220, 142};
    triangle[0] = a;
    triangle[1] = b;
    triangle[2] = c;

    for (int i = 0; i < 16; ++i) {
        char kal[8] = {0,};
        snprintf(kal, 7, "%d", i);
        kek_2d_rect(e, (KEK_IVec2) {0, i * 8}, (KEK_IVec2) { 8, (i + 1) * 8 }, i);
        kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) {10, i*8}, kal, 15);
    }

    float rotate = 3.1415f * (float)ticks / 5.f / (float)e->target_fps;
    
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
    snprintf(
        buf,
        128,
        "x: %.02f\ny: %.02f\nz: %.02f",
        s->camera.position.x,
        s->camera.position.y,
        s->camera.position.z
    );
    kek_2d_text_5x8(e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2) { 30, 8 }, buf, 9);
}

void GAME_EntryScene_key_up(KEK_scene* scene, KEK_engine* e, KEK_scancode key) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;

    if (key == KEK_SCANCODE_W) {
        UNSET_MOVE(s->movement, MOVE_FORWARD);
    }
    if (key == KEK_SCANCODE_S) {
        UNSET_MOVE(s->movement, MOVE_BACK);
    }
    if (key == KEK_SCANCODE_A) {
        UNSET_MOVE(s->movement, MOVE_LEFT);
    }
    if (key == KEK_SCANCODE_D) {
        UNSET_MOVE(s->movement, MOVE_RIGHT);
    }
    if (key == KEK_SCANCODE_SPACE) {
        UNSET_MOVE(s->movement, MOVE_UP);
    }
    if (key == KEK_SCANCODE_LSHIFT) {
        UNSET_MOVE(s->movement, MOVE_DOWN);
    }
    if (key == KEK_SCANCODE_LEFT) {
        UNSET_MOVE(s->movement, ROTATE_LEFT);
    }
    if (key == KEK_SCANCODE_RIGHT) {
        UNSET_MOVE(s->movement, ROTATE_RIGHT);
    }
    if (key == KEK_SCANCODE_UP) {
        UNSET_MOVE(s->movement, ROTATE_UP);
    }
    if (key == KEK_SCANCODE_DOWN) {
        UNSET_MOVE(s->movement, ROTATE_DOWN);
    }
}

void GAME_EntryScene_key_down(KEK_scene* scene, KEK_engine* e, KEK_scancode key) {
    GAME_EntryScene* s = (GAME_EntryScene*)scene;

    if (key == KEK_SCANCODE_W) {
        SET_MOVE(s->movement, MOVE_FORWARD);
    }
    if (key == KEK_SCANCODE_S) {
        SET_MOVE(s->movement, MOVE_BACK);
    }
    if (key == KEK_SCANCODE_A) {
        SET_MOVE(s->movement, MOVE_LEFT);
    }
    if (key == KEK_SCANCODE_D) {
        SET_MOVE(s->movement, MOVE_RIGHT);
    }
    if (key == KEK_SCANCODE_SPACE) {
        SET_MOVE(s->movement, MOVE_UP);
    }
    if (key == KEK_SCANCODE_LSHIFT) {
        SET_MOVE(s->movement, MOVE_DOWN);
    }
    if (key == KEK_SCANCODE_LEFT) {
        SET_MOVE(s->movement, ROTATE_LEFT);
    }
    if (key == KEK_SCANCODE_RIGHT) {
        SET_MOVE(s->movement, ROTATE_RIGHT);
    }
    if (key == KEK_SCANCODE_UP) {
        SET_MOVE(s->movement, ROTATE_UP);
    }
    if (key == KEK_SCANCODE_DOWN) {
        SET_MOVE(s->movement, ROTATE_DOWN);
    }
}

