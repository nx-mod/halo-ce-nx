/*
HOST_MAIN.C

Loads port/switch/guest/guest.elf and runs it (PORTING.md's milestones).
Two real segments now (milestone 2): the executable one (.guest_header,
.text) placed via svcCreateCodeMemory + svcControlCodeMemory's
MapOwner/MapSlave dance (the mechanism nx-mapmem-poc validated - see its
commit history for the two gotchas: cache maintenance, and writing
through the owner view, not the pre-create source buffer), and the
writable one (.rodata, import table/names, .data, .bss, and a 32 MiB
heap - guest_syscall.c's mmap bump allocator hands it out) placed with
plain svcMapMemory, no CodeMemory dance needed since nothing in it ever
needs to execute.
*/

#include <elf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "../../include/switch_guest_abi.h"
#include "xiso.h"
#include "host_loading_text.h"

static FILE *g_log;
/* off while the loading text is on the console: log lines would print
under it. host.log still gets every line. */
static int g_console_echo;

/* not static - host_video.c logs eglSwapBuffers failures through this
same path, so they land in host.log alongside everything else rather
than wherever stderr alone goes */
void logf_both(const char *fmt, ...)
{
	va_list args;
	if (g_console_echo)
	{
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
	}
	if (g_log)
	{
		va_start(args, fmt);
		vfprintf(g_log, fmt, args);
		va_end(args);
		fflush(g_log);
		fsdevCommitDevice("sdmc");
	}
}

/* ---------- the host side of the guest's imports */

static void host_log_impl(const char *text)
{
	logf_both("[guest] %s\n", text);
}

/* length-explicit, not NUL-terminated - the guest's write()/writev()
(guest_syscall.c) route stdout/stderr here */
static void host_write_impl(const char *data, long long length)
{
	if (g_log)
	{
		fwrite(data, 1, (size_t)length, g_log);
		fflush(g_log);
		fsdevCommitDevice("sdmc");
	}
	if (g_console_echo)
		fwrite(data, 1, (size_t)length, stderr);
}

struct host_function
{
	const char *name;
	void *address;
};

/* the real GLES3 bridge: window/context (host_video.c, PORTING.md's
"unstub video" milestone), plus the 98 hostgl_<name> raw GL entry
points (host_gl_resolve.c, generated from build/switch/gen/gl_imports.list -
tools/switch_gl_resolve.py), resolved through host_gl_resolve's own
fallback the same way Android's host_loader.c falls back for its
identically-shaped hostgl_ names. */
int platform_video_initialize(unsigned long width, unsigned long height);
void platform_video_drawable_size(int *width, int *height);
void platform_video_swap(void);
void platform_pump_events(void);
void host_gl_get_string(unsigned int name, int index, char *buffer, unsigned int size);
int host_gl_has_extension(const char *name);
unsigned int host_gl_read_buffer_word(unsigned int buffer, unsigned int offset);
void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data);
void host_gl_fence_frame(unsigned int slot);
void host_gl_wait_frame(unsigned int slot);
void *host_gl_resolve(const char *name);

/* PORTING.md's "real file I/O" milestone: port/switch/host/source/
host_posix_io.c (raw fd open/read/write/close/lseek, under
guest_syscall.c's SYS_openat/read/write/close/lseek) and
host_posix_files.c (port/linux/src/posix_files.c, unmodified - its own
32-bit-both-sides posix_ulong already matches what xbox_files.c, now
part of SWITCH_PLATFORM_FILES, expects). Struct-pointer parameters are
declared `void *` here rather than pulling in posix.h's struct
definition - these are never called directly in this file, only
addressed, so the exact pointee type doesn't need to be visible. */
long host_open(const char *path, int flags, int mode);
long host_read(int fd, void *buf, unsigned long count);
long host_write_fd(int fd, const void *buf, unsigned long count);
long host_close(int fd);
long long host_lseek(int fd, long long offset, int whence);
long host_unlink(const char *path);
long host_rename(const char *from, const char *to);
long host_pread(int fd, void *buf, unsigned long count, long long offset);
long host_pwrite(int fd, const void *buf, unsigned long count, long long offset);
int posix_stat(const char *path, void *information);
int posix_fstat(int descriptor, void *information);
int posix_set_file_times(const char *path, unsigned int access_seconds, unsigned int access_nanoseconds,
	unsigned int modification_seconds, unsigned int modification_nanoseconds);
