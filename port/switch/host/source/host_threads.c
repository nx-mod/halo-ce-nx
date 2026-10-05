/*
HOST_THREADS.C

Real threading (PORTING.md's "a second real thread" milestone):
source/cache/cache_files_windows.c's cache-file worker thread is
genuinely load-bearing (its two match_asserts on CreateEventA/
CreateThread's results were the next real blocker after the file I/O
milestone, hit on every single startup) - unlike every other
CreateThread/CreateEventA call site in the game, which none of the
code checks the result of at all.

Real libnx primitives throughout, not anything hand-rolled:
- threadCreate/threadStart for the thread itself, given a
  guest-allocated (and so guaranteed sub-4 GB, safe for ILP32 guest
  code to take addresses within) stack - CreateThread's own
  implementation (switch_xbox_threads.c, guest-side) allocates it.
- UEvent (user-mode, non-privileged - unlike Event/eventCreate, which
  libnx's own header flags as privileged and this is an ordinary
  homebrew app) for CreateEventA/SetEvent/ResetEvent/
  WaitForSingleObject(Ex), exactly the same same-process,
  multi-thread-safe signaling primitive Win32 events are.
- The host's own real __thread (compiler/libnx-backed, correctly
  virtualized per real OS thread already) as the single piece of
  plumbing guest_tp.c's per-thread TLS block pointer needs - nothing
  guest-side has to manage which real thread is asking.
*/

#include <stdint.h>
#include <stdlib.h>
#include <switch.h>

static __thread void *s_guest_tp;

void *host_get_guest_tp(void)
{
	return s_guest_tp;
}

void host_set_guest_tp(void *ptr)
{
	s_guest_tp = ptr;
}

/* guest_entry is a guest (32-bit) function pointer taking one void*
(switch_xbox_threads.c's own small Win32-calling-convention-unpacking
trampoline - this file has no reason to know stdcall from anything
else); guest_arg is whatever that trampoline expects; tls_block is a
guest-allocated, guest_tp.c-sized buffer for this new thread's own
__guest_get_tp to return once it's running. All addresses, guest or
host, are already valid pointers in this one shared process.

No stack_mem parameter here anymore - see host_create_thread's own
comment for why libnx's real threadCreate() must be given stack_mem=
NULL, never a guest-segment address, and why the resulting stack
address is something libnx itself picks, not something we supply. */
struct host_thread_args
{
	void (*guest_entry)(void *);
	void *guest_arg;
	void *tls_block;
};

static void host_thread_trampoline(void *arg)
{
	struct host_thread_args args = *(struct host_thread_args *)arg;

	free(arg);
	host_set_guest_tp(args.tls_block);
	args.guest_entry(args.guest_arg);
	/* every real call site's start routine loops forever - this is
	defensive, not expected to run */
}

/* cache_file_windows_thread_create's worker is the one confirmed
load-bearing caller, but input_xbox.c's input_initialize() also calls
the game's own CreateThread (CREATE_SUSPENDED, never actually resumed
- see switch_xbox_threads.c's own comment) - now that this is real,
that creates a second real thread too, which a single reused Thread
struct would corrupt out from under the first while it's still
running. A handful of slots, not just one. */
#define MAXIMUM_HOST_THREADS 4
static Thread s_threads[MAXIMUM_HOST_THREADS];
static int s_threads_used[MAXIMUM_HOST_THREADS];

