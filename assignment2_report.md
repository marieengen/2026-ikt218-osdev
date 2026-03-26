# Assignment 2 Report – GDT and Text-Mode Terminal

**Course:** IKT218 – Operating System Development
**Group:** 06_OSDev_06
**Assignment:** 2 – Global Descriptor Table and Text Output

---

## Overview

This report documents the implementation of two core OS features:

1. The **Global Descriptor Table (GDT)** – tells the CPU how memory is segmented and what privilege levels apply.
2. A **VGA text-mode terminal** with a working `printf` – produces visible output in the kernel.

---

## 1. Global Descriptor Table (GDT)

### Background

The x86 CPU uses a segment-based memory model for protection. The GDT is a table in memory where each 8-byte entry (called a *segment descriptor*) defines a region of memory with a base address, a size limit, and a set of access flags.

Before the kernel can run safely, it must install its own GDT. The bootloader (Limine) sets up a temporary GDT for us, but we replace it with our own so that we are in full control.

### Descriptors Implemented

| Index | Selector | Name        | Base | Limit | Description                         |
|-------|----------|-------------|------|-------|-------------------------------------|
| 0     | 0x00     | NULL        | 0    | 0     | Required by CPU; must never be used |
| 1     | 0x08     | Code (CS)   | 0    | 4 GB  | Ring 0, executable, readable        |
| 2     | 0x10     | Data (DS…)  | 0    | 4 GB  | Ring 0, read/write                  |

Both the code and data segments use a **flat model**: base = 0 and limit = 4 GB. This means every virtual address equals its physical address, which is the simplest setup for a 32-bit kernel.

### Entry Bit Layout

Each 8-byte GDT entry has a complex layout because of x86 backwards compatibility:

```
Byte 0–1 : Limit bits 15:0
Byte 2–3 : Base  bits 15:0
Byte 4   : Base  bits 23:16
Byte 5   : Access byte   (P | DPL | S | E | DC | RW | A)
Byte 6   : Granularity   (G | DB | L | AVL | limit bits 19:16)
Byte 7   : Base  bits 31:24
```

**Access byte for code (0x9A):**

| P | DPL | S | E | DC | RW | A |
|---|-----|---|---|----|----|---|
| 1 |  00 | 1 | 1 |  0 |  1 | 0 |

- **P = 1** – segment is present in memory
- **DPL = 0** – kernel privilege (ring 0)
- **S = 1** – this is a code or data segment (not a system segment)
- **E = 1** – executable (code segment)
- **RW = 1** – segment is readable

**Access byte for data (0x92):** same as above but **E = 0** (not executable) and **RW = 1** means writable.

**Granularity byte flags (high nibble = 0xC):**

| G | DB | L | AVL |
|---|----|---|-----|
| 1 |  1 | 0 |  0  |

- **G = 1** – limit is in 4 KB blocks (so limit 0xFFFFF × 4096 = 4 GB)
- **DB = 1** – 32-bit protected mode

### Loading the GDT (`lgdt`)

The CPU is told about the GDT via the GDTR register, loaded with the `lgdt` instruction. GDTR holds a 6-byte structure:

```
Bytes 0–1 : Size of GDT in bytes – 1
Bytes 2–5 : Linear (physical) address of the GDT
```

After `lgdt`, the segment registers (CS, DS, ES, FS, GS, SS) still point to the old GDT entries. We must reload them:

- **DS, ES, FS, GS, SS** can be updated with `mov ax, 0x10` (data selector).
- **CS** cannot be changed with `mov`. We use a **far jump** (`jmp 0x08:.reload_cs`) which simultaneously sets CS to 0x08 and jumps to the label.

This is implemented in `src/arch/i386/gdt_flush.asm`.

### Files

| File                           | Purpose                                  |
|--------------------------------|------------------------------------------|
| `include/gdt.h`                | Struct definitions, selector constants, declaration of `gdt_init()` |
| `src/gdt.c`                    | Fills the GDT entries, sets up the GDTR pointer, calls `gdt_flush` |
| `src/arch/i386/gdt_flush.asm`  | Executes `lgdt`, reloads all segment registers with a far jump for CS |

---

## 2. VGA Text-Mode Terminal

### Background

When a PC boots into 32-bit protected mode (without a graphical framebuffer), the display is controlled by the **VGA text mode** hardware. The simplest way to write text to the screen is to write directly into the VGA text buffer at physical address **0xB8000**.

### VGA Buffer Layout

