# Assignment 3 Report – Interrupts: IDT, ISR, IRQ, and Keyboard

**Course:** IKT218 – Operating System Development
**Group:** 06_OSDev_06
**Assignment:** 3 – Interrupts

---

## Overview

This report documents the implementation of the interrupt subsystem for the UiA OS kernel. The work covers four tasks:

1. **IDT** – Interrupt Descriptor Table setup
2. **ISR** – Interrupt Service Routines for CPU exceptions
3. **IRQ** – Hardware interrupt support via the 8259 PIC
4. **Keyboard** – PS/2 keyboard driver on IRQ1

---

## 1. Interrupts in Operating Systems

Interrupts are signals that tell the CPU to stop what it is doing, save its current state, and run a specific piece of code called an interrupt handler. There are three kinds:

- **Hardware interrupts (IRQs)** – fired by physical devices (keyboard, timer, disk)
- **Software interrupts** – triggered deliberately by the running program (e.g. `int $3` for breakpoint)
- **CPU exceptions** – fired automatically by the CPU when something goes wrong (divide by zero, page fault, etc.)

When an interrupt fires, the CPU uses the **Interrupt Descriptor Table (IDT)** to find the address of the correct handler, saves EFLAGS/CS/EIP to the stack, and jumps there. After the handler finishes it executes `iret` to restore the saved state and resume normal execution.

---

## 2. Interrupt Descriptor Table (IDT)

### Structure

The IDT is an array of up to 256 **gate descriptors**, one per interrupt vector. Each descriptor is 8 bytes:

```
Bits 15: 0  – base_low   (bits 15:0  of handler address)
Bits 31:16  – selector   (code segment: must be 0x08 = kernel CS)
Bits 39:32  – always0    (reserved, must be zero)
Bits 47:40  – flags      (type and attribute byte)
Bits 63:48  – base_high  (bits 31:16 of handler address)
```

The **flags byte** used for all entries is `0x8E`:
```
1  00  0  1110
P DPL  0  Type

P    = 1  : segment present
DPL  = 00 : kernel privilege (ring 0)
Type = 1110: 32-bit interrupt gate (clears IF on entry)
```

Using an interrupt gate (rather than a trap gate `0xF`) automatically disables further interrupts while the handler runs, preventing re-entrancy issues.

### Loading the IDT

The CPU is told where the IDT lives via the **IDTR register**, loaded with the `lidt` instruction. The IDTR holds a 6-byte structure:

```
Bytes 0–1 : IDT size in bytes – 1
Bytes 2–5 : Linear address of the IDT
```

This is implemented in `src/arch/i386/idt_flush.asm`:

```nasm
idt_flush:
    lidt [ip]   ; ip is the idt_ptr_t variable in idt.c
    ret
```

Unlike `lgdt`, no segment register reload is needed after `lidt`. Interrupts stay disabled until `sti` is called explicitly.

### Files

| File | Purpose |
|------|---------|
| `include/idt.h` | `idt_entry_t`, `idt_ptr_t` structs, `idt_init()` declaration |
| `src/idt.c` | Fills all 256 entries, calls `idt_flush()` |
| `src/arch/i386/idt_flush.asm` | Executes `lidt` |

---

## 3. Interrupt Service Routines (ISRs)

### Assembly Stubs

Each interrupt vector needs an entry point in assembly. Since the CPU only automatically saves EFLAGS/CS/EIP (and optionally an error code), the stub must save all other registers before calling C code.

We use NASM macros to generate all 32 stubs without repetition:

```nasm
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0    ; dummy error code (CPU didn't push one)
    push dword %1   ; interrupt vector number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push dword %1   ; CPU already pushed an error code
    jmp isr_common_stub
%endmacro
```

**Which exceptions push an error code** is fixed by the x86 architecture:

| Pushes error code | Vector numbers |
|---|---|
| Yes | 8, 10, 11, 12, 13, 14, 17, 30 |
| No | All others (0-7, 9, 15-16, 18-29, 31) |

Using the wrong macro silently corrupts the stack and causes a triple fault.

### The Common Stub and `registers_t`

All 32 ISR stubs jump to `isr_common_stub`, which saves the full CPU state and calls the C handler:

