#include <memory.h>
#include <libc/stdio.h>

/*
 * Simple first-fit heap allocator
 *
 * The heap starts immediately after the kernel binary in physical memory
 * (at the address of the linker-script symbol 'end', passed to
 * init_kernel_memory). Each allocation is preceded by a heap_block_t header
 * that records the block's size and whether it is free.
 *
 * Layout of one allocated block:
 *
 *   [ heap_block_t header (12 bytes) ][ data area (size bytes) ]
 *                                      ^
 *                                      pointer returned to caller
 *
 * malloc  : first-fit search; if no suitable free block exists, the heap
 *           is extended by carving a new block from unallocated space.
 * free    : marks the block as free and merges adjacent free blocks.
 */

/* Maximum physical address the heap may grow to (must stay below 64 MB). */
#define HEAP_MAX 0x3F00000u   /* ~63 MB — leaves a small guard below 64 MB */

/* Alignment for all allocations (16 bytes satisfies most use-cases). */
#define HEAP_ALIGN 16u

typedef struct heap_block {
    size_t            size;    /* Bytes in the data area (not including header) */
    uint32_t          is_free; /* 1 = available for reuse, 0 = in use           */
    struct heap_block *next;   /* Next block in the linked list, or NULL        */
} heap_block_t;

static heap_block_t *heap_head        = NULL; /* First block in the list   */
static uint32_t      heap_start_addr  = 0;    /* Address of first block    */
static uint32_t      heap_current_end = 0;    /* Next unallocated address  */

/* ------------------------------------------------------------------ */

void init_kernel_memory(uint32_t *kernel_end)
{
    /* Align the heap start to HEAP_ALIGN bytes */
    heap_start_addr  = ((uint32_t)kernel_end + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
    heap_current_end = heap_start_addr;
    heap_head        = NULL;
}

/* ------------------------------------------------------------------ */

void *malloc(size_t size)
{
    if (size == 0) return NULL;

    /* Round up to the next multiple of HEAP_ALIGN */
    size = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);

    /* First-fit: search the existing block list for a free block */
    heap_block_t *block = heap_head;
    heap_block_t *prev  = NULL;

    while (block) {
        if (block->is_free && block->size >= size) {
            block->is_free = 0;
            return (void *)(block + 1);
        }
        prev  = block;
        block = block->next;
    }

    /* No suitable block found — carve a new one from unallocated space */
    uint32_t new_addr = heap_current_end;
    heap_current_end += (uint32_t)(sizeof(heap_block_t) + size);

    if (heap_current_end >= HEAP_MAX) {
        /* Out of memory */
        heap_current_end = new_addr;
        return NULL;
    }

    heap_block_t *new_block = (heap_block_t *)new_addr;
    new_block->size    = size;
    new_block->is_free = 0;
    new_block->next    = NULL;

    if (!heap_head) {
        heap_head = new_block;
    } else {
        prev->next = new_block;
    }

    return (void *)(new_block + 1);
}

/* ------------------------------------------------------------------ */

void free(void *ptr)
{
    if (!ptr) return;

    /* Recover the header that sits just before the data pointer */
    heap_block_t *block = (heap_block_t *)ptr - 1;
    block->is_free = 1;

    /* Coalesce: merge consecutive free blocks to reduce fragmentation */
    heap_block_t *curr = heap_head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            /* Absorb the next block into this one */
            curr->size += sizeof(heap_block_t) + curr->next->size;
            curr->next  = curr->next->next;
            /* Don't advance curr — there may be more free blocks to merge */
        } else {
            curr = curr->next;
        }
    }
}

/* ------------------------------------------------------------------ */

/*
 * init_paging - identity-map the full 4 GB address space using 4 MB pages
 *
 * A page directory has 1024 entries, each covering 4 MB with the PSE flag.
 * We map all 1024 entries so that virtual address == physical address for
 * every byte in the 32-bit address space. This is the simplest setup that
 * lets the kernel continue running after paging is enabled.
 *
 * Steps:
 *   1. Fill the page directory (each entry = physical base | flags).
 *   2. Load CR3 with the address of the page directory.
 *   3. Set CR4.PSE (bit 4) to enable 4 MB page support.
 *   4. Set CR0.PG  (bit 31) to enable paging.
 */

#define PD_ENTRIES 1024
#define PD_PRESENT 0x01u   /* Page is present in memory  */
#define PD_RW      0x02u   /* Page is read/write         */
#define PD_4MB     0x80u   /* Use 4 MB pages (PSE)       */

/* Align to 4 KB so CR3 can hold the address directly */
static uint32_t page_directory[PD_ENTRIES] __attribute__((aligned(4096)));

void init_paging(void)
{
    for (int i = 0; i < PD_ENTRIES; i++) {
        /* Each entry maps virtual [i*4MB .. (i+1)*4MB) → same physical range */
        page_directory[i] = (uint32_t)(i * 0x400000) | PD_PRESENT | PD_RW | PD_4MB;
    }

    __asm__ volatile (
        "mov %0, %%cr3\n\t"          /* Load page directory base address */
        "mov %%cr4, %%eax\n\t"
        "or  $0x00000010, %%eax\n\t" /* CR4.PSE = 1: enable 4 MB pages   */
        "mov %%eax, %%cr4\n\t"
        "mov %%cr0, %%eax\n\t"
        "or  $0x80000000, %%eax\n\t" /* CR0.PG  = 1: enable paging       */
        "mov %%eax, %%cr0\n\t"
        : : "r"((uint32_t)page_directory) : "eax"
    );
}

/* ------------------------------------------------------------------ */

void print_memory_layout(void)
{
    uint32_t kernel_size = heap_start_addr - 0x100000u;
    uint32_t heap_used   = heap_current_end - heap_start_addr;

    printf("--- Memory Layout ---\n");
    printf("  Kernel load address : 0x%x\n", 0x100000u);
    printf("  Kernel end  (linker): 0x%x\n", heap_start_addr);
    printf("  Kernel size         : %d bytes\n", kernel_size);
    printf("  Heap start          : 0x%x\n", heap_start_addr);
    printf("  Heap used           : %d bytes\n", heap_used);
    printf("  Heap limit          : 0x%x\n", HEAP_MAX);
    printf("---------------------\n");
}
