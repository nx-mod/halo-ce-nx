/*
SWITCH_XBOX_THREADS.C

Real CreateThread/CreateEventA/SetEvent/ResetEvent/WaitForSingleObject(Ex)
(PORTING.md's "real threading" milestone) - source/cache/cache_files_
windows.c's cache-file worker thread genuinely needs a second real
thread to exist (its own match_asserts on CreateEventA/CreateThread's
results were the next real blocker after the file I/O milestone, hit
on every single startup), unlike every other CreateThread/CreateEventA
call site in the game, none of which check the result at all.

Real work happens host-side (host_threads.c: libnx threadCreate/
UEvent) - this file is just the Win32-calling-convention-and-ABI-width
boundary: allocating a guest-addressed (so safely sub-4 GB, since
ILP32 code may legally take a 32-bit pointer to a stack local anywhere
on it) stack and this new thread's own TLS block (guest_tp.c), and
unpacking LPTHREAD_START_ROUTINE's stdcall convention + its one `void*`
argument into a plain call host_threads.c's trampoline can make without
needing to know anything about Win32 calling conventions itself.
*/

#include "platform.h"

#include <stdint.h>
#include <stdlib.h>

extern long host_create_thread(unsigned int guest_entry, unsigned int guest_arg, unsigned int stack_size,
	unsigned int tls_block);
extern long host_event_create(int auto_clear);
extern void host_event_signal(long handle);
extern void host_event_clear(long handle);
extern long host_event_wait(long handle, long long timeout_ns);
extern long platform_run_apcs(void);

struct guest_thread_start
{
	unsigned long (__stdcall *start_routine)(void *);
	void *parameter;
};

#define GUEST_THREAD_STACK_SIZE 0x14000

/* the one function host_create_thread's host-side trampoline ever
calls (by address, through the import table the other direction -
this is guest code the host jumps into, same shape as __guest_entry
itself) - unpacks the real Win32 start routine and its stdcall
convention, which host_threads.c has no reason to know about */
static void guest_thread_entry(void *arg)
{
	struct guest_thread_start start = *(struct guest_thread_start *)arg;

	free(arg);
	start.start_routine(start.parameter);
}

void *__stdcall CreateThread(void *security_attributes, unsigned long stack_size,
	unsigned long (__stdcall *start_routine)(void *), void *parameter, unsigned long creation_flags,
	unsigned long *thread_id)
{
	struct guest_thread_start *start;
	void *tls_block;

	(void)security_attributes;
	(void)stack_size;
	/* CREATE_SUSPENDED isn't honored - nothing in the one call site
	that's fatal without a real thread (cache_file_windows_thread_create)
	ever passes it; input_xbox.c's does, but never checks this result
	either way, so starting it running immediately changes nothing it
	depends on */
	(void)creation_flags;
	platform_log("CreateThread: start routine %p", (void *)start_routine);
	start = malloc(sizeof(*start));
	if (!start)
		return 0;
	start->start_routine = start_routine;
	start->parameter = parameter;
	/* No guest-side stack buffer is allocated here anymore - a real
	bug, found by reading libnx's actual threadCreate() source
	(nx/source/kernel/thread.c; no local copy exists, had to fetch it
	from switchbrew/libnx on GitHub). It ALWAYS performs its own
	internal svcMapMemory MOVE of whatever stack_mem it's given, to a
	fresh mirror address it picks itself - even the NULL/auto path
	does this, moving its own freshly __libnx_aligned_alloc()'d
	memory. A MOVE's source must be one untouched block of ordinary
	"Heap" memory. Anything living inside this guest ELF's data
	segment - static .bss array (tried first, following
	~/switch/libdol-nx's working precedent) or heap-bump-allocated
	(tried before that) - is already the *destination* of
	host_main.c's own one-time whole-segment svcMapMemory MOVE at
	load; the kernel won't MOVE a MOVE's destination again. Both
	attempts failed identically: rc=0xd401 (InvalidCurrentMemory).
	host_create_thread (host_threads.c) now always passes stack_mem=
	NULL to the real threadCreate() and logs back the mirror address
	libnx actually chose - see that file's own comment for why this
	is expected to still land safely sub-4 GB despite the address no
	longer being something this file controls. */
	{
		extern void __guest_enable_locks(void);

		__guest_enable_locks();
	}
	tls_block = calloc(1, 512);
	if (!tls_block || !host_create_thread(
		(unsigned int)(uintptr_t)guest_thread_entry,
		(unsigned int)(uintptr_t)start,
		(unsigned int)GUEST_THREAD_STACK_SIZE,
		(unsigned int)(uintptr_t)tls_block))
	{
		free(start);
		free(tls_block);
		return 0;
	}
	if (thread_id)
		*thread_id = 1;
	/* opaque, "it worked" - GetExitCodeThread/CloseHandle are never
	called on this specific handle anywhere real threading is actually
	load-bearing (checked directly, PORTING.md); SetThreadPriority is,
	but is already a safe no-op on any handle, real or not */
	return (void *)1;
}

void *__stdcall CreateEventA(void *security_attributes, int manual_reset, int initial_state,
	const char *name)
{
	long handle;

	(void)security_attributes;
	(void)name;
	handle = host_event_create(!manual_reset);
	if (!handle)
		return 0;
	if (initial_state)
		host_event_signal(handle);
	return (void *)(uintptr_t)handle;
}

