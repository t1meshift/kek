#ifndef DEMO_SCENES_DEMO_SCENE_ENTRY_H
#define DEMO_SCENES_DEMO_SCENE_ENTRY_H

#include "kek_model.h"
#include <kek.h>
#include <kek_keyboard.h>
#include <kek_3d.h>
#include <kek_asset.h>

typedef struct DEMO_EntryScene {
    KEK_scene base;
    KEK_camera camera;
    KEK_ModelHandle model;
    KEK_TextureHandle texture;
    float elapsed; /* seconds since the scene was entered */
} DEMO_EntryScene;

DEMO_EntryScene DEMO_EntryScene_init(void);

void DEMO_EntryScene_enter(KEK_scene* scene, KEK_engine* e);
void DEMO_EntryScene_exit(KEK_scene* scene, KEK_engine* e);
void DEMO_EntryScene_update(KEK_scene* scene, KEK_engine* e, float dt);
void DEMO_EntryScene_render(KEK_scene* scene, KEK_engine* e);

#endif //  DEMO_SCENES_DEMO_SCENE_ENTRY_H
