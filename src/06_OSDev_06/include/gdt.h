#pragma once

#include <libc/stdint.h>

/*
 * GDT Entry (Segment Descriptor) - 8 bytes
 *
 * Each entry describes a memory segment to the CPU.
 * The fields are split across the 8 bytes in a non-obvious way
 * due to historical x86 backwards compatibility reasons.
 *
 * Bit layout of the 8-byte entry:
 *   [15: 0] limit_low    - bits 15:0 of the segment limit
 *   [31:16] base_low     - bits 15:0 of the base address
 *   [39:32] base_middle  - bits 23:16 of the base address
 *   [47:40] access       - access/type flags (see below)
 *   [51:48] granularity  - bits 19:16 of limit (low nibble) + flags (high nibble)
 *   [63:56] base_high    - bits 31:24 of the base address
 *
 * Access byte bits:
 *   Bit 7 (P)  : Segment present (must be 1 for valid segments)
 *   Bit 6:5 (DPL): Descriptor privilege level (0 = kernel, 3 = user)
 *   Bit 4 (S)  : Descriptor type (1 = code/data, 0 = system)
 *   Bit 3 (E)  : Executable (1 = code segment, 0 = data segment)
 *   Bit 2 (DC) : Direction/Conforming
 *   Bit 1 (RW) : Readable (code) / Writable (data)
 *   Bit 0 (A)  : Accessed (CPU sets this; leave 0)
 *
 * Granularity byte (high nibble) bits:
 *   Bit 7 (G)  : Granularity (1 = limit in 4KB blocks, 0 = limit in bytes)
 *   Bit 6 (DB) : Size flag (1 = 32-bit protected mode, 0 = 16-bit)
 *   Bit 5 (L)  : Long mode (1 = 64-bit segment; leave 0 for i386)
 *   Bit 4 (AVL): Available for software use
 */
typedef struct {
    uint16_t limit_low;   /* Segment limit bits 15:0  */
    uint16_t base_low;    /* Base address bits 15:0   */
    uint8_t  base_middle; /* Base address bits 23:16  */
    uint8_t  access;      /* Access flags             */
    uint8_t  granularity; /* Limit bits 19:16 + flags */
    uint8_t  base_high;   /* Base address bits 31:24  */
} __attribute__((packed)) gdt_entry_t;

/*
 * GDT Pointer (GDTR register value)
 *
 * This 6-byte structure is what the lgdt instruction reads.
 * 'limit' is the size of the GDT in bytes minus 1.
 * 'base'  is the linear (physical) address of the GDT.
 */
typedef struct {
    uint16_t limit; /* GDT size in bytes - 1 */
    uint32_t base;  /* Linear address of GDT */
} __attribute__((packed)) gdt_ptr_t;

/*
 * Segment selectors used after loading the GDT.
 * Format: bits 15:3 = descriptor index, bit 2 = TI (0=GDT), bits 1:0 = RPL
 */
#define GDT_SELECTOR_NULL 0x00 /* Index 0 - never used directly */
#define GDT_SELECTOR_CODE 0x08 /* Index 1 - kernel code segment  */
#define GDT_SELECTOR_DATA 0x10 /* Index 2 - kernel data segment  */

/* Initialise the GDT and reload all segment registers. */
void gdt_init(void);