/* A mutex is an auto-reset event that starts signaled (signaled = free),
plus Win32's ownership: the owning thread may take it again, and it is
free once released as many times as taken. Without that, a nested take
waited out thread_win32.c's timeout - an hour, for the saved game
files' mutexes - with the UI's filesystem check thread running.
Threads are told apart by their TLS block (host_get_guest_tp), which is
unique per thread. Indexed by event handle (host_threads.c's 1-based
table). */
#define MAXIMUM_MUTEX_HANDLES 65
static struct
{
	void *volatile owner;
	int count;
	char is_mutex;
} mutex_state[MAXIMUM_MUTEX_HANDLES];

extern void *host_get_guest_tp(void);

void *__stdcall CreateMutexA(void *security_attributes, int initial_owner, const char *name)
{
	long handle;

	(void)security_attributes;
	(void)name;
	handle = host_event_create(1);
	if (!handle || handle >= MAXIMUM_MUTEX_HANDLES)
		return 0;
	mutex_state[handle].is_mutex = 1;
	mutex_state[handle].count = 0;
	mutex_state[handle].owner = 0;
	if (initial_owner)
	{
		mutex_state[handle].owner = host_get_guest_tp();
		mutex_state[handle].count = 1;
	}
	else
	{
		host_event_signal(handle);
	}
	return (void *)(uintptr_t)handle;
}

int __stdcall ReleaseMutex(void *mutex)
{
	long handle = (long)(uintptr_t)mutex;

	if (handle <= 0 || handle >= MAXIMUM_MUTEX_HANDLES || !mutex_state[handle].is_mutex ||
		mutex_state[handle].owner != host_get_guest_tp())
	{
		return 0; /* ERROR_NOT_OWNER */
	}
	if (--mutex_state[handle].count == 0)
	{
		mutex_state[handle].owner = 0;
		host_event_signal(handle);
	}
	return 1;
}

int __stdcall SetEvent(void *event)
{
	host_event_signal((long)(uintptr_t)event);
	return 1;
}

int __stdcall ResetEvent(void *event)
{
	host_event_clear((long)(uintptr_t)event);
	return 1;
}

static unsigned long wait_milliseconds_to_result(void *handle, unsigned long milliseconds)
{
	/* INFINITE (0xFFFFFFFF) as a signed 32-bit value is already -1;
	widened to the 64-bit nanosecond count host_event_wait/libnx's own
	waitSingle expect, (long long)-1 cast to u64 there is exactly
	UINT64_MAX - "wait forever" - with no separate sentinel needed */
	long long timeout_ns = (long long)(long)milliseconds * 1000000LL;
	long index = (long)(uintptr_t)handle;
	int is_mutex = index > 0 && index < MAXIMUM_MUTEX_HANDLES && mutex_state[index].is_mutex;

	if (is_mutex && mutex_state[index].owner == host_get_guest_tp())
	{
		mutex_state[index].count++;
		return 0; /* WAIT_OBJECT_0: already ours */
	}
	if (host_event_wait(index, timeout_ns) != 0)
		return 0x102; /* WAIT_TIMEOUT */
	if (is_mutex)
	{
		mutex_state[index].owner = host_get_guest_tp();
		mutex_state[index].count = 1;
	}
	return 0; /* WAIT_OBJECT_0 */
}

/* Mirrors port/linux/src/xbox_kernel.c's SleepEx. The cache code's async
writes (WriteFileEx) only complete when an alertable SleepEx runs their
queued APC and reports WAIT_IO_COMPLETION; a stub returning 0 made every
one look failed ("setup for new cache file failed (#0)"). The timed wait
is a never-signaled host event, so no extra host import is needed. */
unsigned long __stdcall SleepEx(unsigned long milliseconds, int alertable)
{
	static long sleep_event;

	if (alertable && platform_run_apcs() > 0)
		return 0xC0; /* WAIT_IO_COMPLETION */
	if (!sleep_event)
		sleep_event = host_event_create(1);
	if (sleep_event)
		host_event_wait(sleep_event, (long long)(long)milliseconds * 1000000LL);
	if (alertable && platform_run_apcs() > 0)
		return 0xC0;
	return 0;
}

void __stdcall Sleep(unsigned long milliseconds)
{
	SleepEx(milliseconds, 0);
}

unsigned long __stdcall WaitForSingleObject(void *handle, unsigned long milliseconds)
{
	return wait_milliseconds_to_result(handle, milliseconds);
}

unsigned long __stdcall WaitForSingleObjectEx(void *handle, unsigned long milliseconds, int alertable)
{
	/* real Win32: an alertable wait returns WAIT_IO_COMPLETION (0xC0) as
	soon as a queued APC runs, before necessarily checking the object at
	all - the one real call site (cache_file_windows_thread_proc) loops
	back and waits again either way, so draining every pending APC
	first and reporting that, rather than actually polling the event
	too in the same call, is exactly as correct and much simpler */
	if (alertable && platform_run_apcs() > 0)
		return 0xC0;
	return wait_milliseconds_to_result(handle, milliseconds);
}