int posix_seek(int descriptor, int offset_low, int offset_high, int whence,
	unsigned int *position_low, unsigned int *position_high);
int posix_truncate(int descriptor, unsigned int size_low, unsigned int size_high);
int posix_disk_space(const char *path, unsigned int *free_low, unsigned int *free_high,
	unsigned int *total_low, unsigned int *total_high);
int posix_set_read_only(const char *path, int read_only);
int posix_make_directory(const char *path);
void *posix_directory_open(const char *path);
int posix_directory_next(void *directory, char *name, unsigned int name_size);
void posix_directory_close(void *directory);
int posix_find_entry_case_insensitive(const char *directory, const char *name, char *result,
	unsigned int result_size);

/* PORTING.md's "real threading" milestone: source/cache/cache_files_
windows.c's cache-file worker thread is genuinely load-bearing (see
host_threads.c's own header comment) - real libnx threadCreate/UEvent
underneath, not anything hand-rolled. */
long host_create_thread(unsigned int guest_entry, unsigned int guest_arg, unsigned int stack_mem,
	unsigned int stack_size, unsigned int tls_block);
long host_event_create(int auto_clear);
void host_event_signal(long handle);
void host_event_clear(long handle);
long host_event_wait(long handle, long long timeout_ns);
void *host_get_guest_tp(void);
void host_set_guest_tp(void *ptr);

/* PORTING.md's "wire in audio/controls" milestone: the single real
PadState libnx's pad.h is built around (host_input.c). */
void host_pad_read(unsigned long long *buttons, int *lx, int *ly, int *rx, int *ry);
int host_pad_connected(void);

/* same milestone: real audio output (host_audio.c) - libnx audout, fed
from a dedicated real guest thread running dsound_sdl.c's own mixer */
int host_audio_open(void);
void host_audio_write(const short *pcm);
int host_audio_frames_per_buffer(void);

static const struct host_function kHostFunctions[] = {
	{"host_log", (void *)host_log_impl},
	{"host_write", (void *)host_write_impl},
	{"platform_video_initialize", (void *)platform_video_initialize},
	{"platform_video_drawable_size", (void *)platform_video_drawable_size},
	{"platform_video_swap", (void *)platform_video_swap},
	{"platform_pump_events", (void *)platform_pump_events},
	{"host_gl_get_string", (void *)host_gl_get_string},
	{"host_gl_has_extension", (void *)host_gl_has_extension},
	{"host_gl_read_buffer_word", (void *)host_gl_read_buffer_word},
	{"host_gl_buffer_write", (void *)host_gl_buffer_write},
	{"host_gl_fence_frame", (void *)host_gl_fence_frame},
	{"host_gl_wait_frame", (void *)host_gl_wait_frame},
	{"host_open", (void *)host_open},
	{"host_read", (void *)host_read},
	{"host_write_fd", (void *)host_write_fd},
	{"host_close", (void *)host_close},
	{"host_lseek", (void *)host_lseek},
	{"host_unlink", (void *)host_unlink},
	{"host_rename", (void *)host_rename},
	{"host_pread", (void *)host_pread},
	{"host_pwrite", (void *)host_pwrite},
	{"host_loading_text_stop", (void *)host_loading_text_stop},
	{"host_video_configure", (void *)host_video_configure},
	{"host_pin_current_thread", (void *)host_pin_current_thread},
	{"host_mjx_decode", (void *)host_mjx_decode},
	{"posix_stat", (void *)posix_stat},
	{"posix_fstat", (void *)posix_fstat},
	{"posix_set_file_times", (void *)posix_set_file_times},
	{"posix_seek", (void *)posix_seek},
	{"posix_truncate", (void *)posix_truncate},
	{"posix_disk_space", (void *)posix_disk_space},
	{"posix_set_read_only", (void *)posix_set_read_only},
	{"posix_make_directory", (void *)posix_make_directory},
	{"posix_directory_open", (void *)posix_directory_open},
	{"posix_directory_next", (void *)posix_directory_next},
	{"posix_directory_close", (void *)posix_directory_close},
	{"posix_find_entry_case_insensitive", (void *)posix_find_entry_case_insensitive},
	{"host_create_thread", (void *)host_create_thread},
	{"host_event_create", (void *)host_event_create},
	{"host_event_signal", (void *)host_event_signal},
	{"host_event_clear", (void *)host_event_clear},
	{"host_event_wait", (void *)host_event_wait},
	{"host_get_guest_tp", (void *)host_get_guest_tp},
	{"host_set_guest_tp", (void *)host_set_guest_tp},
	{"host_pad_read", (void *)host_pad_read},
	{"host_pad_connected", (void *)host_pad_connected},
	{"host_audio_open", (void *)host_audio_open},
	{"host_audio_write", (void *)host_audio_write},
	{"host_audio_frames_per_buffer", (void *)host_audio_frames_per_buffer},
};

