#include "game_scene_loader.h"
#include "game.h"
#include "game_tag.h"
#include "game_scenes/game_scene_entry.h"
#include "kek.h"

GAME_EntryScene GAME_scene_entry_;

void GAME_init_scene(GAME_SceneTag tag) {
    switch (tag) {
        case GAME_SCENETAG_ENTRY:
        GAME_scene_entry_ = GAME_EntryScene_init();
        GAME_ctx.scenes[GAME_SCENETAG_ENTRY] = (KEK_scene*)&GAME_scene_entry_;
        break;

        default:
        break;
    }
}
