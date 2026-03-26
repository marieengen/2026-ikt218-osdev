#pragma once

#include <libc/stdint.h>
#include <libc/stddef.h>

/* PIT I/O ports */
#define PIT_CMD_PORT      0x43   /* Mode/command register (write only)  */
#define PIT_CHANNEL0_PORT 0x40   /* Channel 0 data port (IRQ0 / system) */
#define PIT_CHANNEL1_PORT 0x41   /* Channel 1 (legacy DRAM refresh)     */
#define PIT_CHANNEL2_PORT 0x42   /* Channel 2 (PC speaker)              */

/* PIT base oscillator frequency in Hz */
#define PIT_BASE_FREQUENCY 1193180u

/* We configure channel 0 to fire IRQ0 at 1000 Hz (1 ms per tick) */
#define TARGET_FREQUENCY   1000u
#define DIVIDER            (PIT_BASE_FREQUENCY / TARGET_FREQUENCY)  /* 1193 */

/* With IRQ0 at 1000 Hz, one tick = one millisecond */
#define TICKS_PER_MS       1u

/*
 * init_pit - programme channel 0 to fire IRQ0 at TARGET_FREQUENCY Hz
 *            and install the tick-counting IRQ0 handler.
 */
void init_pit(void);

/*
 * sleep_interrupt - sleep for 'milliseconds' ms using IRQ-driven waiting.
 * The CPU halts between ticks (low CPU usage).
 */
void sleep_interrupt(uint32_t milliseconds);

/*
 * sleep_busy - sleep for 'milliseconds' ms using busy-waiting.
 * The CPU spins continuously (high CPU usage; more precise on noisy systems).
 */
void sleep_busy(uint32_t milliseconds);

/*
 * get_tick_count - return the total number of IRQ0 ticks since init_pit().
 */
uint32_t get_tick_count(void);