static void *resolve_import(const char *name)
{
	for (size_t i = 0; i < sizeof(kHostFunctions) / sizeof(kHostFunctions[0]); i++)
		if (!strcmp(kHostFunctions[i].name, name))
			return kHostFunctions[i].address;
	if (!strncmp(name, "hostgl_", 7))
		return host_gl_resolve(name);
	return NULL;
}

/* ---------- loading */

/* RAII would be nice; this is C. Everything that needs cleanup on every
exit path is tracked here and torn down once, at the bottom. */
struct guest_state
{
	unsigned char *file_data;
	void *data_heap;
	void *data_view; /* == data_heap's vaddr once mapped; NULL before */
	uint32_t data_vaddr, data_size;
	void *code_owner_src;
	void *code_owner_view;
	Handle code_handle;
	int code_handle_valid;
	uint32_t code_vaddr, code_size;
};

static void teardown(struct guest_state *gs)
{
	if (gs->data_view)
		svcUnmapMemory((void *)(uintptr_t)gs->data_vaddr, gs->data_heap, gs->data_size);
	free(gs->data_heap);
	if (gs->code_handle_valid)
	{
		svcControlCodeMemory(gs->code_handle, CodeMapOperation_UnmapSlave,
			(void *)(uintptr_t)gs->code_vaddr, gs->code_size, 0);
		if (gs->code_owner_view)
			svcControlCodeMemory(gs->code_handle, CodeMapOperation_UnmapOwner,
				gs->code_owner_view, gs->code_size, 0);
		svcCloseHandle(gs->code_handle);
	}
	free(gs->code_owner_src);
	free(gs->file_data);
}

