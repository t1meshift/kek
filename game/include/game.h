#ifndef GAME_H
#define GAME_H

#include <kek.h>

typedef struct GAME_Context {
    KEK_scene** scenes;
    uint16_t scenes_count;
} GAME_Context;

extern GAME_Context GAME_ctx;

/**
Initializes global game context, then returns an entry point scene.
*/
KEK_scene* GAME_init_ctx();

#endif // GAME_H