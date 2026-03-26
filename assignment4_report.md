# Assignment 4 Report – Memory Management and PIT

**Course:** IKT218 – Operating System Development
**Group:** 06_OSDev_06
**Assignment:** 4 – Memory and PIT

---

## Overview

This report documents two additions to the UiA OS kernel:

1. **Memory Management** – heap allocator (`malloc`/`free`) and identity-mapped paging
2. **Programmable Interval Timer (PIT)** – 1000 Hz tick counter and two sleep functions

---

## Part 1: Memory Management

### The `end` Symbol

The linker script (`src/arch/i386/linker.ld`) defines a symbol called `end` at the very end of the `.bss` section:

```
end = .; _end = .; __end = .;
```

This means `&end` is the first byte of physical memory that the kernel binary does **not** occupy. We use it as the start of the heap:

```c
extern uint32_t end;
init_kernel_memory(&end);
```

### Heap Allocator

Each allocation is preceded by a 12-byte header (`heap_block_t`):

```c
typedef struct heap_block {
    size_t            size;    // bytes in the data area
    uint32_t          is_free; // 1 = free, 0 = in use
    struct heap_block *next;   // next block in list
} heap_block_t;
```

The heap is a **linked list** of these blocks. All allocations are 16-byte aligned.

#### `malloc` – first-fit algorithm

1. Walk the list looking for a free block with `size >= requested`.
2. If one is found, mark it `is_free = 0` and return a pointer to the data area (right after the header).
3. If none is found, carve a new block from the end of the heap by advancing `heap_current_end`.

```
[header 12B][  data area  ][header 12B][  data area  ]...
 ^                          ^
 heap_head                  second block
```

The pointer returned to the caller points to the **data area**, not the header.

#### `free` – mark and coalesce

1. Step back one header-width from the pointer to recover the `heap_block_t`.
2. Set `is_free = 1`.
3. Walk the whole list merging adjacent free blocks:

```c
if (curr->is_free && curr->next->is_free) {
    curr->size += sizeof(heap_block_t) + curr->next->size;
    curr->next  = curr->next->next;
}
```

This prevents fragmentation from building up over time.

#### Verified behaviour (from QEMU output)

```
malloc(12345) -> 0x11801c
malloc(54321) -> 0x11b068
malloc(13331) -> 0x1284b4
free(0x11801c) OK
malloc(100)   -> 0x11801c   ← reused the freed block
```

The last line confirms that `free` correctly returns memory to the pool and `malloc` reuses it.

### Paging

We implement a simple **identity map**: every virtual address equals its physical address. This is the minimum needed to enable paging without breaking any existing code.

We use **4 MB pages** (PSE extension) so the entire page directory fits in one 4 KB structure (1024 entries × 4 bytes each).

Each page directory entry:
```
Bits 31:22 – physical base address (top 10 bits of a 4 MB-aligned address)
Bit  7  (PS)  – 1: 4 MB page
Bit  1  (R/W) – 1: read/write
Bit  0  (P)   – 1: present
```

Enabling paging requires three steps:
1. Load `CR3` with the address of the page directory.
2. Set `CR4.PSE` (bit 4) to enable 4 MB page support.
3. Set `CR0.PG` (bit 31) to turn paging on.

```c
__asm__ volatile (
    "mov %0, %%cr3\n\t"
    "mov %%cr4, %%eax\n\t"
    "or  $0x10, %%eax\n\t"       // CR4.PSE = 1
    "mov %%eax, %%cr4\n\t"
    "mov %%cr0, %%eax\n\t"
    "or  $0x80000000, %%eax\n\t" // CR0.PG = 1
    "mov %%eax, %%cr0\n\t"
    : : "r"((uint32_t)page_directory) : "eax"
);
```

After this, the CPU translates every address through the page directory — but since we identity-mapped everything, the result is the same physical address.

### `print_memory_layout`

Prints the kernel load address, size, heap start, bytes used, and the heap limit:

