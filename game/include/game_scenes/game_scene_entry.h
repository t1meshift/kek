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
    float elapsed; /* seconds since the scene was entered */
} GAME_EntryScene;

GAME_EntryScene GAME_EntryScene_init(void);

void GAME_EntryScene_enter(KEK_scene* scene, KEK_engine* e);
void GAME_EntryScene_exit(KEK_scene* scene, KEK_engine* e);
void GAME_EntryScene_update(KEK_scene* scene, KEK_engine* e, float dt);
void GAME_EntryScene_render(KEK_scene* scene, KEK_engine* e);

#endif //  GAME_SCENES_GAME_SCENE_ENTRY_H
