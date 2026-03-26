; gdt_flush.asm
; Loads the new GDT into the GDTR register and reloads all segment registers.
;
; The CPU's segment registers still point to the old (bootloader) GDT
; after gdt_init() fills our table. We must:
;   1. Run `lgdt` to tell the CPU where our new GDT lives.
;   2. Reload DS/ES/FS/GS/SS with our data selector (0x10).
;   3. Reload CS with our code selector (0x08) via a far jump,
;      because mov cannot change CS directly.
;
; The C variable `gp` (of type gdt_ptr_t) is defined in gdt.c and
; holds the 6-byte GDTR value (limit + base) that lgdt reads.

[BITS 32]

global gdt_flush   ; expose to C as  extern void gdt_flush(void)
extern gp          ; gdt_ptr_t gp  from gdt.c

gdt_flush:
    lgdt [gp]       ; Load our GDT pointer into GDTR

    ; Reload all data-class segment registers with the data selector.
    ; Selector 0x10 = index 2 | TI=0 (GDT) | RPL=0 (ring 0)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; CS cannot be changed with mov; a far jump is required.
    ; The far jump flushes the instruction pipeline and sets CS to 0x08
    ; (index 1 | TI=0 | RPL=0).
    jmp 0x08:.reload_cs

.reload_cs:
    ret
