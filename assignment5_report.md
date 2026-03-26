# Assignment 5 Report – PC Speaker Music Player

**Course:** IKT218 – Operating System Development
**Group:** 06_OSDev_06
**Assignment:** 5 – Sound via PCSPK

---

## Overview

This assignment implements a music player that produces sound using the PC Speaker (PCSPK) hardware device, timed by the PIT driver from assignment 4.

---

## How the PC Speaker Works

The PC speaker is one of the oldest pieces of IBM PC hardware still emulated by QEMU. It is driven by **PIT channel 2**, which generates a square wave at a programmable frequency. Port **0x61** controls whether the speaker's amplifier is connected to that signal.

### Port 0x61 – PC Speaker Control Register

| Bit | Name | Effect |
|-----|------|--------|
| 0 | PIT channel 2 gate | 1 = PIT channel 2 is counting (clock running) |
| 1 | Speaker data enable | 1 = speaker output connected to PIT channel 2 |

To produce a tone, both bits must be set. Clearing bit 1 silences the speaker without stopping PIT channel 2.

### PIT Channel 2 – Tone Generation

PIT channel 2 is separate from channel 0 (which we use for the 1000 Hz system tick). We configure it independently:

- **Port 0x43** – PIT command register (shared by all channels)
- **Port 0x42** – Channel 2 data register

Command byte `0xB6` programs channel 2:

```
1 0 1 1 0 1 1 0
│ │ │ │ │ └─┘ └─ Binary counting
│ │ │ │ └───── Mode 3: square wave generator
│ │ └─┴─────── Access: lo byte then hi byte
└─┴─────────── Channel 2
```

The divisor for a desired frequency:
```
divisor = 1 193 180 / frequency_Hz
```

---

## Implementation

### `play_sound(frequency)`

1. If frequency == 0 (rest), return immediately.
2. Calculate `divisor = 1193180 / frequency`.
3. Send command `0xB6` to port `0x43`.
4. Send `divisor & 0xFF` (low byte) to port `0x42`.
5. Send `(divisor >> 8) & 0xFF` (high byte) to port `0x42`.
6. Read port `0x61`, set bits 0 and 1, write back.

### `stop_sound()`

Reads port `0x61` and clears bit 1 (disconnects speaker output). Bit 0 (PIT gate) is left set so the PIT keeps running — this avoids a glitch when the next note starts.

### `enable_speaker()` / `disable_speaker()`

Set or clear both bits 0 and 1 of port `0x61`. Called at the start and end of a song to ensure a clean state.

### `play_song_impl(Song *song)`

```c
enable_speaker();
for each note in song:
    if note.frequency == 0:
        stop_sound()      // rest: silence
    else:
        play_sound(note.frequency)
    sleep_interrupt(note.duration)  // wait in ms using PIT tick counter
    stop_sound()
disable_speaker();
```

`sleep_interrupt` halts the CPU between PIT ticks so the speaker plays the note for the exact duration without wasting CPU cycles.

### `SongPlayer` and `create_song_player()`

```c
typedef struct {
    void (*play_song)(Song *song);
} SongPlayer;

SongPlayer *create_song_player(void) {
    SongPlayer *player = malloc(sizeof(SongPlayer));
    player->play_song = play_song_impl;
    return player;
}
```

The function pointer allows future implementations (e.g. a MIDI player) to plug in without changing the call site.

---

## Song Format

Each note is a `{frequency, duration_ms}` pair:

```c
typedef struct {
    uint32_t frequency;  // Hz; 0 = rest (R)
    uint32_t duration;   // milliseconds
} Note;
```

Example (opening of Super Mario Bros):
```c
{E5, 250}, {R, 125}, {E5, 125}, {R, 125}, {E5, 125}, {R, 125},
{C5, 125}, {E5, 125}, {G5, 125}, {R, 125}, {G4, 125}, {R, 250},
```

All note frequencies are defined in `include/song/frequencies.h` (e.g. `E5 = 659 Hz`).

---

## Playlist

| # | Song |
|---|------|
| 1 | Super Mario Bros theme |
| 2 | Star Wars main theme |
| 3 | Battlefield 1942 theme |
| 4 | Frere Jacques |

Songs loop indefinitely. The keyboard IRQ still fires during `sleep_interrupt` waits, so typed input still appears on screen while music plays.

---

## Files

| File | Purpose |
|------|---------|
| `include/song/frequencies.h` | Note frequency constants (C0–B9) |
| `include/song/song.h` | `Note`, `Song`, `SongPlayer` structs + song data arrays |
| `include/libc/system.h` | Convenience header pulling in stdint/stddef/stdbool |
| `src/song_player.c` | PC speaker driver + `play_song_impl`, `create_song_player`, `play_music` |

---

## Testing

Build and run as in previous assignments. With QEMU on Windows/WSL you should hear the PC speaker output through your system audio (QEMU emulates the PCSPK). If no audio is heard, ensure QEMU has audio enabled — the devcontainer setup includes `pcspk-audiodev` in the QEMU launch script.
