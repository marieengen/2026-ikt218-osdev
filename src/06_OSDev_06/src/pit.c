#include <pit.h>
#include <irq.h>
#include <io.h>

/*
 * Global tick counter incremented by the IRQ0 handler on every PIT interrupt.
 *
 * 'volatile' is critical here: it tells the compiler that this variable can
 * change outside of the current execution flow (i.e. inside an ISR), so it
 * must re-read it from memory on every access rather than caching it in a
 * register. Without 'volatile', the busy-wait and interrupt-sleep loops
 * would be optimised into infinite loops that never see the updated value.
 */
static volatile uint32_t tick_count = 0;

/*
 * pit_irq_handler - IRQ0 handler called on every PIT tick
 *
 * Simply increments tick_count. The EOI is sent by irq_dispatch in irq.c
 * after this handler returns — we must NOT send it here.
 */
static void pit_irq_handler(registers_t *regs)
{
    (void)regs;
    tick_count++;
}

/* ------------------------------------------------------------------ */

uint32_t get_tick_count(void)
{
    return tick_count;
}

/* ------------------------------------------------------------------ */

/*
 * init_pit - configure PIT channel 0 at TARGET_FREQUENCY (1000 Hz)
 *
 * Command byte sent to PIT_CMD_PORT (0x43):
 *   Bits 7:6 = 00  : Select channel 0
 *   Bits 5:4 = 11  : Access mode: lo byte then hi byte
 *   Bits 3:1 = 010 : Operating mode 2 (rate generator / periodic)
 *   Bit  0   = 0   : Binary counting (not BCD)
 *   → 0x34
 *
 * The divisor is sent as two bytes (lo first, hi second) to channel 0.
 * PIT fires IRQ0 at: 1193180 / DIVIDER ≈ 1000 Hz → 1 tick per ms.
 */
void init_pit(void)
{
    outb(PIT_CMD_PORT,      0x34);
    outb(PIT_CHANNEL0_PORT, (uint8_t)(DIVIDER & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((DIVIDER >> 8) & 0xFF));

    irq_install_handler(0, pit_irq_handler);
}

/* ------------------------------------------------------------------ */

/*
 * sleep_busy - busy-wait for the given number of milliseconds
 *
 * Watches tick_count advance one tick at a time. The CPU is fully occupied
 * during the wait (100% utilisation on one core). Useful when precise timing
 * is needed and interrupt latency is unacceptable.
 *
 * With TICKS_PER_MS = 1, each loop iteration waits exactly 1 ms.
 */
void sleep_busy(uint32_t milliseconds)
{
    uint32_t start         = tick_count;
    uint32_t ticks_to_wait = milliseconds * TICKS_PER_MS;
    uint32_t elapsed       = 0;

    while (elapsed < ticks_to_wait) {
        /* Spin until the tick counter advances by one */
        while (tick_count == start + elapsed) {
            /* busy wait — intentionally empty */
        }
        elapsed++;
    }
}

/* ------------------------------------------------------------------ */

/*
 * sleep_interrupt - sleep for the given number of milliseconds using hlt
 *
 * Halts the CPU between ticks using the `hlt` instruction. The CPU wakes on
 * the next IRQ0 (or any other interrupt), checks whether enough time has
 * passed, and halts again if not. This leaves the CPU idle between ticks,
 * drastically reducing power consumption compared to busy-waiting.
 *
 * The `sti` before `hlt` ensures interrupts are enabled so the CPU can
 * actually wake up. The interrupt gate (0x8E) in the IDT clears IF on entry,
 * so after iret IF is restored to its previous state — no explicit re-enable
 * is needed between iterations, but it doesn't hurt to be explicit.
 */
void sleep_interrupt(uint32_t milliseconds)
{
    uint32_t end_tick = tick_count + milliseconds * TICKS_PER_MS;

    while (tick_count < end_tick) {
        __asm__ volatile ("sti\n\thlt");
    }
}
