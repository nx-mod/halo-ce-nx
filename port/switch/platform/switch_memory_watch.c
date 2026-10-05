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
often while it keeps not changing (every 1 or 2 frames): vertex
data the game rewrites each frame stays checked every frame, static
textures cost almost nothing, and a game write to a page that had been
static is still seen within 2 frames. Vertex data never backs off
(memory_watch_generation_every_frame): one stale frame of it is a decal
drawn late or in the wrong place. Writes the host makes (file reads
into guest memory, xbox_files.c) force a recheck at once, as on the Vita
- which, unlike this, never sees writes made by game code.
*/

#include "platform.h"

#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)
/* 2 frames: at 32 (about a second at 30 fps) text whose glyphs loaded
into a page that had backed off showed garbled for that long */
#define WATCH_MAXIMUM_INTERVAL_SHIFT 1

/* the two costs that matter, settings in config.toml (debug.memory_watch_*)
so they can be tried without a rebuild */
int config_boolean(const char *name);
long config_integer(const char *name);

static int maximum_interval_shift = WATCH_MAXIMUM_INTERVAL_SHIFT;
static int vertex_every_frame = 1;

/* The tag, texture and sound caches (source/cache/physical_memory_map.c,
48 MB between them, most of what the renderer watches) change only when a
file is read into them, and those reads force a recheck at once
(memory_watch_prepare_write). Their pages back off much further and are not
part of the every-frame vertex check: hashing them as often as the memory
the game writes at runtime (decals, the glyph texture) cost a re-read of
tens of MB a frame, which the first playable build, backing everything off
to 32 frames, never paid. debug.memory_watch_file_cache_shift sets it. */
#define FILE_CACHE_INTERVAL_SHIFT 5 /* 32 frames */
static int file_cache_interval_shift = FILE_CACHE_INTERVAL_SHIFT;

void *physical_memory_get_tag_cache_base_address(void);
void *physical_memory_get_texture_cache_base_address(void);
void *physical_memory_get_sound_cache_base_address(void);

static struct
{
	unsigned long first, last; /* pages, inclusive */
} file_caches[3];
static int file_caches_known;

static void find_file_caches(void)
{
	static const unsigned long sizes[3] = {0x1600000, 0x1600000, 0x400000};
	void *bases[3];
	int index;

	bases[0] = physical_memory_get_tag_cache_base_address();
	bases[1] = physical_memory_get_texture_cache_base_address();
	bases[2] = physical_memory_get_sound_cache_base_address();
	for (index = 0; index < 3; index++)
	{
		unsigned long address = (unsigned long)bases[index];

		/* (not allocated yet: asked again next time) */
		if (!address || !platform_is_contiguous(bases[index]))
			return;
		file_caches[index].first = (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
		file_caches[index].last = (address - PLATFORM_CONTIGUOUS_BASE + sizes[index] - 1) / WATCH_PAGE_SIZE;
	}
	file_caches_known = 1;
}

static int in_file_cache(unsigned long page)
{
	int index;

	for (index = 0; index < 3; index++)
	{
		if (page >= file_caches[index].first && page <= file_caches[index].last)
			return 1;
	}
	return 0;
}

static unsigned long page_generation[WATCH_PAGE_COUNT];
static unsigned long long page_hash[WATCH_PAGE_COUNT];
static unsigned long page_next_check[WATCH_PAGE_COUNT];
static unsigned char page_interval_shift[WATCH_PAGE_COUNT];
static unsigned long page_checked_frame[WATCH_PAGE_COUNT];
static unsigned long watch_serial = 1;

/* what the hashing costs, logged every STATISTICS_FRAMES presents: pages
hashed and the time spent, for textures and vertex data apart */
#define STATISTICS_FRAMES 300
static unsigned long statistics_pages[2];
static unsigned long long statistics_ticks[2];

static unsigned long long ticks_now(void)
{
	unsigned long long tick;

	__asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (tick));
	return tick;
}
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

static unsigned long generation_of(unsigned long address, unsigned long size, int every_frame)
{
	unsigned long first, last, page, newest = 0, hashed = 0;
	unsigned long long start;

	if (!page_range(address, size, &first, &last))
		return 0;
	if (!file_caches_known)
		find_file_caches();
	start = ticks_now();
	for (page = first; page <= last; page++)
	{
		int file_cache = file_caches_known && in_file_cache(page);

		if (page_next_check[page] <= watch_frame ||
			(every_frame && vertex_every_frame && !file_cache && page_checked_frame[page] != watch_frame))
		{
			hashed++;
			unsigned long long hash = hash_page(
				(const unsigned long long *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE));

			page_checked_frame[page] = watch_frame;
			if (hash != page_hash[page] || !page_generation[page])
			{
				page_hash[page] = hash;
				page_generation[page] = __atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
				page_interval_shift[page] = 0;
			}
			else if (page_interval_shift[page] < (file_cache ? file_cache_interval_shift : maximum_interval_shift))
			{
				page_interval_shift[page]++;
			}
			page_next_check[page] = watch_frame + (1UL << page_interval_shift[page]);
		}
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	if (hashed)
	{
		statistics_pages[every_frame != 0] += hashed;
		statistics_ticks[every_frame != 0] += ticks_now() - start;
	}
	return newest;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	return generation_of(address, size, 0);
}

/* the same, but no page backs off: hashed once every frame it is asked
about. For vertex data (d3d8_gl.c's mirror): a decal written into a page
that had backed off was drawn from the old contents for a frame - late,
missing, or briefly somewhere else while turning */
unsigned long memory_watch_generation_every_frame(unsigned long address, unsigned long size)
{
	return generation_of(address, size, 1);
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
	static int configured;

	if (!configured)
	{
		long shift = config_integer("debug.memory_watch_shift");
		long file_cache_shift = config_integer("debug.memory_watch_file_cache_shift");

		configured = 1;
		if (shift >= 0 && shift <= 8)
			maximum_interval_shift = (int)shift;
		if (file_cache_shift >= 0 && file_cache_shift <= 8)
			file_cache_interval_shift = (int)file_cache_shift;
		vertex_every_frame = config_boolean("debug.memory_watch_vertices");
		platform_log("memory watch: pages back off to every %d frames, file caches to every %d; vertex data %s",
			1 << maximum_interval_shift, 1 << file_cache_interval_shift,
			vertex_every_frame ? "checked every frame" : "backs off too");
	}
	if (!(watch_frame % STATISTICS_FRAMES))
	{
		/* the system counter runs at 19.2 MHz */
		const double ms_per_tick = 1000.0 / 19200000.0, mb_per_page = (double)WATCH_PAGE_SIZE / (1024.0 * 1024.0);

		platform_log("memory watch: per frame, textures %.2f MB in %.2f ms, vertex data %.2f MB in %.2f ms",
			statistics_pages[0] * mb_per_page / STATISTICS_FRAMES,
			statistics_ticks[0] * ms_per_tick / STATISTICS_FRAMES,
			statistics_pages[1] * mb_per_page / STATISTICS_FRAMES,
			statistics_ticks[1] * ms_per_tick / STATISTICS_FRAMES);
		statistics_pages[0] = statistics_pages[1] = 0;
		statistics_ticks[0] = statistics_ticks[1] = 0;
	}
	watch_frame++;
	__atomic_add_fetch(&watch_serial, 1, __ATOMIC_RELAXED);
}
