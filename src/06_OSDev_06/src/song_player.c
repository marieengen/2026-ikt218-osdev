#include <song/song.h>
#include <pit.h>
#include <io.h>
#include <memory.h>
#include <libc/stdio.h>

/*
 * PC Speaker (PCSPK) driver
 *
 * The PC speaker is driven by PIT channel 2. To produce a tone:
 *   1. Program PIT channel 2 with the desired frequency divisor.
 *   2. Connect the speaker to PIT channel 2 via port 0x61.
 *
 * Port 0x61 — PC Speaker control:
 *   Bit 0: PIT channel 2 gate  (1 = PIT channel 2 is running)
 *   Bit 1: Speaker data enable (1 = speaker output connected to PIT ch2)
 *
 * Port 0x43 — PIT command port (shared with pit.c's channel 0 setup,
 *              but we only write a channel-2 command here).
 * Port 0x42 — PIT channel 2 data port.
 */

#define PC_SPEAKER_PORT  0x61
#define PIT_CMD_PORT_SPK 0x43
#define PIT_CH2_PORT     0x42
#define PIT_BASE_FREQ    1193180u

/* ------------------------------------------------------------------ */

/*
 * enable_speaker - connect the speaker to PIT channel 2
 * Sets bits 0 (PIT2 gate) and 1 (speaker data) in port 0x61.
 */
static void enable_speaker(void)
{
    uint8_t val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val | 0x03);
}

/*
 * disable_speaker - fully disconnect the speaker
 * Clears both bits 0 and 1 in port 0x61.
 */
static void disable_speaker(void)
{
    uint8_t val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val & ~0x03);
}

/*
 * play_sound - start producing a tone at the given frequency
 *
 * Programs PIT channel 2 as a square-wave generator (mode 3).
 * Command byte 0xB6:
 *   Bits 7:6 = 10  : channel 2
 *   Bits 5:4 = 11  : access mode lobyte/hibyte
 *   Bits 3:1 = 011 : mode 3 (square wave)
 *   Bit  0   = 0   : binary counting
 *
 * @frequency : tone in Hz; 0 means silence (rest) — function returns early.
 */
static void play_sound(uint32_t frequency)
{
    if (frequency == 0) return;

    uint32_t divisor = PIT_BASE_FREQ / frequency;

    outb(PIT_CMD_PORT_SPK, 0xB6);
    outb(PIT_CH2_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH2_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    /* Connect speaker to PIT channel 2 output */
    uint8_t val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val | 0x03);
}

/*
 * stop_sound - cut the speaker output without disabling the PIT gate
 * Clears bit 1 (speaker data enable) while leaving bit 0 intact.
 */
static void stop_sound(void)
{
    uint8_t val = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, val & ~0x02);
}

/* ------------------------------------------------------------------ */

/*
 * play_song_impl - play every note in a song sequentially
 *
 * For each note:
 *   - R (frequency == 0) produces silence for the note's duration.
 *   - Non-zero frequency drives PIT channel 2 at that frequency.
 *   - sleep_interrupt() waits for the note's duration in milliseconds.
 */
void play_song_impl(Song *song)
{
    enable_speaker();

    for (uint32_t i = 0; i < song->length; i++) {
        Note *note = &song->notes[i];

        if (note->frequency == 0) {
            stop_sound();          /* rest: silence for duration */
        } else {
            play_sound(note->frequency);
        }

        sleep_interrupt(note->duration);
        stop_sound();
    }

    disable_speaker();
}

/*
 * play_song - thin wrapper that calls play_song_impl via the function pointer
 */
static void play_song(Song *song)
{
    play_song_impl(song);
}

/*
 * create_song_player - allocate and initialise a SongPlayer on the heap
 */
SongPlayer *create_song_player(void)
{
    SongPlayer *player = (SongPlayer *)malloc(sizeof(SongPlayer));
    player->play_song = play_song;
    return player;
}

/* ------------------------------------------------------------------ */

/*
 * play_music - main music loop
 *
 * Plays all songs in the playlist in a loop forever.
 * Keyboard interrupts still fire during sleep_interrupt() waits.
 */
void play_music(void)
{
    Song songs[] = {
        { music_1,              sizeof(music_1)              / sizeof(Note) },
        { starwars_theme,       sizeof(starwars_theme)       / sizeof(Note) },
        { battlefield_1942_theme, sizeof(battlefield_1942_theme) / sizeof(Note) },
        { music_4,              sizeof(music_4)              / sizeof(Note) },
    };
    uint32_t n_songs = sizeof(songs) / sizeof(Song);

    SongPlayer *player = create_song_player();

    while (1) {
        for (uint32_t i = 0; i < n_songs; i++) {
            printf("Playing song %d...\n", i + 1);
            player->play_song(&songs[i]);
            printf("Finished song %d.\n\n", i + 1);
        }
    }
}
