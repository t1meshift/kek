/* IRQ1 (INT 09h) keyboard handler for the DOS platform layer. Not part of kek: this
   file is meant to be copied and changed the same way demo/platform/ itself
   is (see main_sdl.c's own comment). */
#include "platform_dos_keyboard.h"

#include <dpmi.h>
#include <go32.h>
#include <pc.h>

#define DOS_KB_QUEUE_SIZE 64

typedef struct DOS_KeyEvent {
    uint16_t scancode;
    uint8_t down;
} DOS_KeyEvent;

/* Everything the interrupt handler touches, so dos_keyboard_install() knows
   exactly what to lock: a page fault while servicing INT 09h is a hang, not a
   recoverable error. */
static volatile DOS_KeyEvent dos_kb_queue_[DOS_KB_QUEUE_SIZE];
static volatile unsigned dos_kb_head_ = 0;
static volatile unsigned dos_kb_tail_ = 0;
static volatile int dos_kb_extended_ = 0;
static volatile int dos_kb_escape_down_ = 0;

static _go32_dpmi_seginfo dos_kb_old_handler_;
static _go32_dpmi_seginfo dos_kb_new_handler_;
static int dos_kb_installed_ = 0;

/* AT scancode set 1, make code (bit 7, the break flag, is masked off before
   indexing). Only what the demo's camera controller and Escape-to-quit path
   need are filled in; everything else is KEK_SCANCODE_UNKNOWN and dropped. */
static const uint16_t DOS_SCANCODE_TO_KEK_[128] = {
    [0x01] = KEK_SCANCODE_ESCAPE,
    [0x10] = KEK_SCANCODE_Q,
    [0x11] = KEK_SCANCODE_W,
    [0x12] = KEK_SCANCODE_E,
    [0x1C] = KEK_SCANCODE_RETURN,
    [0x1E] = KEK_SCANCODE_A,
    [0x1F] = KEK_SCANCODE_S,
    [0x20] = KEK_SCANCODE_D,
    [0x2A] = KEK_SCANCODE_LSHIFT,
    [0x36] = KEK_SCANCODE_RSHIFT,
    [0x39] = KEK_SCANCODE_SPACE,
};

/* The 0xE0-prefixed extended set: arrows, matching set 1's actual codes. */
static const uint16_t DOS_SCANCODE_EXT_TO_KEK_[128] = {
    [0x48] = KEK_SCANCODE_UP,
    [0x4B] = KEK_SCANCODE_LEFT,
    [0x4D] = KEK_SCANCODE_RIGHT,
    [0x50] = KEK_SCANCODE_DOWN,
};

static void dos_kb_push_(uint16_t scancode, uint8_t down) {
    unsigned next = (dos_kb_head_ + 1u) % DOS_KB_QUEUE_SIZE;

    if (next == dos_kb_tail_) {
        return; /* full: drop rather than block waiting for the main loop */
    }
    dos_kb_queue_[dos_kb_head_].scancode = scancode;
    dos_kb_queue_[dos_kb_head_].down = down;
    dos_kb_head_ = next;
}

/* The IRET wrapper supplies the interrupt stack and saves registers. Consume
   the scancode here rather than chaining to BIOS: otherwise BIOS also queues
   game keys, which DOS reads as prompt input after the demo exits. */
static void dos_kb_handler_(void) {
    unsigned char code = inportb(0x60);
    unsigned char control = inportb(0x61);
    uint16_t kc;

    outportb(0x61, control | 0x80u);
    outportb(0x61, control);
    outportb(0x20, 0x20); /* acknowledge IRQ1 even for prefix/unknown bytes */

    if (code == 0xE0) {
        dos_kb_extended_ = 1;
        return;
    }

    kc = dos_kb_extended_ ? DOS_SCANCODE_EXT_TO_KEK_[code & 0x7Fu]
                          : DOS_SCANCODE_TO_KEK_[code & 0x7Fu];
    dos_kb_extended_ = 0;
    if (kc == KEK_SCANCODE_ESCAPE) {
        dos_kb_escape_down_ = (code & 0x80u) == 0;
    }
    if (kc != KEK_SCANCODE_UNKNOWN) {
        dos_kb_push_(kc, (uint8_t)((code & 0x80u) == 0));
    }
}

int dos_keyboard_install(void) {
    /* A function pointer has no portable conversion to void*; a union is the
       usual way to hand one to an API that (like this one) wants an address
       rather than a call, without a -Wpedantic complaint at the cast. */
    union { void (*fn)(void); void* obj; } handler_addr;

    if (dos_kb_installed_) {
        return 1;
    }
    dos_kb_head_ = 0;
    dos_kb_tail_ = 0;
    dos_kb_extended_ = 0;
    dos_kb_escape_down_ = 0;

    _go32_dpmi_lock_data((void*)dos_kb_queue_, sizeof(dos_kb_queue_));
    _go32_dpmi_lock_data((void*)&dos_kb_head_, sizeof(dos_kb_head_));
    _go32_dpmi_lock_data((void*)&dos_kb_tail_, sizeof(dos_kb_tail_));
    _go32_dpmi_lock_data((void*)&dos_kb_extended_, sizeof(dos_kb_extended_));
    _go32_dpmi_lock_data((void*)&dos_kb_escape_down_, sizeof(dos_kb_escape_down_));
    _go32_dpmi_lock_data((void*)DOS_SCANCODE_TO_KEK_, sizeof(DOS_SCANCODE_TO_KEK_));
    _go32_dpmi_lock_data((void*)DOS_SCANCODE_EXT_TO_KEK_, sizeof(DOS_SCANCODE_EXT_TO_KEK_));
    /* The handler compiles to well under this; a fixed generous size is the
       usual way to lock a short ISR without a linker section to size it by. */
    handler_addr.fn = dos_kb_handler_;
    _go32_dpmi_lock_code(handler_addr.obj, 4096);

    dos_kb_new_handler_.pm_offset = (int)dos_kb_handler_;
    dos_kb_new_handler_.pm_selector = _go32_my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&dos_kb_new_handler_) != 0) {
        return 0;
    }
    if (_go32_dpmi_get_protected_mode_interrupt_vector(9, &dos_kb_old_handler_) != 0) {
        _go32_dpmi_free_iret_wrapper(&dos_kb_new_handler_);
        return 0;
    }
    if (_go32_dpmi_set_protected_mode_interrupt_vector(9, &dos_kb_new_handler_) != 0) {
        _go32_dpmi_free_iret_wrapper(&dos_kb_new_handler_);
        return 0;
    }
    dos_kb_installed_ = 1;
    return 1;
}

void dos_keyboard_remove(void) {
    if (!dos_kb_installed_) {
        return;
    }
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &dos_kb_old_handler_);
    _go32_dpmi_free_iret_wrapper(&dos_kb_new_handler_);
    dos_kb_installed_ = 0;
}

void dos_keyboard_wait_for_escape_release(void) {
    while (dos_kb_escape_down_) {
        /* IRQ1 updates the flag even if the event queue is full. */
    }
}

void dos_keyboard_pump(KEK_engine* engine) {
    while (dos_kb_tail_ != dos_kb_head_) {
        DOS_KeyEvent ev = dos_kb_queue_[dos_kb_tail_];
        dos_kb_tail_ = (dos_kb_tail_ + 1u) % DOS_KB_QUEUE_SIZE;
        if (ev.down) {
            kek_key_down(engine, ev.scancode);
        } else {
            kek_key_up(engine, ev.scancode);
        }
    }
}
