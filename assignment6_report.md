# Assignment 6 Report - Snake Game

**Course:** IKT218 - Operating System Development
**Group:** 06_OSDev_06
**Assignment:** 6 - Snake Game

---

## Overview

This assignment implements a playable Snake game running directly on the bare-metal OS, using the VGA text-mode display, PS/2 keyboard input, and the PIT tick counter from assignment 4. No OS services, no standard library.

---

## Architecture

### Direct VGA Rendering

Instead of using the terminal driver (which scrolls and manages a cursor), the Snake game writes directly to the VGA frame buffer at physical address `0xB8000`. Each cell is a 16-bit word:

```
bits 15:8  = attribute byte (bg color nibble | fg color nibble)
bits  7:0  = ASCII character
```

Writing individual cells with `ATTR(bg, fg)` macros gives full control over color without side effects from the terminal driver.

### Game Area Layout

```
Row  0:      Border (top)         "#####...SNAKE...####"
Rows 1-22:   Play field (78x22)
Row 23:      Border (bottom)
Row 24:      Status bar (score, speed, controls)
```

Columns 0 and 79 are the left/right border. The play field is 78x22 = 1716 cells.

### Keyboard: Game Mode

The keyboard driver was extended with two new functions:

```c
void    keyboard_set_game_mode(int mode);
uint8_t keyboard_consume_scancode(void);
```

When game mode is active (`mode=1`), the IRQ1 handler stores the raw make-code in a single-byte buffer instead of translating it to ASCII and printing it. `keyboard_consume_scancode()` atomically reads and clears that buffer.

WASD scancodes (Scancode Set 1):

| Key | Scancode |
|-----|----------|
| W   | 0x11     |
| A   | 0x1E     |
| S   | 0x1F     |
| D   | 0x20     |
| R   | 0x13     |

Direction changes are queued and applied at the start of each game step. A 180-degree reversal is ignored (you cannot immediately reverse into yourself).

### Game Loop

```
while (1):
    sc = keyboard_consume_scancode()
    update queued direction from sc
    if get_tick_count() - last_move < speed_ms:
        hlt           // wait for next PIT IRQ0 tick
        continue
    last_move = get_tick_count()
    commit queued direction
    compute new head position
    check wall collision -> game over
    check self collision -> game over
    check food collision -> grow + score + beep + respawn food
    shift body array, update head
    redraw changed cells only
```

`hlt` suspends the CPU until the next interrupt (PIT IRQ0 fires every 1 ms), so the game uses virtually no CPU while waiting between steps.

### Snake Representation

The snake body is stored as an array of `{x, y}` points, head at index 0:

```c
typedef struct { int x; int y; } Point;
static Point snake[MAX_LEN];
static int   snake_len;
```

Moving the snake shifts the entire array by one position toward the tail, then writes the new head into `snake[0]`. When food is eaten, `snake_len` is incremented before the shift, which preserves the old tail cell.

### Food Spawning

An LCG random-number generator seeded from `get_tick_count()` at startup picks a random cell. If the cell is occupied by the snake, it retries until an empty cell is found.

```
rng = rng * 1664525 + 1013904223   (Numerical Recipes LCG)
food.x = GX1 + rng % GW
food.y = GY1 + rng % GH
```

### Speed Scaling

The game starts at 200 ms per step. Each food eaten subtracts 5 ms (minimum 60 ms), so the snake accelerates as the score grows.

### PC Speaker Feedback

A short beep is played on food pickup using PIT channel 2 (same hardware as assignment 5):

- Food eaten: 880 Hz, 40 ms
- Death: 220 Hz (150 ms) then 165 Hz (300 ms) - a descending two-note sound

---

## Files Changed / Added

| File | Change |
|------|--------|
| `include/keyboard.h` | Added `keyboard_set_game_mode()` and `keyboard_consume_scancode()` |
| `src/keyboard.c` | Added `game_mode` flag, `last_scancode` buffer, two new functions |
| `include/snake.h` | New - declares `run_snake()` |
| `src/snake.c` | New - full Snake game implementation |
| `src/kernel.c` | Replaced `play_music()` call with `run_snake()` |
| `CMakeLists.txt` | Added `src/snake.c` to the build |

---

## Controls

| Key | Action |
|-----|--------|
| W   | Move up |
| A   | Move left |
| S   | Move down |
| D   | Move right |
| R   | Restart after game over |
