/*
SWITCH_WIN32_NULL.C

The "headless boot" Win32 handle/thread/time/memory API: every entry
point source/ calls directly (not through posix.h's boundary - those
are real now too, see host_posix_files.c/host_posix_io.c). The file
functions that used to be null-stubbed here (CreateFileA, ReadFile,
WriteFile, ReadFileEx, WriteFileEx, CloseHandle, GetFileSize,
GetFileTime, SetFileTime, SetFilePointer, SetEndOfFile, DeleteFileA,
MoveFileA, CopyFileA, CreateDirectoryA, RemoveDirectoryA,
FindFirstFileA, FindNextFileA, GetFileAttributesA,
GetFileAttributesExA, SetFileAttributesA, GetDiskFreeSpaceExA) are real
now too, in xbox_files.c (CloseHandle: switch_xbox_handles.c) -
PORTING.md's "real file I/O" milestone. CreateThread/CreateEventA/
SetEvent/ResetEvent/WaitForSingleObject(Ex) are also real now
(switch_xbox_threads.c, host_threads.c) - PORTING.md's "real
threading" milestone, needed because source/cache/cache_files_
windows.c's cache-file worker thread genuinely depends on both
actually working, unlike every other CreateThread/CreateEventA call
site (input_xbox.c, cache_files_decompress_windows.c, bungie_net's
thread_win32.c), none of which check the result at all.
CreateMutexA/ReleaseMutex stay null-stubbed - only bungie_net
(networking, not yet in scope) uses them. Failure returns use the
real Win32 sentinel values where they differ from plain 0
(INVALID_HANDLE_VALUE, INVALID_SET_FILE_POINTER, WAIT_FAILED) for what's
still null-stubbed here - getting these wrong is exactly the kind of
bug a caller's `== 0` vs `== INVALID_HANDLE_VALUE` check would hide
until a very confusing crash later. See switch_d3d8_null.c's header
comment for the generation method and caveats. GetTickCount/
QueryPerformanceCounter/QueryPerformanceFrequency are real now too
(AArch64's own system counter register, see below) -
vita_host_time_us (guest_platform_stubs.c) still isn't; that one needs
a real host-synced wall-clock import this doesn't provide, not just a
free-running counter.
*/

#include "platform.h"

long __stdcall CompareFileTime(const struct _FILETIME *, const struct _FILETIME *)
{
	return 0;
}

int __stdcall SwitchToThread(void)
{
	return 0;
}


/* per thread, as on Windows: the cache worker, the UI's filesystem check
thread and the main thread all do file I/O, and a shared value let one
thread's error make another's success look like a failure (or the other
way round - CreateFileA's ERROR_ALREADY_EXISTS is read straight after) */
#include "../include/switch_guest_thread.h"

unsigned long __stdcall GetLastError(void)
{
	return __guest_thread_port_data()->last_error;
}

void __stdcall SetLastError(unsigned long error)
{
	__guest_thread_port_data()->last_error = error;
}

/* real now (not null stubs): AArch64's own CNTPCT_EL0/CNTFRQ_EL0
system counter - the same registers libnx's own armGetSystemTick/
armGetSystemTickFreq (arm/counter.h) read, an ordinary EL0 (so,
guest-code-legal with no host import at all) register read, not a
privileged one. The previous "return 0 without ever touching the
output" null stubs were a real, if latent, crash risk beyond just
"the game runs with no timing": source/cseries/profile.c's own
profile_initialize reads QueryPerformanceFrequency's OUTPUT
unconditionally right after calling it (PORTING.md), with no check on
the call's own return value - divides by whatever garbage was already
on the stack where `frequency.QuadPart` lived, including a genuine
chance of a divide-by-zero crash if that happened to be zero. */
static inline unsigned long long switch_system_tick(void)
{
	unsigned long long value;

	__asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (value));
	return value;
}

static inline unsigned long long switch_system_tick_frequency(void)
{
	unsigned long long value;

	__asm__("mrs %0, cntfrq_el0" : "=r" (value));
	return value;
}

unsigned long __stdcall GetTickCount(void)
{
	unsigned long long frequency = switch_system_tick_frequency();

	return frequency ? (unsigned long)(switch_system_tick() * 1000ULL / frequency) : 0;
}

/* A microsecond counter, as port/linux/src/xbox_kernel.c reports: the
game does 32-bit intermediate arithmetic on counter values, and the raw
19.2 MHz system counter overflowed it (a fade computed an alpha of -48). */
#define PERFORMANCE_FREQUENCY 1000000ULL

int __stdcall QueryPerformanceCounter(union _LARGE_INTEGER *counter)
{
	unsigned long long tick = switch_system_tick();
	unsigned long long frequency = switch_system_tick_frequency();

	if (counter && frequency)
		counter->QuadPart = (__int64)((tick / frequency) * PERFORMANCE_FREQUENCY +
			(tick % frequency) * PERFORMANCE_FREQUENCY / frequency);
	return 1;
}

int __stdcall QueryPerformanceFrequency(union _LARGE_INTEGER *frequency)
{
	if (frequency)
		frequency->QuadPart = (__int64)PERFORMANCE_FREQUENCY;
	return 1;
}

/* real now (not null stubs): source/cseries/cseries_windows.c's
system_malloc/system_realloc/system_free - and so every single plain
`malloc`/`realloc`/`free` the game's own code makes, since cseries.h's
`#define malloc(size) match_malloc(__FILE__, __LINE__, size)` routes
every one of them through here - called GlobalAlloc(0, size) and got
NULL back unconditionally, every time, with no exception. The game's
first plain `malloc` call (cache_files_initialize's cache_file_globals.
requests, PORTING.md's milestone 11) is where this first got hit hard
enough to fail a match_assert and halt, but every earlier allocation
through this path (there weren't many - most early allocations are
game_state_malloc/XPhysicalAlloc, a separate, already-real pool) was
silently buggy the same way until now. Backed by this guest's own
musl heap (guest_syscall.c's mmap bump allocator) - the same real
allocator a plain game-side `malloc` already proved out early
(guest_main.c's own smoke test). */
#include <stdlib.h>
#include <string.h>

void *__stdcall GlobalAlloc(unsigned int flags, unsigned long size)
{
	void *memory = malloc(size);

	if (memory && (flags & 0x0040 /* GMEM_ZEROINIT */))
		memset(memory, 0, size);
	return memory;
}

void *__stdcall GlobalReAlloc(void *memory, unsigned long size, unsigned int)
{
	return realloc(memory, size);
}

void __stdcall GlobalMemoryStatus(struct _MEMORYSTATUS *)
{

}

void *__stdcall LocalFree(void *memory)
{
	free(memory);
	return 0;
}

unsigned long __stdcall LocalSize(void *memory)
{
	extern unsigned long malloc_usable_size(void *);

	return memory ? (unsigned long)malloc_usable_size(memory) : 0;
}

void __stdcall OutputDebugStringA(const char *)
{
	
}

int __stdcall VirtualProtect(void *, unsigned long, unsigned long, unsigned long *)
{
	return 0;
}

int __stdcall SystemTimeToFileTime(const struct _SYSTEMTIME *, struct _FILETIME *)
{
	return 0;
}

void __stdcall GetSystemTime(struct _SYSTEMTIME *)
{
	
}
