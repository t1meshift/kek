#ifndef PLATFORM_DOS_KEYBOARD_H
#define PLATFORM_DOS_KEYBOARD_H

#include <kek.h>

/* Hooks IRQ1 (INT 0x09) in protected mode. The handler queues mapped
   AT scancode-set-1 keys — it does not touch the engine, so nothing engine-
   sized needs to be locked in memory for the interrupt. dos_keyboard_pump()
   drains that queue from the main loop, once a frame before kek_update(),
   and turns it into kek_key_down()/kek_key_up() calls the same way
   SDL_AppEvent does in main_sdl.c.

   Call dos_keyboard_install() once after kek_init(); it returns zero on failure. Call
   dos_keyboard_remove() before returning to DOS — a handler left hooked past
   exit is a hang on the next keypress. */
int dos_keyboard_install(void);
void dos_keyboard_remove(void);
void dos_keyboard_wait_for_escape_release(void);
void dos_keyboard_pump(KEK_engine* engine);

#endif // PLATFORM_DOS_KEYBOARD_H
