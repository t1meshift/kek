#include <kek.h>
#include <kek_macro.h>
#include "game.h"
#include "game_tag.h"
#include "game_scene_loader.h"

KEK_scene* _GAME_scenes[GAME_SCENETAG_COUNT] = {0, };

GAME_Context GAME_ctx = {
    .scenes = _GAME_scenes,
    .scenes_count = GAME_SCENETAG_COUNT
};

KEK_scene* GAME_init_ctx() {
    KEK_STATIC_ASSERT(GAME_SCENETAG_ENTRY == 0);
    GAME_init_scene(GAME_SCENETAG_ENTRY);

    return GAME_ctx.scenes[GAME_SCENETAG_ENTRY];
}