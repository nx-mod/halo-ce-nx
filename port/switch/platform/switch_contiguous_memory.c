/*
SWITCH_CONTIGUOUS_MEMORY.C

The Xbox contiguous ("physical") memory window (port/linux/src/
platform.h's own comment): Direct3D resources (d3d8_resources.c) keep
their data's "physical address" in their Data field, and the real
Xbox's GPU would read it through that window directly.

This guest has no separate physical/virtual split at all (same
simplification already made for tags - PORTING.md's "Resolved" section:
"tags only need to be real pointers within this process, which they
are"), so the window is just a plain static array; the linker decides
where, same as the Vita's host-allocated block landing wherever it did
(platform.h's HALO_VITA branch, which HALO_SWITCH now shares) - hence
platform_contiguous_base is a runtime variable here too, not a
compile-time constant like Linux's real mmap(..., 0x80000000, ...).

Page-granular, top-down first-fit allocator - the same shape as
port/linux/src/xbox_memory.c's (not reused directly: that file's real
mmap/vita_host.h dependencies don't apply here), with pthread_mutex_lock
calls dropped since guest_pthread_stubs.c already makes them no-ops on
this single-threaded guest anyway.
*/

#include <string.h>

#include "platform.h"

#define PAGE_SIZE_BYTES 0x1000UL
#define PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / PAGE_SIZE_BYTES)

/* placed at a fixed address by guest.ld (.contiguous): saves record game
state addresses. The section is nobits (the "//" ends the assembler line
GCC writes, the way "#" does on x86), so 112 MB of zeroes stay out of
guest.elf. */
static unsigned char arena[PLATFORM_CONTIGUOUS_SIZE]
	__attribute__((aligned(PAGE_SIZE_BYTES), section(".contiguous_arena,\"aw\",%nobits//")));
unsigned long platform_contiguous_base;
static DWORD page_protection[PAGE_COUNT];
static unsigned long block_page_count[PAGE_COUNT];
static int initialized;

static void ensure_initialized(void)
{
	if (!initialized)
	{
		platform_contiguous_base = (unsigned long)arena;
		initialized = 1;
	}
}

BOOL platform_is_contiguous(const void *address)
{
	unsigned long value;

	ensure_initialized();
	value = (unsigned long)address;
	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static BOOL pages_free(unsigned long first, unsigned long count)
{
	unsigned long page;

	if (first + count > PAGE_COUNT)
		return FALSE;
	for (page = first; page < first + count; page++)
	{
		if (page_protection[page])
			return FALSE;
	}
	return TRUE;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect)
{
	unsigned long count = (size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
	unsigned long alignment_pages = alignment > PAGE_SIZE_BYTES ? alignment / PAGE_SIZE_BYTES : 1;
	unsigned long first = PAGE_COUNT;
	unsigned long page;
	void *address;

	ensure_initialized();
	if (!count)
		count = 1;
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;

	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
	{
		unsigned long wanted = physical_address / PAGE_SIZE_BYTES;

		if (pages_free(wanted, count))
			first = wanted;
	}
	else if (count <= PAGE_COUNT)
	{
		/* top-down first fit, like the Xbox contiguous allocator */
		unsigned long candidate = count <= PAGE_COUNT ? PAGE_COUNT - count : 0;

		for (;;)
		{
			candidate -= candidate % alignment_pages;
			if (pages_free(candidate, count))
			{
				first = candidate;
				break;
			}
			if (candidate == 0)
				break;
			candidate--;
		}
	}
	if (first == PAGE_COUNT)
		return NULL;

	address = (void *)(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
	memset(address, 0, count * PAGE_SIZE_BYTES);
	for (page = first; page < first + count; page++)
		page_protection[page] = protect;
	block_page_count[first] = count;
	return address;
}

void platform_contiguous_free(void *address)
{
	unsigned long first, count, page;

	if (!platform_is_contiguous(address))
		return;
	first = ((unsigned long)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
	count = block_page_count[first];
	for (page = first; page < first + count; page++)
		page_protection[page] = 0;
	block_page_count[first] = 0;
}