static int load_and_run_guest(const char *path)
{
	struct guest_state gs = {0};
	FILE *file;
	long file_size;
	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdrs;
	Elf32_Phdr *exec_segment = NULL, *data_segment = NULL;
	const struct guest_header *header_in_file;
	Result rc;
	int missing_imports = 0;

	file = fopen(path, "rb");
	if (!file)
	{
		logf_both("can't open %s\n", path);
		return -1;
	}
	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	gs.file_data = malloc(file_size);
	if (!gs.file_data || fread(gs.file_data, 1, file_size, file) != (size_t)file_size)
	{
		logf_both("can't read %s\n", path);
		fclose(file);
		teardown(&gs);
		return -1;
	}
	fclose(file);
	logf_both("read guest.elf, %ld bytes\n", file_size);

	ehdr = (Elf32_Ehdr *)gs.file_data;
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) || ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
		ehdr->e_machine != EM_AARCH64 || ehdr->e_type != ET_EXEC)
	{
		logf_both("guest.elf is not an ILP32 AArch64 executable\n");
		teardown(&gs);
		return -1;
	}

	phdrs = (Elf32_Phdr *)(gs.file_data + ehdr->e_phoff);
	for (int i = 0; i < ehdr->e_phnum; i++)
	{
		if (phdrs[i].p_type != PT_LOAD)
			continue;
		if (phdrs[i].p_flags & PF_X)
			exec_segment = &phdrs[i];
		else
			data_segment = &phdrs[i];
	}
	if (!exec_segment || !data_segment)
	{
		logf_both("guest.elf needs exactly one executable and one non-executable PT_LOAD "
			"segment, found exec=%p data=%p\n", (void *)exec_segment, (void *)data_segment);
		teardown(&gs);
		return -1;
	}

	gs.code_vaddr = exec_segment->p_vaddr;
	gs.code_size = (exec_segment->p_memsz + 0xfff) & ~0xfffu;
	gs.data_vaddr = data_segment->p_vaddr;
	gs.data_size = (data_segment->p_memsz + 0xfff) & ~0xfffu;
	logf_both("exec segment: vaddr=0x%x filesz=0x%x memsz=0x%x\n",
		exec_segment->p_vaddr, exec_segment->p_filesz, exec_segment->p_memsz);
	logf_both("data segment: vaddr=0x%x filesz=0x%x memsz=0x%x (includes the guest heap)\n",
		data_segment->p_vaddr, data_segment->p_filesz, data_segment->p_memsz);

	/* ---------- the data segment: plain svcMapMemory, no CodeMemory dance */

	gs.data_heap = aligned_alloc(0x1000, gs.data_size);
	if (!gs.data_heap)
	{
		logf_both("aligned_alloc(0x%x) for the data segment failed\n", gs.data_size);
		teardown(&gs);
		return -1;
	}
	memset(gs.data_heap, 0, gs.data_size);
	memcpy(gs.data_heap, gs.file_data + data_segment->p_offset, data_segment->p_filesz);

	/* the header is link-time-constant content, already correct in the
	file at its linked offsets - read it from the file buffer rather
	than re-deriving addresses (it lives in the exec segment). */
	header_in_file = (const struct guest_header *)(gs.file_data + exec_segment->p_offset);
	if (header_in_file->magic != GUEST_MAGIC || header_in_file->abi_version != GUEST_ABI_VERSION)
	{
		logf_both("guest.elf's header doesn't match this host (magic/version)\n");
		teardown(&gs);
		return -1;
	}
	/* import_count holds the address of a uint32_t, not the count itself
	(switch_guest_abi.h: only known as a symbol's value at link time, not
	a constant this header's own static initializer could fold in) -
	dereference it the same way import_table/import_names get translated
	below, before logging or looping on it. Getting this wrong doesn't
	crash - it reads whatever raw pointer value was stored as a giant
	bogus loop count instead, which looks like a hang, not a crash. */
	{
		const uint32_t *count_ptr = (const uint32_t *)((char *)gs.data_heap +
			(header_in_file->import_count - gs.data_vaddr));
		uint32_t import_count = *count_ptr;

		logf_both("guest header OK: image_end=0x%x import_table=0x%x import_names=0x%x "
			"import_count=%u entry=0x%x\n",
			header_in_file->image_end, header_in_file->import_table, header_in_file->import_names,
			import_count, header_in_file->entry);

		/* resolve imports, writing resolved addresses into the data heap
		while it's still ours to write - once svcMapMemory moves it, this
		pointer stops being valid (nx-mapmem-poc's lesson, again) */
		uint64_t *table = (uint64_t *)((char *)gs.data_heap + (header_in_file->import_table - gs.data_vaddr));
		const char *name = (const char *)gs.data_heap + (header_in_file->import_names - gs.data_vaddr);

		for (uint32_t i = 0; i < import_count; i++)
		{
			void *function = resolve_import(name);

			if (!function)
			{
				logf_both("guest import '%s' has no host implementation\n", name);
				missing_imports++;
			}
			else
			{
				logf_both("resolved import '%s' -> %p\n", name, function);
				table[i] = (uint64_t)(uintptr_t)function;
			}
			name += strlen(name) + 1;
		}
	}
	if (missing_imports)
	{
		teardown(&gs);
		return -1;
	}

	rc = svcMapMemory((void *)(uintptr_t)gs.data_vaddr, gs.data_heap, gs.data_size);
	if (R_FAILED(rc))
	{
		logf_both("svcMapMemory (data) FAILED, rc=0x%x\n", rc);
		teardown(&gs);
		return -1;
	}
	gs.data_view = gs.data_heap; /* now also true at gs.data_vaddr; only used as a "moved" flag */

	/* ---------- the exec segment: the CodeMemory dance */

	gs.code_owner_src = aligned_alloc(0x1000, gs.code_size);
	if (!gs.code_owner_src)
	{
		logf_both("aligned_alloc(0x%x) for the exec segment failed\n", gs.code_size);
		teardown(&gs);
		return -1;
	}

	rc = svcCreateCodeMemory(&gs.code_handle, gs.code_owner_src, gs.code_size);
	if (R_FAILED(rc))
	{
		logf_both("svcCreateCodeMemory FAILED, rc=0x%x\n", rc);
		teardown(&gs);
		return -1;
	}
	gs.code_handle_valid = 1;

	virtmemLock();
	gs.code_owner_view = virtmemFindCodeMemory(gs.code_size, 0x1000);
	virtmemUnlock();
	if (!gs.code_owner_view)
	{
		logf_both("virtmemFindCodeMemory failed\n");
		teardown(&gs);
		return -1;
	}

	rc = svcControlCodeMemory(gs.code_handle, CodeMapOperation_MapOwner, gs.code_owner_view, gs.code_size, Perm_Rw);
	if (R_FAILED(rc))
	{
		logf_both("MapOwner FAILED, rc=0x%x\n", rc);
		teardown(&gs);
		return -1;
	}

	memset(gs.code_owner_view, 0, gs.code_size);
	memcpy(gs.code_owner_view, gs.file_data + exec_segment->p_offset, exec_segment->p_filesz);
	armDCacheFlush(gs.code_owner_view, gs.code_size);

	rc = svcControlCodeMemory(gs.code_handle, CodeMapOperation_MapSlave, (void *)(uintptr_t)gs.code_vaddr,
		gs.code_size, Perm_Rx);
	if (R_FAILED(rc))
	{
		logf_both("MapSlave FAILED, rc=0x%x\n", rc);
		teardown(&gs);
		return -1;
	}
	armICacheInvalidate((void *)(uintptr_t)gs.code_vaddr, gs.code_size);

	logf_both("mapped: code 0x%x (R-X), data 0x%x (RW-). about to call entry 0x%x ...\n",
		gs.code_vaddr, gs.data_vaddr, header_in_file->entry);
	{
		void (*entry)(void) = (void (*)(void))(uintptr_t)header_in_file->entry;
		entry();
	}
	logf_both("entry returned.\n");

	teardown(&gs);
	return 0;
}