```nasm
isr_common_stub:
    pusha               ; save EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    mov ax, ds
    push eax            ; save data segment
    mov ax, 0x10        ; load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp            ; pass pointer to stack frame as argument
    call isr_dispatch
    add esp, 4
    pop eax             ; restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8          ; discard int_no and err_code
    iret
```

The C handler receives a pointer to a `registers_t` struct, whose field order **must exactly match** the push order (last pushed = lowest address = first field):

```c
typedef struct {
    uint32_t ds;          // pushed last → lowest address
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  // pusha
    uint32_t int_no;      // pushed by stub macro
    uint32_t err_code;    // pushed by stub macro or CPU
    uint32_t eip, cs, eflags;  // pushed by CPU → highest addresses
} __attribute__((packed)) registers_t;
```

### C Dispatcher

`isr_dispatch()` in `src/isr.c` looks up a registered handler for the vector. If none is installed, it prints a diagnostic message (vector name, registers) and halts:

```c
void isr_dispatch(registers_t *regs) {
    if (regs->int_no < 32 && isr_handlers[regs->int_no]) {
        isr_handlers[regs->int_no](regs);
        return;
    }
    // print error message and halt
}
```

### Demonstrating Three ISRs

Three demo handlers are registered in `kernel.c` before `sti`:

```c
isr_install_handler(0, isr_demo_handler);  // Division Error
isr_install_handler(3, isr_demo_handler);  // Breakpoint
isr_install_handler(6, isr_demo_handler);  // Invalid Opcode
```

They are triggered with software interrupts:

```c
__asm__ volatile ("int $0");  // ISR 0
__asm__ volatile ("int $3");  // ISR 3
__asm__ volatile ("int $6");  // ISR 6
```

> **Note:** `int $0` is a direct software invocation of vector 0, not an actual divide-by-zero instruction. The CPU pushes the address of the instruction *after* `int $0`, so `iret` safely returns and execution continues. A real `div` with a zero divisor would push the faulting instruction and loop forever.

### Files

| File | Purpose |
|------|---------|
| `include/isr.h` | `registers_t` struct, `isr_handler_t` typedef, declarations |
| `src/isr.c` | `isr_dispatch()`, exception name table, `isr_install_handler()` |
| `src/arch/i386/interrupt_stubs.asm` | All 32 ISR stubs + common stub |

---

## 4. Interrupt Requests (IRQs)

### The 8259 PIC

Hardware interrupts are managed by the **8259 Programmable Interrupt Controller (PIC)**. A PC has two PICs in cascade:
- **Master PIC** handles IRQ0–7, connected directly to the CPU
- **Slave PIC** handles IRQ8–15, connected to the master on IRQ2

After a PC reset, the PIC maps IRQ0–7 to interrupt vectors 0x08–0x0F — which conflicts with CPU exception vectors. We must **remap** them.

### PIC Remapping

The remapping sequence sends four Initialisation Command Words (ICWs) to each PIC:

```c
// ICW1: start initialisation
outb(0x20, 0x11);   // master command
outb(0xA0, 0x11);   // slave command

// ICW2: new vector offsets
outb(0x21, 0x20);   // master: IRQ0-7 → INT 0x20-0x27
outb(0xA1, 0x28);   // slave:  IRQ8-15 → INT 0x28-0x2F

// ICW3: cascade wiring
outb(0x21, 0x04);   // master: slave is on IRQ2 (bitmask 00000100)
outb(0xA1, 0x02);   // slave:  cascade identity = IRQ2 (binary 010)

// ICW4: 8086 mode
outb(0x21, 0x01);
outb(0xA1, 0x01);
```

`io_wait()` (a write to port 0x80) is called between each step to give the PIC time to process the command on real hardware.

### End of Interrupt (EOI)

After every IRQ handler, we must send an **End of Interrupt** command to the PIC(s), otherwise the PIC will not send further interrupts at the same or lower priority.

```c
if (irq_num >= 8) {
    outb(0xA0, 0x20);   // EOI to slave (for IRQ8-15)
}
outb(0x20, 0x20);       // EOI to master (always)
```

For IRQ8–15 (slave), the slave must receive EOI **before** the master.

### IRQ Stubs

IRQ stubs follow the same pattern as ISR stubs but use `irq_common_stub` which calls `irq_dispatch`:

