#include "demo_scene_loader.h"
#include "demo.h"
#include "demo_tag.h"
#include "demo_scenes/demo_scene_entry.h"
#include "kek.h"

DEMO_EntryScene DEMO_scene_entry_;

void DEMO_init_scene(DEMO_SceneTag tag) {
    switch (tag) {
        case DEMO_SCENETAG_ENTRY:
        DEMO_scene_entry_ = DEMO_EntryScene_init();
        DEMO_ctx.scenes[DEMO_SCENETAG_ENTRY] = (KEK_scene*)&DEMO_scene_entry_;
        break;

        default:
        break;
    }
}
