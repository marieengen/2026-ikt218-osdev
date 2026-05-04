#include <irq.h>
#include <io.h>
#include <libc/stdint.h>

/* 8259 PIC I/O ports */
#define PIC1_COMMAND  0x20   /* Master PIC command port */
#define PIC1_DATA     0x21   /* Master PIC data port    */
#define PIC2_COMMAND  0xA0   /* Slave  PIC command port */
#define PIC2_DATA     0xA1   /* Slave  PIC data port    */

/* PIC commands */
#define PIC_EOI       0x20   /* End-of-Interrupt command */

/* Initialisation Command Words */
#define ICW1_INIT     0x10   /* Begin initialisation sequence */
#define ICW1_ICW4     0x01   /* Tell PIC that ICW4 will follow */
#define ICW4_8086     0x01   /* 8086/88 mode (not MCS-80/85)  */

/*
 * Table of registered C handlers for IRQ lines 0-15.
 * Initialised to NULL by .bss.
 */
static irq_handler_t irq_handlers[16];

/*
 * irq_init - remap the 8259 PIC
 *
 * After a PC reset the PIC maps:
 *   IRQ 0- 7 → INT 0x08-0x0F  (conflicts with CPU exception vectors!)
 *   IRQ 8-15 → INT 0x70-0x77
 *
 * We remap to:
 *   IRQ 0- 7 → INT 0x20-0x27
 *   IRQ 8-15 → INT 0x28-0x2F
 *
 * The remapping follows the standard 4-step ICW (Initialisation Command Word)
 * sequence. io_wait() is called between each write to allow the PIC to
 * process the command on real hardware.
 */
void irq_init(void)
{
    /* Save the current IRQ masks so we can restore them after remapping */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: Start initialisation; cascade mode; ICW4 required */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: Set new interrupt vector offsets */
    outb(PIC1_DATA, 0x20);  /* Master: IRQ0-7 → INT 0x20-0x27 */
    io_wait();
    outb(PIC2_DATA, 0x28);  /* Slave:  IRQ8-15 → INT 0x28-0x2F */
    io_wait();

    /* ICW3: Inform master/slave of their wiring
     *   Master: slave is connected on IRQ line 2 (bit mask 00000100)
     *   Slave:  its cascade identity is IRQ line 2 (binary 010)        */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: Set 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Unmask IRQ0 (PIT) and IRQ1 (keyboard); mask everything else */
    (void)mask1;
    (void)mask2;
    outb(PIC1_DATA, 0xFC);  /* 1111 1100 — IRQ0 and IRQ1 unmasked */
    outb(PIC2_DATA, 0xFF);  /* all slave IRQs masked               */
}

void irq_install_handler(uint8_t irq_num, irq_handler_t handler)
{
    if (irq_num < 16) {
        irq_handlers[irq_num] = handler;
    }
}

/*
 * irq_dispatch - C-level IRQ dispatcher
 *
 * Called from irq_common_stub in interrupt_stubs.asm.
 *
 * 1. Look up and call the registered handler (if any).
 * 2. Send End-of-Interrupt (EOI) to the PIC(s).
 *
 * EOI order: for IRQ8-15 (slave PIC), send EOI to slave first, then
 * to master. For IRQ0-7, only the master needs an EOI.
 * Failing to send EOI will block all further interrupts at the same
 * or lower priority.
 */
void irq_dispatch(registers_t *regs)
{
    uint8_t irq_num = (uint8_t)(regs->int_no - 0x20);

    if (irq_num < 16 && irq_handlers[irq_num]) {
        irq_handlers[irq_num](regs);
    }

    /* Send EOI to slave PIC for IRQ8-15 */
    if (irq_num >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}
