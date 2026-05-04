#include <snake.h>
#include <keyboard.h>
#include <pit.h>
#include <io.h>
#include <libc/stdint.h>

/* ------------------------------------------------------------------ *
 * VGA text-mode direct access (80x25, 2 bytes per cell)              *
 * ------------------------------------------------------------------ */
#define VGA_BASE   ((volatile uint16_t *)0xB8000)
#define VGA_W      80
#define VGA_H      25

/* Color nibbles: (bg << 4) | fg */
#define C_BLACK        0x0
#define C_GREEN        0x2
#define C_RED          0x4
#define C_DARK_GREY    0x8
#define C_LIGHT_GREEN  0xA
#define C_LIGHT_CYAN   0xB
#define C_YELLOW       0xE
#define C_WHITE        0xF

#define ATTR(bg, fg)   (uint8_t)(((bg) << 4) | (fg))

/* ------------------------------------------------------------------ *
 * Game area                                                           *
 * ------------------------------------------------------------------ */
#define BORDER_ATTR  ATTR(C_LIGHT_CYAN, C_WHITE)
#define BORDER_CHAR  '#'

/* Play-field: inside the border */
#define GX1  1
#define GY1  1
#define GX2  78
#define GY2  22
#define GW   (GX2 - GX1 + 1)   /* 78 columns */
#define GH   (GY2 - GY1 + 1)   /* 22 rows    */

/* Status bar at row 24 */
#define STATUS_ROW  24

/* ------------------------------------------------------------------ *
 * WASD make-code scancodes (Scancode Set 1)                          *
 * ------------------------------------------------------------------ */
#define SC_W   0x11
#define SC_A   0x1E
#define SC_S   0x1F
#define SC_D   0x20
#define SC_R   0x13   /* restart after game over */
#define SC_ESC 0x01   /* exit to main menu */

/* ------------------------------------------------------------------ *
 * Snake state                                                         *
 * ------------------------------------------------------------------ */
#define MAX_LEN  (GW * GH)   /* theoretical maximum */

typedef struct { int x; int y; } Point;

static Point  snake[MAX_LEN];
static int    snake_len;
static int    dir_x, dir_y;   /* current direction */
static int    next_dx, next_dy; /* queued direction (set on keypress) */
static Point  food;
static int    score;
static int    speed_ms;        /* milliseconds per game step */

/* ------------------------------------------------------------------ *
 * LCG random-number generator, seeded from the PIT tick counter      *
 * ------------------------------------------------------------------ */
static uint32_t rng;