```
--- Memory Layout ---
  Kernel load address : 0x100000
  Kernel end  (linker): 0x118000   (approximate)
  Kernel size         : ...
  Heap start          : 0x118000
  Heap used           : 0 bytes    (before any malloc)
  Heap limit          : 0x3f00000
---------------------
```

### Files

| File | Purpose |
|------|---------|
| `include/memory.h` | `init_kernel_memory`, `init_paging`, `print_memory_layout`, `malloc`, `free` |
| `src/memory.c` | Full implementation |

---

## Part 2: Programmable Interval Timer (PIT)

### Background

The 8253/8254 PIT is a chip with three independent timer channels. **Channel 0** is wired to **IRQ0** and is the traditional system timer. We configure it to fire at **1000 Hz** (one interrupt per millisecond), giving us a 1 ms resolution tick counter.

### Configuration

The PIT base frequency is **1 193 180 Hz**. To get 1000 Hz we use a divisor of:

```
DIVIDER = 1 193 180 / 1000 = 1193
```

The command byte `0x34` means:
- Bits 7:6 = `00` – channel 0
- Bits 5:4 = `11` – access mode: lo byte then hi byte
- Bits 3:1 = `010` – mode 2 (rate generator / periodic)
- Bit 0 = `0` – binary counting

```c
outb(0x43, 0x34);
outb(0x40, DIVIDER & 0xFF);         // low byte
outb(0x40, (DIVIDER >> 8) & 0xFF);  // high byte
```

### Tick Counter

The IRQ0 handler increments a `volatile` global:

```c
static volatile uint32_t tick_count = 0;

static void pit_irq_handler(registers_t *regs) {
    tick_count++;
}
```

`volatile` is essential here. Without it, the compiler would cache `tick_count` in a register and the sleep loops would never observe it changing.

### `sleep_busy`

Busy-waits by watching `tick_count` advance one tick at a time:

```c
void sleep_busy(uint32_t milliseconds) {
    uint32_t start   = tick_count;
    uint32_t elapsed = 0;
    while (elapsed < milliseconds * TICKS_PER_MS) {
        while (tick_count == start + elapsed) { /* spin */ }
        elapsed++;
    }
}
```

- **Pros:** very precise, unaffected by interrupt latency
- **Cons:** CPU is 100% busy the entire time

### `sleep_interrupt`

Halts the CPU between ticks using `hlt`:

```c
void sleep_interrupt(uint32_t milliseconds) {
    uint32_t end_tick = tick_count + milliseconds * TICKS_PER_MS;
    while (tick_count < end_tick) {
        __asm__ volatile ("sti\n\thlt");
    }
}
```

`sti` enables interrupts so the CPU can wake up, then `hlt` suspends it until the next IRQ fires (IRQ0 or any other). The CPU then checks if `tick_count >= end_tick`; if not, it halts again.

- **Pros:** CPU is idle between ticks, very low power/resource usage
- **Cons:** slightly less precise because it wakes on *any* interrupt, not just IRQ0

### Verified behaviour (from QEMU output)

```
[0]: Sleeping with busy-waiting (HIGH CPU).
[0]: Slept using busy-waiting.
[1]: Sleeping with interrupts (LOW CPU).
[1]: Slept using interrupts.
[2]: Sleeping with busy-waiting (HIGH CPU).
...
```

The counter advances every ~1 second as expected.

### Files

| File | Purpose |
|------|---------|
| `include/pit.h` | PIT constants, function declarations |
| `src/pit.c` | `init_pit`, `sleep_busy`, `sleep_interrupt`, tick handler |

---

## Testing

```bash
cmake -S src/06_OSDev_06 -B build/06_OSDev_06
cmake --build build/06_OSDev_06
# create ISO as in previous assignments, then:
qemu-system-i386 -cdrom /tmp/os.iso -m 64M
```

Expected output: memory layout printed, three malloc addresses shown with the fourth reusing the freed block, then PIT counter incrementing every second alternating between busy-wait and interrupt sleep.
