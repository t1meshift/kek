#ifndef DEMO_H
#define DEMO_H

#include <kek.h>

typedef struct DEMO_Context {
    KEK_scene** scenes;
    uint16_t scenes_count;
} DEMO_Context;

extern DEMO_Context DEMO_ctx;

/**
Initializes global demo context, then returns an entry point scene.
*/
KEK_scene* DEMO_init_ctx(void);

#endif // DEMO_H
