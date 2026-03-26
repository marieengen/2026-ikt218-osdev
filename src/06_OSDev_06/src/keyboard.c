#include <keyboard.h>
#include <irq.h>
#include <io.h>
#include <terminal.h>
#include <libc/stdio.h>
#include <libc/stdint.h>

#define KEYBOARD_DATA_PORT 0x60   /* PS/2 keyboard data register */

/*
 * US QWERTY Scancode Set 1 → ASCII lookup table
 *
 * Index = make-code scancode byte (key-press, bit 7 clear).
 * Value = ASCII character to print, or 0 for non-printable keys.
 *
 * Break codes (key-release) have bit 7 set (scancode | 0x80); we skip them.
 * Extended key sequences start with 0xE0 followed by a second byte;
 * scancode_table[0xE0] = 0 so they are silently ignored.
 *
 * Row layout (standard US keyboard):
 *   Esc 1 2 3 4 5 6 7 8 9 0 - =  Backspace
 *   Tab q w e r t y u i o p [ ]  Enter
 *       a s d f g h j k l ; ' `
 *   Shift \ z x c v b n m , . /  Shift
 *   Ctrl  Alt  Space
 */
static const char scancode_table[128] = {
/*00*/  0,
/*01*/  27,    /* Escape                */
/*02*/  '1',
/*03*/  '2',
/*04*/  '3',
/*05*/  '4',
/*06*/  '5',
/*07*/  '6',
/*08*/  '7',
/*09*/  '8',
/*0A*/  '9',
/*0B*/  '0',
/*0C*/  '-',
/*0D*/  '=',
/*0E*/  '\b',  /* Backspace             */
/*0F*/  '\t',  /* Tab                   */
/*10*/  'q',
/*11*/  'w',
/*12*/  'e',
/*13*/  'r',
/*14*/  't',
/*15*/  'y',
/*16*/  'u',
/*17*/  'i',
/*18*/  'o',
/*19*/  'p',
/*1A*/  '[',
/*1B*/  ']',
/*1C*/  '\n',  /* Enter                 */
/*1D*/  0,     /* Left Ctrl             */
/*1E*/  'a',
/*1F*/  's',
/*20*/  'd',
/*21*/  'f',
/*22*/  'g',
/*23*/  'h',
/*24*/  'j',
/*25*/  'k',
/*26*/  'l',
/*27*/  ';',
/*28*/  '\'',
/*29*/  '`',
/*2A*/  0,     /* Left Shift            */
/*2B*/  '\\',
/*2C*/  'z',
/*2D*/  'x',
/*2E*/  'c',
/*2F*/  'v',
/*30*/  'b',
/*31*/  'n',
/*32*/  'm',
/*33*/  ',',
/*34*/  '.',
/*35*/  '/',
/*36*/  0,     /* Right Shift           */
/*37*/  '*',   /* Keypad *              */
/*38*/  0,     /* Left Alt              */
/*39*/  ' ',   /* Space                 */
/*3A*/  0,     /* Caps Lock             */
/*3B*/  0,     /* F1                    */
/*3C*/  0,     /* F2                    */
/*3D*/  0,     /* F3                    */
/*3E*/  0,     /* F4                    */
/*3F*/  0,     /* F5                    */
/*40*/  0,     /* F6                    */
/*41*/  0,     /* F7                    */
/*42*/  0,     /* F8                    */
/*43*/  0,     /* F9                    */
/*44*/  0,     /* F10                   */
/*45*/  0,     /* Num Lock              */
/*46*/  0,     /* Scroll Lock           */
/*47*/  '7',   /* Keypad 7 / Home       */
/*48*/  '8',   /* Keypad 8 / Up         */
/*49*/  '9',   /* Keypad 9 / PgUp       */
/*4A*/  '-',   /* Keypad -              */
/*4B*/  '4',   /* Keypad 4 / Left       */
/*4C*/  '5',   /* Keypad 5              */
/*4D*/  '6',   /* Keypad 6 / Right      */
/*4E*/  '+',   /* Keypad +              */
/*4F*/  '1',   /* Keypad 1 / End        */
/*50*/  '2',   /* Keypad 2 / Down       */
/*51*/  '3',   /* Keypad 3 / PgDn       */
/*52*/  '0',   /* Keypad 0 / Insert     */
/*53*/  '.',   /* Keypad . / Delete     */
/*54*/  0, 0, 0,   /* 0x54-0x56 reserved    */
/*57*/  0,     /* F11                   */
/*58*/  0,     /* F12                   */
/* 0x59-0x7F: extended or reserved; all zero */
    0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0
};

/* 0 = normal text mode, 1 = raw scancode mode (game) */
static volatile int     game_mode    = 0;
static volatile uint8_t last_scancode = 0;

/*
 * keyboard_handler - IRQ1 interrupt service routine
 *
 * Fired by the PS/2 controller each time a key is pressed or released.
 * We MUST read port 0x60 to clear the controller's output buffer,
 * otherwise the keyboard controller will not generate further interrupts.
 */
static void keyboard_handler(registers_t *regs)
{
    (void)regs;  /* register state not needed for keyboard handling */

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /*
     * Bit 7 of the scancode indicates key state:
     *   0 = make (key pressed)  -> handle it
     *   1 = break (key released) -> ignore
     */
    if (scancode & 0x80) {
        return;
    }

    if (game_mode) {
        last_scancode = scancode;
        return;
    }

    char c = scancode_table[scancode];
    if (c == '\b') {
        /* Backspace: erase the last character on screen */
        terminal_backspace();
    } else if (c != 0) {
        putchar(c);
    }
}

void keyboard_init(void)
{
    irq_install_handler(1, keyboard_handler);  /* IRQ1 = PS/2 keyboard */
}

void keyboard_set_game_mode(int mode)
{
    game_mode    = mode;
    last_scancode = 0;
}

uint8_t keyboard_consume_scancode(void)
{
    uint8_t sc   = last_scancode;
    last_scancode = 0;
    return sc;
}
