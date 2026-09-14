/*
SDL3 platform layer. Built as a native executable and, under Emscripten, as a
browser page; SDL's main callbacks are what make both work from one loop —
a browser cannot be blocked in a while(1), so SDL_AppIterate hands control back
after every frame and Emscripten drives it from requestAnimationFrame.
*/
#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stddef.h>
#include <stdint.h>
#include "platform_assets_fs.h"
#include <kek.h>
#include <game.h>

#define APP_WINDOW_WIDTH 1280
#define APP_WINDOW_HEIGHT 800

/* Scenes keep the KEK_engine* they are handed, so the engine has to outlive the
   callback that created it — hence the app state instead of locals in main().
   It is static for the same reason the engine's buffers are: no allocations. */
typedef struct App {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    FS_AssetProvider assets;
    KEK_engine engine;
    uint32_t palette[256]; /* the engine's 18-bit palette, widened to RGBX8888 */
    uint64_t next_frame_ticks;
} App;

static App APP;

static void app_build_palette(App* app) {
    for (int i = 0; i < 256; ++i) {
        struct KEK_palette_channels c = app->engine.palette[i].channels;
        uint8_t r = c.r << 2 | c.r >> 4;
        uint8_t g = c.g << 2 | c.g >> 4;
        uint8_t b = c.b << 2 | c.b >> 4;
        app->palette[i] = (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | 0xFF;
    }
}

static void app_present(App* app) {
    const KEK_engine* e = &app->engine;
    void* pixels;
    int pitch;

    /* Expanding the palette straight into the locked texture keeps the copy
       row-wise, which is what the pitch is for. */
    if (SDL_LockTexture(app->texture, NULL, &pixels, &pitch)) {
        for (uint16_t y = 0; y < e->h; ++y) {
            const uint8_t* src = e->fb + (size_t)y * e->w;
            uint32_t* dst = (uint32_t*)((uint8_t*)pixels + (size_t)y * (size_t)pitch);
            for (uint16_t x = 0; x < e->w; ++x) {
                dst[x] = app->palette[src[x]];
            }
        }
        SDL_UnlockTexture(app->texture);
    }

    SDL_RenderClear(app->renderer);
    SDL_RenderTexture(app->renderer, app->texture, NULL, NULL);
    SDL_RenderPresent(app->renderer);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    App* app = &APP;
#ifdef __EMSCRIPTEN__
    /* The canvas is sized by the page's CSS, and SDL only follows it — picking
       up the layout and device pixel ratio — for a resizable window. */
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#else
    SDL_WindowFlags window_flags = 0;
#endif

    (void)argc;
    (void)argv;
    *appstate = app;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app->engine = kek_init();
    fs_asset_provider_init(&app->assets, "./assets/");
    app->engine.assets = &app->assets.base;
    kek_set_scene(&app->engine, GAME_init_ctx());
    app_build_palette(app);

    if (!SDL_CreateWindowAndRenderer("kek", APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT, window_flags,
                                     &app->window, &app->renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBX8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     app->engine.w, app->engine.h);
    if (!app->texture) {
        SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);
    /* Presenting at framebuffer resolution lets SDL pick the integer upscale and
       letterbox the remainder, whatever the window or canvas ends up being. At
       the default 1280x800 that is an exact 4x, as before. */
    SDL_SetRenderLogicalPresentation(app->renderer, app->engine.w, app->engine.h,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    app->next_frame_ticks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;
    uint64_t ticks = SDL_GetTicks();

    /* kek_update() advances by a fixed 1000/target_fps regardless of real time,
       so the frame gate below is what actually sets the simulation rate. */
    if (ticks < app->next_frame_ticks) {
#ifndef __EMSCRIPTEN__
        /* The browser paces the callback for us; native builds would spin. */
        SDL_Delay(1);
#endif
        return SDL_APP_CONTINUE;
    }
    app->next_frame_ticks = ticks + 1000 / app->engine.target_fps;

    kek_update(&app->engine);
    kek_render(&app->engine);
    app_present(app);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    App* app = (App*)appstate;

    switch (event->type) {
        case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

        case SDL_EVENT_KEY_DOWN:
        kek_key_down(&app->engine, event->key.scancode);
        break;

        case SDL_EVENT_KEY_UP:
        kek_key_up(&app->engine, event->key.scancode);
        break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    App* app = (App*)appstate;

    (void)result;
    if (app && app->texture) {
        SDL_DestroyTexture(app->texture);
        app->texture = NULL;
    }
    SDL_Quit();
}
