#include <gdt.h>

/*
 * We define three descriptors:
 *   0 - NULL descriptor  (required; the CPU uses index 0 as "no segment")
 *   1 - Kernel code      (selector 0x08)
 *   2 - Kernel data      (selector 0x10)
 *
 * Both code and data segments use a flat model (base = 0, limit = 4 GB)
 * so the kernel can address the entire 32-bit address space.
 */
#define GDT_ENTRIES 3

static gdt_entry_t gdt[GDT_ENTRIES]; /* The actual table of descriptors */
gdt_ptr_t gp;                        /* Loaded by lgdt in gdt_flush.asm  */

/* Implemented in src/arch/i386/gdt_flush.asm */
extern void gdt_flush(void);

/*
 * gdt_set_entry - fill one GDT slot
 *
 * @i      : slot index (0 = NULL, 1 = code, 2 = data, …)
 * @base   : segment base address (32-bit linear address)
 * @limit  : segment limit (20 significant bits; set to 0xFFFFFFFF for 4 GB flat)
 * @access : access byte (P, DPL, S, E, DC, RW, A fields)
 * @gran   : high nibble = flags (G, DB, L, AVL); low nibble unused here
 *           (the low nibble of the granularity byte comes from limit bits 19:16)
 */
static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    /* Base address split across three fields */
    gdt[i].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[i].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high   = (uint8_t)((base >> 24) & 0xFF);

    /* Limit split: low 16 bits + upper 4 bits packed into granularity byte */
    gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[i].granularity = (uint8_t)((limit >> 16) & 0x0F) /* limit bits 19:16 */
                       | (gran & 0xF0);                   /* flags (G, DB, L, AVL) */

    gdt[i].access = access;
}

/*
 * gdt_init - set up the GDT and activate it
 *
 * Builds the three descriptors, fills the GDTR pointer, then calls
 * gdt_flush (assembly) which executes lgdt and reloads all segment registers.
 */
void gdt_init(void)
{
    /* GDTR: limit is size of table minus 1, base is its address */
    gp.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gp.base  = (uint32_t)&gdt;

    /*
     * Access bytes used below:
     *   0x9A = 1001 1010 → P=1, DPL=0, S=1, E=1 (code), DC=0, RW=1 (readable), A=0
     *   0x92 = 1001 0010 → P=1, DPL=0, S=1, E=0 (data), DC=0, RW=1 (writable), A=0
     *
     * Granularity flag byte 0xCF:
     *   High nibble 0xC = 1100 → G=1 (4 KB pages), DB=1 (32-bit), L=0, AVL=0
     *   Low  nibble 0xF → limit bits 19:16 (gives full 4 GB with G=1)
     */

    /* Entry 0: NULL descriptor – all zeros, never used directly */
    gdt_set_entry(0, 0, 0, 0x00, 0x00);

    /* Entry 1: Kernel code segment – ring 0, execute/read, flat 4 GB */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Entry 2: Kernel data segment – ring 0, read/write, flat 4 GB */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Hand the new GDT to the CPU and reload segment registers */
    gdt_flush();
}
