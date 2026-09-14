#include "game_scene_loader.h"
#include "game.h"
#include "game_tag.h"
#include "game_scenes/game_scene_entry.h"
#include "kek.h"

GAME_EntryScene _GAME_scene_entry;

void GAME_init_scene(GAME_SceneTag tag) {
    switch (tag) {
        case GAME_SCENETAG_ENTRY:
        _GAME_scene_entry = GAME_EntryScene_init();
        GAME_ctx.scenes[GAME_SCENETAG_ENTRY] = (KEK_scene*)&_GAME_scene_entry;
        break;

        default:
        break;
    }
}