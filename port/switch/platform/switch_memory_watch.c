/*
SWITCH_MEMORY_WATCH.C

Guest memory write tracking (port/linux/src/memory_watch.c's interface)
for the renderer's caches: the GPU mirror of vertex data in the
contiguous window (d3d8_gl.c) and converted textures (xbox_textures.c).

Linux write-protects cached pages and catches the fault; Switch homebrew
has no user-space page faults to catch. A stub that called every page
written on every query made the renderer re-upload every vertex page and
re-convert every texture on every draw: about 24 ms a draw, 5 frames a
second at the main menu.

Instead each page's contents are hashed, and its generation moves only
when the hash does. A page is hashed at most once a frame, and less
often while it keeps not changing (every 1, 2, 4 frames): vertex
data the game rewrites each frame stays checked every frame, static
textures cost almost nothing, and a game write to a page that had been
static is still seen within 4 frames. Writes the host makes (file reads
into guest memory, xbox_files.c) force a recheck at once, as on the Vita
- which, unlike this, never sees writes made by game code.
*/

#include "platform.h"

#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)
/* 4 frames: at 32 (about a second at 30 fps) text whose glyphs loaded
into a page that had backed off showed garbled for that long */
#define WATCH_MAXIMUM_INTERVAL_SHIFT 2

static unsigned long page_generation[WATCH_PAGE_COUNT];
static unsigned long long page_hash[WATCH_PAGE_COUNT];
static unsigned long page_next_check[WATCH_PAGE_COUNT];
static unsigned char page_interval_shift[WATCH_PAGE_COUNT];
static unsigned long watch_serial = 1;
static unsigned long watch_frame = 1;

/* four independent lanes so the multiplies overlap */
static unsigned long long hash_page(const unsigned long long *words)
{
	unsigned long long a = 0x9e3779b97f4a7c15ULL, b = 0xc2b2ae3d27d4eb4fULL;
	unsigned long long c = 0x165667b19e3779f9ULL, d = 0x27d4eb2f165667c5ULL;
	unsigned long index;

	for (index = 0; index < WATCH_PAGE_SIZE / sizeof(*words); index += 4)
	{
		a = (a ^ words[index + 0]) * 0x100000001b3ULL;
		b = (b ^ words[index + 1]) * 0x100000001b3ULL;
		c = (c ^ words[index + 2]) * 0x100000001b3ULL;
		d = (d ^ words[index + 3]) * 0x100000001b3ULL;
	}
	return a ^ (b << 1) ^ (c << 2) ^ (d << 3) ^ (b >> 63) ^ (c >> 62) ^ (d >> 61);
}

static int page_range(unsigned long address, unsigned long size, unsigned long *first, unsigned long *last)
{
	if (!size || !platform_is_contiguous((void *)address))
		return 0;
	*first = (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
	*last = (address - PLATFORM_CONTIGUOUS_BASE + size - 1) / WATCH_PAGE_SIZE;
	if (*last >= WATCH_PAGE_COUNT)
		*last = WATCH_PAGE_COUNT - 1;
	return 1;
}

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long first, last, page, newest = 0;

	if (!page_range(address, size, &first, &last))
		return 0;
	for (page = first; page <= last; page++)
	{
		if (page_next_check[page] <= watch_frame)
		{
			unsigned long long hash = hash_page(
				(const unsigned long long *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE));

			if (hash != page_hash[page] || !page_generation[page])
			{
				page_hash[page] = hash;
				page_generation[page] = __atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
				page_interval_shift[page] = 0;
			}
			else if (page_interval_shift[page] < WATCH_MAXIMUM_INTERVAL_SHIFT)
			{
				page_interval_shift[page]++;
			}
			page_next_check[page] = watch_frame + (1UL << page_interval_shift[page]);
		}
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

unsigned long memory_watch_serial(void)
{
	return __atomic_load_n(&watch_serial, __ATOMIC_RELAXED);
}

/* the host is about to write, or has just written, into the range: hash
it again at its next use */
static void force_recheck(void *address, unsigned long size)
{
	unsigned long first, last, page;

	if (!page_range((unsigned long)address, size, &first, &last))
		return;
	for (page = first; page <= last; page++)
	{
		page_next_check[page] = 0;
		page_interval_shift[page] = 0;
	}
	__atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	force_recheck(address, size);
}

void memory_watch_forget(void *address, unsigned long size)
{
	force_recheck(address, size);
}

/* d3d8_gl.c, once a presented frame: pages due a recheck get one, and the
serial moves so xbox_textures.c's recent-texture shortcut lasts a frame */
void memory_watch_frame(void)
{
	watch_frame++;
	__atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
}
