#pragma once

#include <libc/stdint.h>
#include <libc/stddef.h>

/*
 * Standard 16-colour VGA palette.
 * The value is the 4-bit colour index used in the VGA attribute byte.
 */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

/* Clear the screen and reset cursor to (0,0). */
void terminal_init(void);

/* Write a single character at the current cursor position. */
void terminal_putchar(char c);

/* Write a null-terminated string at the current cursor position. */
void terminal_write(const char* str);

/* Write a null-terminated string using the given foreground/background colours. */
void terminal_writecolor(const char* str, vga_color_t fg, vga_color_t bg);

/* Set the active foreground and background colour for subsequent output. */
void terminal_setcolor(vga_color_t fg, vga_color_t bg);