- The buffer is an array of 16-bit values, one per character cell.
- The screen is **80 columns × 25 rows** = 2000 cells.
- Cell index: `row * 80 + col`
- Each 16-bit cell:
  - Low byte: ASCII character code
  - High byte: colour attribute
    - Bits 3:0 – foreground colour (VGA 4-bit palette, 0–15)
    - Bits 7:4 – background colour

### Features Implemented

- **`terminal_init()`** – clears the screen (fills all cells with `' '`) and resets the cursor.
- **`terminal_putchar(c)`** – writes one character; handles `\n` (newline) and `\r` (carriage return).
- **`terminal_write(str)`** – iterates a null-terminated string and calls `terminal_putchar`.
- **`terminal_writecolor(str, fg, bg)`** – writes a string in a specified colour, then restores the previous colour.
- **`terminal_setcolor(fg, bg)`** – changes the active colour for all subsequent output.
- **Scrolling** – when the cursor would move past row 24, every row is shifted up by one and the bottom row is cleared.

### Files

| File                | Purpose                                       |
|---------------------|-----------------------------------------------|
| `include/terminal.h`| Public API: function declarations, `vga_color_t` enum |
| `src/terminal.c`    | VGA buffer access, cursor management, scrolling |

---

## 3. printf Implementation

`printf` is implemented in `src/stdio.c`. It uses variadic arguments (`va_list` / `va_arg` from `<stdarg.h>`) and supports the following format specifiers:

| Specifier | Meaning                                 |
|-----------|-----------------------------------------|
| `%c`      | Single character                        |
| `%s`      | Null-terminated string                  |
| `%d`      | Signed 32-bit decimal integer           |
| `%u`      | Unsigned 32-bit decimal integer         |
| `%x`      | Unsigned 32-bit hexadecimal (lowercase) |
| `%%`      | Literal `%`                             |

`printf` calls `putchar`, which calls `terminal_putchar`, so all output goes to the VGA buffer.

---

## 4. Kernel Entry Point

`src/multiboot2.asm` is the true entry point (`_start`):
1. Clears interrupts (`cli`).
2. Sets up a 64 KB stack.
3. Pushes the Multiboot2 magic number (EAX) and info pointer (EBX) onto the stack.
4. Calls `main()` in `src/kernel.c`.

`main()` performs initialisation in the following order:
1. `gdt_init()` – installs the GDT before anything else.
2. `terminal_init()` – clears the screen.
3. `printf("Hello World\n")` – prints the required greeting.
4. Halts the CPU in an infinite loop (`cli; hlt`).

---

## 5. Build System

New source files added to `CMakeLists.txt`:

```
src/multiboot2.asm          – Multiboot2 header + _start
src/kernel.c                – Kernel entry point
src/gdt.c                   – GDT table + init
src/arch/i386/gdt_flush.asm – lgdt + segment register reload
src/terminal.c              – VGA terminal driver
src/stdio.c                 – printf / putchar / print
```

The project targets **i386 (32-bit protected mode)** with `-m32 -march=i386`, no standard library (`-nostdlib -nostdinc`), and position-independent executable (`-fPIE -pie`) with our custom linker script.

---

## 6. Testing

### Option A – Inside the devcontainer (recommended)

Open the repo in VS Code and choose **"Reopen in Container"**. The devcontainer has all required tools (cross-compiler, NASM, Limine, QEMU) pre-installed.

```bash
# Build the kernel
cmake --build .

# Create bootable ISO (requires Limine at /usr/local/limine)
cmake --build . --target uiaos-create-image

# Run in QEMU with GDB server on port 1234
./scripts/start_qemu.sh kernel.bin disk.iso
```

### Option B – Outside the devcontainer (WSL2 / native Linux)

Requires: `nasm`, `gcc-multilib`, `grub-pc-bin`, `grub-common`, `xorriso`, `qemu-system-x86`.

```bash
# Build from the repo root
cmake -S src/06_OSDev_06 -B build/06_OSDev_06
cmake --build build/06_OSDev_06

# Create a bootable ISO using GRUB
mkdir -p /tmp/isodir/boot/grub
cp build/06_OSDev_06/kernel.bin /tmp/isodir/boot/kernel.bin
echo 'set timeout=0
menuentry "OSDev06" { multiboot2 /boot/kernel.bin; boot }' \
    > /tmp/isodir/boot/grub/grub.cfg
grub-mkrescue -o /tmp/os.iso /tmp/isodir

# Run in QEMU
qemu-system-i386 -cdrom /tmp/os.iso -m 64M
```

Expected output: **`Hello World`** in white text on a black screen (top-left corner).
