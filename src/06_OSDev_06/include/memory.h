#pragma once

#include <libc/stdint.h>
#include <libc/stddef.h>

/*
 * init_kernel_memory - initialise the heap allocator
 * @kernel_end : address of the 'end' symbol from the linker script.
 *               This is the first byte of free memory after the kernel binary.
 */
void init_kernel_memory(uint32_t *kernel_end);

/*
 * init_paging - identity-map the full 4 GB address space and enable paging
 *
 * Uses 4 MB pages (PSE) so only a single 4 KB page directory is needed.
 * Virtual address == physical address for all memory (flat identity map).
 */
void init_paging(void);

/*
 * print_memory_layout - print kernel and heap addresses to the terminal
 */
void print_memory_layout(void);

/*
 * malloc - allocate 'size' bytes from the kernel heap
 * Returns a pointer to the allocated memory, or NULL on failure.
 */
void *malloc(size_t size);

/*
 * free - return a previously malloc'd block to the heap
 * Adjacent free blocks are merged (coalesced) to reduce fragmentation.
 */
void free(void *ptr);
