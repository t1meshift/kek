#include <kek.h>
#include <kek_macro.h>
#include "demo.h"
#include "demo_tag.h"
#include "demo_scene_loader.h"

KEK_scene* DEMO_scenes_[DEMO_SCENETAG_COUNT] = {0, };

DEMO_Context DEMO_ctx = {
    .scenes = DEMO_scenes_,
    .scenes_count = DEMO_SCENETAG_COUNT
};

KEK_scene* DEMO_init_ctx(void) {
    KEK_STATIC_ASSERT(DEMO_SCENETAG_ENTRY == 0);
    DEMO_init_scene(DEMO_SCENETAG_ENTRY);

    return DEMO_ctx.scenes[DEMO_SCENETAG_ENTRY];
}
