#pragma once

#include <libc/stdint.h>

/*
 * keyboard_init - register the PS/2 keyboard IRQ handler
 *
 * Must be called after irq_init() and before sti().
 * Installs a handler on IRQ1 that reads scancodes from port 0x60,
 * translates them to ASCII, and prints them to the terminal.
 */
void keyboard_init(void);

/*
 * keyboard_set_game_mode - switch between text mode and raw scancode mode.
 *
 * mode=1: suppress ASCII output; store raw make-code in scancode buffer.
 * mode=0: normal text output (default after keyboard_init).
 */
void keyboard_set_game_mode(int mode);

/*
 * keyboard_consume_scancode - read and clear the last raw make-code.
 *
 * Returns 0 if no new key has been pressed since the last call.
 * Only meaningful while game mode is active.
 */
uint8_t keyboard_consume_scancode(void);