/* sdmc:/haloce-nx/ - game data (the xiso, the maps extracted from it),
separate from sdmc:/switch/halo-ce-nx-guest-poc/'s app binaries/log:
different lifecycle (gigabytes, user-supplied, survives a reinstall),
different convention (not every homebrew app's own folder needs a
multi-GB disc image sitting in it). */
#define GAME_DATA_DIR "sdmc:/haloce-nx"
#define GAME_XISO_PATH GAME_DATA_DIR "/halo.xiso"

/* a multi-GB xiso takes minutes to extract over the SD card, not
seconds - with no on-screen feedback during it, that looked exactly
like a hang on a blank screen (nothing calls consoleUpdate between
"starting" and load_and_run_guest otherwise). Throttled so the SD
card's own write speed is the bottleneck, not console text. */
static void extract_game_data_proc(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	static unsigned long long last_shown_mb;
	unsigned long long done_mb = done / (1024 * 1024);

	(void)context;
	if (done == 0 || done == total)
		logf_both("xiso: %s (%llu/%llu bytes overall)\n", file, done, total);
	if (done != total && done_mb == last_shown_mb)
		return;
	last_shown_mb = done_mb;
	printf("\x1b[2J\x1b[HExtracting Halo CE game data...\n\n%s\n%llu / %llu MB\n",
		file, done_mb, total / (1024 * 1024));
	consoleUpdate(NULL);
}

