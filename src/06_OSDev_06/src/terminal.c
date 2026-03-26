#include <terminal.h>

/*
 * VGA text-mode driver
 *
 * The VGA text buffer is memory-mapped at physical address 0xB8000.
 * It is a flat array of 16-bit cells, one per character position.
 *
 * Each 16-bit cell layout:
 *   Bits  7:0  – ASCII character code
 *   Bits 11:8  – Foreground colour (4-bit VGA palette index)
 *   Bits 15:12 – Background colour (4-bit VGA palette index)
 *
 * The screen is 80 columns × 25 rows = 2000 cells.
 * Cell index for (row, col) = row * VGA_WIDTH + col.
 */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* Physical address of the VGA text buffer */
#define VGA_MEMORY ((uint16_t*)0xB8000)

/* Current cursor position and colour state */
static size_t   terminal_row;
static size_t   terminal_col;
static uint8_t  terminal_color;
static uint16_t *terminal_buffer;

/* Pack foreground + background into the attribute byte */
static inline uint8_t make_color(vga_color_t fg, vga_color_t bg)
{
    return (uint8_t)((uint8_t)fg | ((uint8_t)bg << 4));
}

/* Pack a character and its colour attribute into a VGA cell word */
static inline uint16_t make_vga_entry(char c, uint8_t color)
{
    return (uint16_t)((uint8_t)c) | ((uint16_t)color << 8);
}

void terminal_init(void)
{
    terminal_row    = 0;
    terminal_col    = 0;
    terminal_color  = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;

    /* Fill every cell with a blank in the default colour */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] =
                make_vga_entry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(vga_color_t fg, vga_color_t bg)
{
    terminal_color = make_color(fg, bg);
}

/*
 * terminal_scroll - move every row up by one and clear the last row.
 * Called when the cursor would move past row 24.
 */
static void terminal_scroll(void)
{
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[(y - 1) * VGA_WIDTH + x] =
                terminal_buffer[y * VGA_WIDTH + x];
        }
    }
    /* Clear the newly exposed bottom row */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            make_vga_entry(' ', terminal_color);
    }
    terminal_row = VGA_HEIGHT - 1;
}

void terminal_putchar(char c)
{
    if (c == '\n') {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
        }
        return;
    }

    if (c == '\r') {
        terminal_col = 0;
        return;
    }

    terminal_buffer[terminal_row * VGA_WIDTH + terminal_col] =
        make_vga_entry(c, terminal_color);

    terminal_col++;
    if (terminal_col >= VGA_WIDTH) {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
        }
    }
}

void terminal_write(const char* str)
{
    for (size_t i = 0; str[i] != '\0'; i++) {
        terminal_putchar(str[i]);
    }
}

void terminal_writecolor(const char* str, vga_color_t fg, vga_color_t bg)
{
    uint8_t saved = terminal_color;
    terminal_color = make_color(fg, bg);
    terminal_write(str);
    terminal_color = saved;
}