static uint32_t rand_next(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

/* ------------------------------------------------------------------ *
 * VGA helpers                                                         *
 * ------------------------------------------------------------------ */
static void vga_put(int x, int y, char c, uint8_t attr)
{
    VGA_BASE[y * VGA_W + x] = (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

static void vga_puts(int x, int y, const char *s, uint8_t attr)
{
    while (*s) vga_put(x++, y, *s++, attr);
}

static void vga_fill(int x1, int y1, int x2, int y2, char c, uint8_t attr)
{
    for (int row = y1; row <= y2; row++)
        for (int col = x1; col <= x2; col++)
            vga_put(col, row, c, attr);
}

/* Print an unsigned decimal number; returns width printed */
static int vga_putu(int x, int y, uint32_t n, uint8_t attr)
{
    char buf[12];
    int  i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else        { while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; } }
    /* reverse */
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
    for (int k = 0; k < i; k++) vga_put(x + k, y, buf[k], attr);
    return i;
}

/* ------------------------------------------------------------------ *
 * PC speaker beep (PIT channel 2)                                    *
 * ------------------------------------------------------------------ */
static void beep(uint32_t freq_hz, uint32_t duration_ms)
{
    uint32_t divisor = 1193180u / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    uint8_t v = inb(0x61);
    outb(0x61, v | 0x03);
    sleep_interrupt(duration_ms);
    outb(0x61, (uint8_t)(inb(0x61) & ~0x02));
}

/* ------------------------------------------------------------------ *
 * Drawing                                                             *
 * ------------------------------------------------------------------ */
static void draw_border(void)
{
    /* Top and bottom rows */
    for (int x = 0; x < VGA_W; x++) {
        vga_put(x, 0,        BORDER_CHAR, BORDER_ATTR);
        vga_put(x, GY2 + 1, BORDER_CHAR, BORDER_ATTR);
    }
    /* Left and right columns */
    for (int y = 0; y <= GY2 + 1; y++) {
        vga_put(0,        y, BORDER_CHAR, BORDER_ATTR);
        vga_put(VGA_W-1, y, BORDER_CHAR, BORDER_ATTR);
    }
    /* Title centred in top border */
    vga_puts(35, 0, " SNAKE ", ATTR(C_LIGHT_GREEN, C_BLACK));
}

static void draw_status(void)
{
    uint8_t a = ATTR(C_BLACK, C_YELLOW);
    vga_fill(0, STATUS_ROW, VGA_W - 1, STATUS_ROW, ' ', ATTR(C_BLACK, C_BLACK));
    vga_puts(1,  STATUS_ROW, "Score: ", a);
    int w = vga_putu(8, STATUS_ROW, (uint32_t)score, a);
    vga_puts(8 + w + 2, STATUS_ROW, "WASD=Move", a);
    vga_puts(30, STATUS_ROW, "Speed: ", a);
    vga_putu(37, STATUS_ROW, (uint32_t)speed_ms, a);
    vga_puts(41, STATUS_ROW, "ms/step", a);
    vga_puts(55, STATUS_ROW, "R=Restart  ESC=Menu", a);
}

static void draw_food(void)
{
    vga_put(food.x, food.y, '*', ATTR(C_BLACK, C_RED));
}

static void erase_food(void)
{
    vga_put(food.x, food.y, ' ', ATTR(C_BLACK, C_BLACK));
}

static void draw_snake_head(void)
{
    vga_put(snake[0].x, snake[0].y, '@', ATTR(C_BLACK, C_LIGHT_GREEN));
}

static void draw_snake_body(int i)
{
    vga_put(snake[i].x, snake[i].y, 'o', ATTR(C_BLACK, C_GREEN));
}

static void erase_cell(int x, int y)
{
    vga_put(x, y, ' ', ATTR(C_BLACK, C_BLACK));
}

/* ------------------------------------------------------------------ *
 * Food spawning                                                       *
 * ------------------------------------------------------------------ */
static int on_snake(int x, int y)
{
    for (int i = 0; i < snake_len; i++)
        if (snake[i].x == x && snake[i].y == y) return 1;
    return 0;
}

static void spawn_food(void)
{
    do {
        food.x = GX1 + (int)(rand_next() % (uint32_t)GW);
        food.y = GY1 + (int)(rand_next() % (uint32_t)GH);
    } while (on_snake(food.x, food.y));
    draw_food();
}

/* ------------------------------------------------------------------ *
 * Game-over overlay                                                   *
 * ------------------------------------------------------------------ */
static void draw_game_over(void)
{
    uint8_t a = ATTR(C_RED, C_WHITE);
    int cx = VGA_W / 2;
    vga_fill(cx - 11, 10, cx + 11, 15, ' ', a);
    vga_puts(cx - 5, 11, "GAME OVER", a);
    vga_puts(cx - 7, 12, "Score: ", a);
    vga_putu(cx, 12, (uint32_t)score, a);
    vga_puts(cx - 11, 13, "R = Restart", a);
    vga_puts(cx - 11, 14, "ESC = Main Menu", a);
}

/* ------------------------------------------------------------------ *
 * One game session                                                    *
 * ------------------------------------------------------------------ */
static int play_one_game(void)
{
    /* Clear play field */
    vga_fill(GX1, GY1, GX2, GY2, ' ', ATTR(C_BLACK, C_BLACK));

    score    = 0;
    speed_ms = 250;

    /* Initial snake: length 3, centre of field, moving right */
    snake_len = 3;
    int cx = GX1 + GW / 2;
    int cy = GY1 + GH / 2;
    snake[0] = (Point){cx,     cy};
    snake[1] = (Point){cx - 1, cy};
    snake[2] = (Point){cx - 2, cy};
    dir_x  = 1; dir_y  = 0;
    next_dx = 1; next_dy = 0;

    draw_snake_head();
    for (int i = 1; i < snake_len; i++) draw_snake_body(i);

    spawn_food();
    draw_status();

    uint32_t last_move = get_tick_count();

    while (1) {
        /* Process one queued keypress */
        uint8_t sc = keyboard_consume_scancode();
        if (sc != 0) {
            if (sc == SC_W && dir_y !=  1) { next_dx =  0; next_dy = -1; }
            if (sc == SC_S && dir_y != -1) { next_dx =  0; next_dy =  1; }
            if (sc == SC_A && dir_x !=  1) { next_dx = -1; next_dy =  0; }
            if (sc == SC_D && dir_x != -1) { next_dx =  1; next_dy =  0; }
            if (sc == SC_R)   return 1;  /* early restart */
            if (sc == SC_ESC) return 0;  /* back to main menu */
        }

        /* Wait until next step interval.
         * VGA cells are 9x16px, so vertical steps cover ~1.78x more screen
         * distance. Compensate by making vertical moves take longer. */
        uint32_t step_ms = (next_dy != 0) ? (uint32_t)(speed_ms * 16 / 9)
                                          : (uint32_t)speed_ms;
        if (get_tick_count() - last_move < step_ms) {
            __asm__ volatile("hlt");
            continue;
        }
        last_move += step_ms;

        /* Commit queued direction */
        dir_x = next_dx;
        dir_y = next_dy;

        /* New head position */
        int nx = snake[0].x + dir_x;
        int ny = snake[0].y + dir_y;

        /* Wall collision */
        if (nx < GX1 || nx > GX2 || ny < GY1 || ny > GY2)
            break;

        /* Self collision (skip the tail cell — it will vacate this tick) */
        int hit = 0;
        for (int i = 0; i < snake_len - 1; i++) {
            if (snake[i].x == nx && snake[i].y == ny) { hit = 1; break; }
        }
        if (hit) break;

        /* Did we eat food? */
        int ate = (nx == food.x && ny == food.y);

        if (ate) {
            erase_food();
            if (snake_len < MAX_LEN) snake_len++;
        } else {
            /* Erase tail before it moves */
            erase_cell(snake[snake_len - 1].x, snake[snake_len - 1].y);
        }

        /* Shift body */
        for (int i = snake_len - 1; i > 0; i--)
            snake[i] = snake[i - 1];
        snake[0] = (Point){nx, ny};

        /* Redraw: new head, old head becomes body */
        draw_snake_head();
        if (snake_len > 1) draw_snake_body(1);

        if (ate) {
            score++;
            if (speed_ms > 60) speed_ms -= 5;
            beep(880, 40);
            spawn_food();
            draw_status();
        }
    }

    /* Death */
    beep(220, 150);
    beep(165, 300);
    draw_game_over();

    /* Wait for R or ESC */
    while (1) {
        uint8_t sc = keyboard_consume_scancode();
        if (sc == SC_R)   return 1;
        if (sc == SC_ESC) return 0;
        __asm__ volatile("hlt");
    }
}

/* ------------------------------------------------------------------ *
 * Public entry point                                                  *
 * ------------------------------------------------------------------ */
void run_snake(void)
{
    rng = get_tick_count();

    keyboard_set_game_mode(1);

    /* Clear the whole screen */
    vga_fill(0, 0, VGA_W - 1, VGA_H - 1, ' ', ATTR(C_BLACK, C_BLACK));

    draw_border();

    /* "Press any key" splash */
    uint8_t sa = ATTR(C_BLACK, C_WHITE);
    vga_puts(24, 11, "  Welcome to SNAKE!  ", ATTR(C_GREEN, C_LIGHT_GREEN));
    vga_puts(24, 12, "  WASD to move       ", sa);
    vga_puts(24, 13, "  Eat * to grow      ", sa);
    vga_puts(24, 14, "  Avoid walls & self ", sa);
    vga_puts(24, 15, "  ESC = Main Menu    ", sa);
    vga_puts(24, 16, "  Press any key...   ", sa);

    /* Drain old scancodes, then wait for a fresh one */
    keyboard_consume_scancode();
    while (keyboard_consume_scancode() == 0)
        __asm__ volatile("hlt");

    /* Erase splash */
    vga_fill(GX1, GY1, GX2, GY2, ' ', ATTR(C_BLACK, C_BLACK));

    /* Game loop: play_one_game returns 1 to restart, 0 to exit to menu */
    while (play_one_game())
        ;

    keyboard_set_game_mode(0);
}