/* xiso_extract_maps always (re)writes into GAME_DATA_DIR/maps - this
just avoids redoing a multi-GB extraction on every single launch once
it has already succeeded once. Supports both of the shapes a player
might have on their SD card: GAME_XISO_PATH alone (extracted here on
first run) or GAME_DATA_DIR/maps already populated some other way
(skipped here, used as-is) - PORTING.md's "unstub video" notes. */
/* returns whether the extraction screen was shown */
static int ensure_game_data_extracted(void)
{
	struct stat info;
	char marker[256];

	snprintf(marker, sizeof(marker), "%s/maps/.extracted", GAME_DATA_DIR);
	if (stat(marker, &info) == 0)
	{
		logf_both("game data already extracted at %s/maps\n", GAME_DATA_DIR);
		return 0;
	}
	if (stat(GAME_XISO_PATH, &info) != 0)
	{
		logf_both("no xiso at %s and no extracted maps yet - game data unavailable\n", GAME_XISO_PATH);
		return 0;
	}
	{
		char error[256] = {0};

		logf_both("extracting maps from %s to %s ...\n", GAME_XISO_PATH, GAME_DATA_DIR);
		if (xiso_extract_maps(GAME_XISO_PATH, GAME_DATA_DIR, extract_game_data_proc, NULL, error, sizeof(error)))
		{
			FILE *marker_file = fopen(marker, "w");

			if (marker_file)
				fclose(marker_file);
			logf_both("extraction done.\n");
		}
		else
		{
			logf_both("extraction FAILED: %s\n", error);
		}
	}
	return 1;
}

/* Every 10 seconds, how many frames the game has presented: a hang
with no log line says nothing on its own, this says whether the game
is still presenting (alive, stuck loading) or frozen. */
extern volatile unsigned long g_host_swap_count;

static void heartbeat_thread(void *arg)
{
	extern void host_audio_stats(unsigned long *buffers, int *peak);
	u64 start = armGetSystemTick();

	(void)arg;
	for (;;)
	{
		unsigned long audio_buffers;
		int audio_peak;

		/* every 10 s: each log line commits the SD card, which must not
		become a hitch of its own */
		svcSleepThread(10000000000ULL);
		host_audio_stats(&audio_buffers, &audio_peak);
		logf_both("heartbeat: %llus, %lu frames presented, %lu audio buffers, audio peak %d\n",
			armTicksToNs(armGetSystemTick() - start) / 1000000000ULL, (unsigned long)g_host_swap_count,
			audio_buffers, audio_peak);
	}
}

static void start_heartbeat(void)
{
	static Thread thread;

	/* applications may use priorities 0x1C-0x3B only; same as the main
	thread, on core 1, so a main thread spinning on core 0 can't starve it */
	Result rc = threadCreate(&thread, heartbeat_thread, NULL, NULL, 0x10000, 0x2C, 1);

	if (R_SUCCEEDED(rc))
		rc = threadStart(&thread);
	if (R_FAILED(rc))
		logf_both("heartbeat: could not start, rc=0x%x\n", rc);
}

int main(int argc, char *argv[])
{
	consoleInit(NULL);

	host_loading_text_console();
	/* the previous run's log survives one relaunch */
	remove("sdmc:/switch/halo-ce-nx-guest-poc/host.prev.log");
	rename("sdmc:/switch/halo-ce-nx-guest-poc/host.log", "sdmc:/switch/halo-ce-nx-guest-poc/host.prev.log");
	g_log = fopen("sdmc:/switch/halo-ce-nx-guest-poc/host.log", "w");
	logf_both("halo-ce-nx guest-poc host starting\n");

	if (ensure_game_data_extracted())
		host_loading_text_console(); /* the extraction screen replaced it */
	start_heartbeat();

	{
		int guest_result = load_and_run_guest("sdmc:/switch/halo-ce-nx-guest-poc/guest.elf");

		/* the guest returned (or never started): failures belong on screen */
		g_console_echo = 1;
		if (guest_result == 0)
			logf_both("SUCCESS: the guest loaded, ran and called back into the host.\n");
		else
			logf_both("FAILED: see the lines above.\n");
	}

	if (g_log)
		fclose(g_log);

	printf("Done. See sdmc:/switch/halo-ce-nx-guest-poc/host.log\nPress + to exit.\n");
	consoleUpdate(NULL);

	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);
	while (appletMainLoop())
	{
		padUpdate(&pad);
		if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
			break;
		consoleUpdate(NULL);
	}
	consoleExit(NULL);
	return 0;
}