```nasm
%macro IRQ 2
global irq%1
irq%1:
    cli
    push dword 0
    push dword %2   ; remapped vector number (0x20 + irq line)
    jmp irq_common_stub
%endmacro

IRQ  0, 0x20   ; PIT timer
IRQ  1, 0x21   ; PS/2 keyboard
; ... IRQ2-15 ...
```

### Files

| File | Purpose |
|------|---------|
| `include/irq.h` | `irq_handler_t` typedef, `irq_init()`, `irq_install_handler()` |
| `src/irq.c` | PIC remapping, `irq_dispatch()` with EOI logic |
| `include/io.h` | `outb()`, `inb()`, `io_wait()` port I/O helpers |
| `src/arch/i386/interrupt_stubs.asm` | IRQ 0-15 stubs + `irq_common_stub` |

---

## 5. Keyboard Logger (IRQ1)

### How the PS/2 Keyboard Works

When a key is pressed or released, the PS/2 keyboard controller signals **IRQ1**. The CPU jumps to our IRQ1 handler. We must read port `0x60` to get the **scancode** byte — this also clears the controller's output buffer so it can accept the next keypress.

Scancode format (Scancode Set 1):
- **Make code** (key press): bit 7 = 0 — the scancode identifies which key
- **Break code** (key release): bit 7 = 1 — `scancode = make_code | 0x80`

We ignore break codes and only act on make codes.

### Scancode to ASCII

A lookup table maps scancode → ASCII character for the US QWERTY layout:

```c
static const char scancode_table[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    // ... etc.
    ' ',   // 0x39 = Space
    // non-printable keys (Ctrl, Alt, F-keys, etc.) → 0
};
```

### Handler

```c
static void keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    if (scancode & 0x80) return;        // ignore key-release
    char c = scancode_table[scancode];
    if (c == '\b')
        terminal_backspace();           // erase last character
    else if (c != 0)
        putchar(c);                     // print to terminal
}
```

### Files

| File | Purpose |
|------|---------|
| `include/keyboard.h` | `keyboard_init()` declaration |
| `src/keyboard.c` | IRQ1 handler, US QWERTY scancode table |

---

## 6. Initialisation Order in `kernel.c`

```c
gdt_init();        // 1. CPU needs valid segments before any fault
terminal_init();   // 2. Clear screen, enable terminal output
idt_init();        // 3. Load IDT (interrupts still disabled)
irq_init();        // 4. Remap PIC
keyboard_init();   // 5. Register IRQ1 handler
// install demo ISR handlers (0, 3, 6)
sti();             // 6. Enable interrupts
// trigger ISR 0, 3, 6 via software
// spin on hlt waiting for keyboard input
```

---

## 7. Testing

### Build and run (outside devcontainer)

```bash
cmake -S src/06_OSDev_06 -B build/06_OSDev_06
cmake --build build/06_OSDev_06

mkdir -p /tmp/isodir/boot/grub
cp build/06_OSDev_06/kernel.bin /tmp/isodir/boot/kernel.bin
echo 'set timeout=0
menuentry "OSDev06" { multiboot2 /boot/kernel.bin; boot }' \
    > /tmp/isodir/boot/grub/grub.cfg
grub-mkrescue -o /tmp/os.iso /tmp/isodir

qemu-system-i386 -cdrom /tmp/os.iso -m 64M
```

### Build and run (devcontainer)

```bash
cmake --build .
cmake --build . --target uiaos-create-image
./scripts/start_qemu.sh kernel.bin disk.iso
```

### Expected output

```
=== UiA OS — Assignment 3 ===

GDT loaded.
IDT loaded.
PIC remapped (IRQ 0-15 -> INT 0x20-0x2F).
Keyboard handler installed on IRQ1.

Interrupts enabled.

--- ISR Demo ---
Triggering ISR 0  (Division Error)...
  [ISR] Caught interrupt 0 — returned safely.
Triggering ISR 3  (Breakpoint)...
  [ISR] Caught interrupt 3 — returned safely.
Triggering ISR 6  (Invalid Opcode)...
  [ISR] Caught interrupt 6 — returned safely.

All ISRs handled successfully.

--- Keyboard Logger Active ---
Type below (Backspace supported):
> _
```

Typing on the keyboard prints characters to screen. Backspace erases the last character.
