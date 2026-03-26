#include <libc/stdint.h>
#include <libc/stdio.h>
#include <gdt.h>
#include <terminal.h>

/*
 * main - kernel entry point
 *
 * Called from _start (src/multiboot2.asm) after the stack is set up.
 * The bootloader passes two values on the stack:
 *   magic - should equal 0x36d76289 for Multiboot2
 *   mbi   - physical address of the Multiboot2 information structure
 *
 * Initialisation order matters:
 *   1. GDT first – the CPU needs valid segment descriptors before we
 *      do anything that might fault or reload segment registers.
 *   2. Terminal second – once the GDT is in place we can safely write
 *      to the VGA buffer and produce visible output.
 */
void main(uint32_t magic, uint32_t mbi)
{
    /* Suppress unused-parameter warnings; we don't use them yet */
    (void)magic;
    (void)mbi;

    /* Step 1: Install our own Global Descriptor Table */
    gdt_init();

    /* Step 2: Initialise the VGA text-mode terminal (clears the screen) */
    terminal_init();

    /* Step 3: Print the required greeting */
    printf("Hello World\n");

    /*
     * Hang forever – there is nothing more to do.
     * cli disables interrupts; hlt halts the CPU until the next interrupt,
     * which will never come, so the loop is effectively infinite.
     */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
