/* The demo's DOS platform layer: VGA mode 13h through DJGPP, on real or
   emulated 486/Pentium hardware. Not part of kek, and not part of the SDL3
   layer either — see main_sdl.c's own comment on why a game copies
   demo/platform/ rather than linking against it; this file follows the same
   rule.

   Mode 13h is exactly the engine's frame: 320x200, one byte a pixel, indexed
   into a 256-colour palette in the VGA DAC's own 0-63 range. Presenting is a
   straight memcpy to 0xA0000 through DJGPP's near-pointer window, and the
   palette is pushed to it once through ports 0x3C8/0x3C9 with no widening,
   unlike the SDL backend's RGBX8888 expansion. */
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nearptr.h>
#include <time.h>

#include "../platform_assets_fs.h"
#include "platform_dos_keyboard.h"
#include <demo.h>
#include <kek.h>

#define APP_FRAME_WIDTH 320
#define APP_FRAME_HEIGHT 200
#define VGA_MODE_13H 0x13
#define VGA_MODE_TEXT_80X25 0x03

typedef struct App {
    FS_AssetProvider assets;
    KEK_engine engine;
    uint8_t* vga_fb; /* 0xA0000, through the near-pointer window */
    int graphics_mode_set;
    int keyboard_installed;
    int nearptr_enabled;
} App;

static App APP;
#define APP_ASSET_BUDGET ((size_t)256u * 1024u)
static unsigned char APP_ENGINE_MEMORY[KEK_MEMORY_SIZE(APP_FRAME_WIDTH, APP_FRAME_HEIGHT) + APP_ASSET_BUDGET];

static void app_set_video_mode_(uint8_t mode) {
    __dpmi_regs regs;

    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x00;
    regs.h.al = mode;
    __dpmi_int(0x10, &regs);
}

static void app_push_palette_(const App* app) {
    int i;

    outportb(0x3C8, 0);
    for (i = 0; i < 256; ++i) {
        const KEK_palette_item* c = &app->engine.palette[i];
        outportb(0x3C9, c->r);
        outportb(0x3C9, c->g);
        outportb(0x3C9, c->b);
    }
}

static void app_present_(App* app) {
    const KEK_engine* e = &app->engine;
    /* fb is already packed 320x200 bytes, and mode 13h's plane is that
       exact layout: no per-row pitch to account for, unlike a windowed
       framebuffer. */
    memcpy(app->vga_fb, e->fb, (size_t)e->w * e->h);
}

/* Runs however app_init() fails or SDL_AppQuit's DOS equivalent (falling out
   of main()) exits: text mode and IRQ9 are DOS-wide state, not this
   process's, so leaving either behind is a hung keyboard or a garbled
   screen for whatever runs next. */
static void app_shutdown_(void) {
    if (APP.keyboard_installed) {
        dos_keyboard_remove();
        APP.keyboard_installed = 0;
    }
    if (APP.nearptr_enabled) {
        __djgpp_nearptr_disable();
        APP.nearptr_enabled = 0;
    }
    if (APP.graphics_mode_set) {
        app_set_video_mode_(VGA_MODE_TEXT_80X25);
        APP.graphics_mode_set = 0;
    }
}

int main(void) {
    App* app = &APP;
    uclock_t next_frame_ticks;
    uclock_t last_update_ticks;

    if (!kek_init(&app->engine, &(KEK_desc){ .width = APP_FRAME_WIDTH, .height = APP_FRAME_HEIGHT },
                  APP_ENGINE_MEMORY, sizeof(APP_ENGINE_MEMORY))) {
        return 1;
    }
    fs_asset_provider_init(&app->assets, "./assets/");
    app->engine.assets = &app->assets.base;
    kek_set_scene(&app->engine, DEMO_init_ctx());

    atexit(app_shutdown_);

    if (!__djgpp_nearptr_enable()) {
        return 1;
    }
    app->nearptr_enabled = 1;
    app->vga_fb = (uint8_t*)(0xA0000 + __djgpp_conventional_base);

    app_set_video_mode_(VGA_MODE_13H);
    app->graphics_mode_set = 1;
    app_push_palette_(app);

    dos_keyboard_install();
    app->keyboard_installed = 1;

    next_frame_ticks = uclock();
    last_update_ticks = next_frame_ticks;
    for (;;) {
        uclock_t ticks = uclock();
        float dt;

        if (ticks < next_frame_ticks) {
            continue; /* DOS is single-tasking: spin rather than sleep */
        }
        next_frame_ticks = ticks + UCLOCKS_PER_SEC / app->engine.target_fps;

        dt = (float)(ticks - last_update_ticks) * 1000.f / (float)UCLOCKS_PER_SEC;
        last_update_ticks = ticks;

        dos_keyboard_pump(&app->engine);
        if (kek_key_held(&app->engine, KEK_SCANCODE_ESCAPE)) {
            break;
        }

        kek_update(&app->engine, dt);
        kek_render(&app->engine);
        app_present_(app);
    }

    return 0;
}
