#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform_assets_fs.h"
#include <kek.h>
#include <kek_macro.h>
#include <game.h>

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window;
    SDL_Renderer* renderer;
    FS_AssetProvider assets_provider;

    KEK_engine e = kek_init();
    fs_asset_provider_init(&assets_provider, "./assets/");
    e.assets = &assets_provider.base;
    KEK_scene* start_scene = GAME_init_ctx();
    kek_set_scene(&e, start_scene);

    KEK_palette_item* palette18 = e.palette;
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) {
        struct KEK_palette_channels c = palette18[i].channels;
        uint8_t r = c.r << 2 | c.r >> 4;
        uint8_t g = c.g << 2 | c.g >> 4;
        uint8_t b = c.b << 2 | c.b >> 4;
        palette[i] = r << 24 | g << 16 | b << 8 | 0xFF;
    }

    int w = e.w;
    int h = e.h;

    bool window_result = SDL_CreateWindowAndRenderer("kek", 1280, 800, 0, &window, &renderer);
    if (!window_result) {
        const char* error = SDL_GetError();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", error, NULL);
        return 1;
    }

    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBX8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    uint32_t* fb = malloc(sizeof(uint32_t) * w * h);

    SDL_SetRenderLogicalPresentation(renderer, 1280, 800, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    uint64_t last_ticks = SDL_GetTicks();
    SDL_Event ev;
    bool quit = false;
    while (!quit) {
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                quit = true;
                break;

                case SDL_EVENT_KEY_DOWN:
                kek_key_down(&e, ev.key.scancode);
                break;

                case SDL_EVENT_KEY_UP:
                kek_key_up(&e, ev.key.scancode);
                break;
            }
        }

        uint64_t ticks = SDL_GetTicks();
        int dt = ticks - last_ticks;
        int delay_time = KEK_MAX(1000/e.target_fps - dt, 0);
        kek_update(&e);

        kek_render(&e);

        // Get new framebuffer data, convert it to RGBA8888
        unsigned char* fb8 = e.fb;
        for (int i = 0; i < w * h; ++i) {
            fb[i] = palette[fb8[i]];
        }

        // Write it to texture
        void* pixels;
        int pitch;
        SDL_LockTexture(tex, NULL, &pixels, &pitch);
        memcpy(pixels, fb, h * pitch);
        SDL_UnlockTexture(tex);

        // Render the texture
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay( delay_time);
        last_ticks = ticks;
    }


    SDL_DestroyTexture(tex);
    free(fb);
    SDL_Quit();
}