/* stack_mem is gone - a real libnx source read (nx/source/kernel/
thread.c, there's no local copy, only the prebuilt .a) settled this:
threadCreate() ALWAYS performs its own internal svcMapMemory MOVE of
whatever stack_mem it's given, to a brand-new mirror address it picks
itself via virtmemFindStack - even in the stack_mem==NULL path, where
it moves its own __libnx_aligned_alloc()'d memory. A MOVE's source
must be a single, untouched block of ordinary "Heap" state memory.
Any address inside the guest data segment - static .bss array or
heap-bump-allocated, doesn't matter which - is already the
*destination* of host_main.c's own one-time svcMapMemory MOVE (the
whole-segment load). The kernel does not allow MOVE-ing a MOVE's
destination again: that's rc=0xd401 (InvalidCurrentMemory), identical
either way, which is exactly what two straight hardware tests showed.

So: always pass NULL. libnx allocates genuinely untouched host-heap
memory for the stack itself and moves THAT (which succeeds), and the
real, running stack address afterwards is s_threads[index].stack_mirror
- a destination *libnx* chose, not anything we supplied. Logging it
is now how we can tell, next hardware test, whether that address is
safely sub-4 GB (needed for the ILP32 guest code that will actually
run on it) - the Switch homebrew map region conventionally starts low
(around 0x8000000), so it should be, but this is the first real
confirmation opportunity rather than another guess. */
long host_create_thread(unsigned int guest_entry, unsigned int guest_arg, unsigned int stack_size,
	unsigned int tls_block)
{
	struct host_thread_args *args;
	Result rc;
	int index;

	for (index = 0; index < MAXIMUM_HOST_THREADS; index++)
		if (!s_threads_used[index])
			break;
	if (index == MAXIMUM_HOST_THREADS)
		return 0;
	args = malloc(sizeof(*args));
	if (!args)
		return 0;
	args->guest_entry = (void (*)(void *))(uintptr_t)guest_entry;
	args->guest_arg = (void *)(uintptr_t)guest_arg;
	args->tls_block = (void *)(uintptr_t)tls_block;
	rc = threadCreate(&s_threads[index], host_thread_trampoline, args, NULL, stack_size, 0x3B, -2);
	if (R_FAILED(rc))
	{
		extern void logf_both(const char *fmt, ...);

		logf_both("host_create_thread: threadCreate failed, rc=0x%x (stack_size=%u)\n", rc, stack_size);
		free(args);
		return 0;
	}
	s_threads_used[index] = 1;
	rc = threadStart(&s_threads[index]);
	if (R_FAILED(rc))
	{
		extern void logf_both(const char *fmt, ...);

		logf_both("host_create_thread: threadStart failed, rc=0x%x\n", rc);
		return 0;
	}
	{
		extern void logf_both(const char *fmt, ...);
		void *mirror = s_threads[index].stack_mirror;

		logf_both("host_create_thread: created, stack_mirror=%p (%s 4GB)\n", mirror,
			(uintptr_t)mirror < 0x100000000ULL ? "below" : "NOT BELOW - guest 32-bit pointers to this stack WILL be corrupt");
	}
	return 1;
}

#define MAXIMUM_HOST_EVENTS 16
static UEvent s_events[MAXIMUM_HOST_EVENTS];
static int s_events_used[MAXIMUM_HOST_EVENTS];

long host_event_create(int auto_clear)
{
	int index;

	for (index = 0; index < MAXIMUM_HOST_EVENTS; index++)
	{
		if (!s_events_used[index])
		{
			s_events_used[index] = 1;
			ueventCreate(&s_events[index], auto_clear != 0);
			return index + 1; /* 1-based: 0 stays "no event" */
		}
	}
	return 0;
}

void host_event_signal(long handle)
{
	if (handle >= 1 && handle <= MAXIMUM_HOST_EVENTS)
		ueventSignal(&s_events[handle - 1]);
}

void host_event_clear(long handle)
{
	if (handle >= 1 && handle <= MAXIMUM_HOST_EVENTS)
		ueventClear(&s_events[handle - 1]);
}

/* 0 on success (signaled), nonzero on timeout/invalid handle. timeout_ns
of -1 (so, cast to u64, UINT64_MAX) waits indefinitely, matching
libnx's own waitSingle convention exactly - callers pass this through
directly from Win32's INFINITE, no separate sentinel needed. */
long host_event_wait(long handle, long long timeout_ns)
{
	Result rc;

	if (handle < 1 || handle > MAXIMUM_HOST_EVENTS)
		return 1;
	rc = waitSingle(waiterForUEvent(&s_events[handle - 1]), (u64)timeout_ns);
	return R_FAILED(rc) ? 1 : 0;
}
