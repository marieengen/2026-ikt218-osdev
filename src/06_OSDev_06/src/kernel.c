#include <libc/stdint.h>
#include <libc/stdio.h>
#include <gdt.h>
#include <idt.h>
#include <isr.h>
#include <irq.h>
#include <keyboard.h>
#include <terminal.h>
#include <memory.h>
#include <pit.h>

/*
 * 'end' is defined by the linker script (src/arch/i386/linker.ld).
 * Its address is the first byte of free memory after the kernel binary
 * (.text + .rodata + .data + .bss sections).
 */
extern uint32_t end;

/*
 * isr_demo_handler - demo handler for software-triggered CPU exceptions.
 * Prints the vector number and returns safely.
 */
static void isr_demo_handler(registers_t *regs)
{
    terminal_writecolor("  [ISR] Caught interrupt ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printf("%d", regs->int_no);
    terminal_writecolor(" - returned safely.\n", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

/*
 * main - kernel entry point
 *
 * Initialisation order (each step depends on the previous):
 *   1. GDT      - valid segments required before any fault can be handled
 *   2. Terminal - clear screen; needed for all output below
 *   3. IDT      - load exception/IRQ gate descriptors (interrupts still off)
 *   4. IRQ      - remap 8259 PIC: IRQ 0-15 -> INT 0x20-0x2F
 *   5. Keyboard - register IRQ1 handler
 *   6. Memory   - initialise heap at &end, set up identity-mapped paging
 *   7. PIT      - configure channel 0 at 1000 Hz, register IRQ0 handler
 *   8. sti      - enable hardware interrupts
 */
void main(uint32_t magic, uint32_t mbi)
{
    (void)magic;
    (void)mbi;

    gdt_init();
    terminal_init();

    terminal_writecolor("=== UiA OS - Assignment 4 ===\n\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

    idt_init();
    printf("IDT loaded.\n");

    irq_init();
    printf("PIC remapped (IRQ 0-15 -> INT 0x20-0x2F).\n");

    keyboard_init();
    printf("Keyboard handler installed on IRQ1.\n");

    /* --- Memory management --- */
    init_kernel_memory(&end);
    printf("Heap initialised at 0x%x.\n", (uint32_t)&end);

    init_paging();
    printf("Paging enabled (identity-mapped, 4 MB pages).\n\n");

    print_memory_layout();

    /* --- PIT --- */
    init_pit();
    printf("PIT configured at 1000 Hz (1 tick = 1 ms).\n\n");

    /* --- Install demo ISR handlers --- */
    isr_install_handler(0, isr_demo_handler);
    isr_install_handler(3, isr_demo_handler);
    isr_install_handler(6, isr_demo_handler);

    /* --- Enable hardware interrupts --- */
    __asm__ volatile ("sti");

    /* --- Test malloc / free --- */
    terminal_writecolor("--- malloc / free test ---\n", VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);

    void *mem1 = malloc(12345);
    void *mem2 = malloc(54321);
    void *mem3 = malloc(13331);

    printf("malloc(12345) -> 0x%x\n", (uint32_t)mem1);
    printf("malloc(54321) -> 0x%x\n", (uint32_t)mem2);
    printf("malloc(13331) -> 0x%x\n", (uint32_t)mem3);

    free(mem1);
    printf("free(0x%x) OK\n", (uint32_t)mem1);

    void *mem4 = malloc(100);
    printf("malloc(100)   -> 0x%x  (reused freed block)\n\n", (uint32_t)mem4);

    /* --- PIT sleep test --- */
    terminal_writecolor("--- PIT sleep test ---\n", VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);

    uint32_t counter = 0;
    while (1) {
        printf("[%d]: Sleeping with busy-waiting (HIGH CPU).\n", counter);
        sleep_busy(1000);
        printf("[%d]: Slept using busy-waiting.\n", counter++);

        printf("[%d]: Sleeping with interrupts (LOW CPU).\n", counter);
        sleep_interrupt(1000);
        printf("[%d]: Slept using interrupts.\n", counter++);
    }
}
