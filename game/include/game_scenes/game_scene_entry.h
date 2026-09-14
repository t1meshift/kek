#ifndef GAME_SCENES_GAME_SCENE_ENTRY_H
#define GAME_SCENES_GAME_SCENE_ENTRY_H

#include "kek_model.h"
#include <kek.h>
#include <kek_keyboard.h>
#include <kek_3d.h>
#include <kek_asset.h>

typedef struct GAME_EntryScene {
    KEK_scene base;
    KEK_camera camera;
    KEK_ModelHandle model;
    KEK_TextureHandle texture;
    uint16_t movement;
    uint32_t ticks;
} GAME_EntryScene;

GAME_EntryScene GAME_EntryScene_init();

void GAME_EntryScene_enter(KEK_scene* scene, KEK_engine* e);
void GAME_EntryScene_exit(KEK_scene* scene, KEK_engine* e);
void GAME_EntryScene_update(KEK_scene* scene, KEK_engine* e, float dt);
void GAME_EntryScene_render(KEK_scene* scene, KEK_engine* e);
void GAME_EntryScene_key_up(KEK_scene* scene, KEK_engine* e, KEK_scancode key);
void GAME_EntryScene_key_down(KEK_scene* scene, KEK_engine* e, KEK_scancode key);

#endif //  GAME_SCENES_GAME_SCENE_ENTRY_H
