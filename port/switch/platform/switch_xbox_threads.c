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

#define GUEST_THREAD_STACK_SIZE 0x14000

/* Threads the game creates, so a handle means something: ResumeThread,
GetExitCodeThread and WaitForSingleObject act on the right thread.
thread_win32.c's create_thread reported failure (SetThreadPriority was a
stub returning FALSE) and ran the UI's filesystem check synchronously
while the thread - started at once, CREATE_SUSPENDED ignored - ran it
too; and thread_has_exited never saw it finish (GetExitCodeThread always
failed), so the UI stayed inhibited. Handles are GUEST_THREAD_HANDLE_BASE
+ index: below 0x1000, so platform_handle_get never dereferences them,
and clear of the event handles (1-64). */
#define GUEST_THREAD_HANDLE_BASE 0x200
#define MAXIMUM_GUEST_THREADS 32
#define STILL_ACTIVE_CODE 0x103

struct guest_thread
{
	volatile int used;
	volatile int exited;
	volatile int suspended;
	unsigned long exit_code;
	long resume_event;
	unsigned long (__stdcall *start_routine)(void *);
	void *parameter;
};

static struct guest_thread guest_threads[MAXIMUM_GUEST_THREADS];

static struct guest_thread *guest_thread_from_handle(void *handle)
{
	unsigned long index = (unsigned long)(uintptr_t)handle - GUEST_THREAD_HANDLE_BASE;

	if ((unsigned long)(uintptr_t)handle < GUEST_THREAD_HANDLE_BASE || index >= MAXIMUM_GUEST_THREADS ||
		!guest_threads[index].used)
	{
		return NULL;
	}
	return &guest_threads[index];
}

/* the function host_create_thread's host-side trampoline calls: waits
out CREATE_SUSPENDED, runs the Win32 start routine, records its exit */
static void guest_thread_entry(void *arg)
{
	struct guest_thread *thread = arg;

	if (thread->resume_event)
		host_event_wait(thread->resume_event, -1);
	thread->exit_code = thread->start_routine(thread->parameter);
	__atomic_store_n(&thread->exited, 1, __ATOMIC_SEQ_CST);
}

void *__stdcall CreateThread(void *security_attributes, unsigned long stack_size,
	unsigned long (__stdcall *start_routine)(void *), void *parameter, unsigned long creation_flags,
	unsigned long *thread_id)
{
	struct guest_thread *thread = NULL;
	void *tls_block;
	int index;

	(void)security_attributes;
	(void)stack_size;
	platform_log("CreateThread: start routine %p%s", (void *)start_routine,
		(creation_flags & CREATE_SUSPENDED) ? " (suspended)" : "");
	for (index = 0; index < MAXIMUM_GUEST_THREADS; index++)
	{
		int expected = 0;

		if (__atomic_compare_exchange_n(&guest_threads[index].used, &expected, 1, 0, __ATOMIC_SEQ_CST,
			__ATOMIC_SEQ_CST))
		{
			thread = &guest_threads[index];
			break;
		}
	}
	if (!thread)
		return 0;
	thread->exited = 0;
	thread->exit_code = STILL_ACTIVE_CODE;
	thread->start_routine = start_routine;
	thread->parameter = parameter;
	thread->suspended = (creation_flags & CREATE_SUSPENDED) != 0;
	thread->resume_event = thread->suspended ? host_event_create(0) : 0;
	{
		extern void __guest_enable_locks(void);

		__guest_enable_locks();
	}
	/* the stack is the host's (host_threads.c: threadCreate always moves
	its stack, and the guest segment can't be moved again); this block
	is the thread's guest TLS (guest_tp.c, switch_guest_thread.h) */
	tls_block = calloc(1, 512);
	if (!tls_block || (thread->suspended && !thread->resume_event) || !host_create_thread(
		(unsigned int)(uintptr_t)guest_thread_entry,
		(unsigned int)(uintptr_t)thread,
		(unsigned int)GUEST_THREAD_STACK_SIZE,
		(unsigned int)(uintptr_t)tls_block))
	{
		free(tls_block);
		thread->used = 0;
		return 0;
	}
	if (thread_id)
		*thread_id = (unsigned long)index + 1;
	return (void *)(uintptr_t)(GUEST_THREAD_HANDLE_BASE + index);
}

unsigned long __stdcall ResumeThread(void *handle)
{
	struct guest_thread *thread = guest_thread_from_handle(handle);

	if (!thread)
		return 0;
	if (!thread->suspended)
		return 0; /* previous suspend count */
	thread->suspended = 0;
	host_event_signal(thread->resume_event);
	return 1;
}

int __stdcall SetThreadPriority(void *handle, int priority)
{
	(void)handle;
	(void)priority;
	return 1;
}

int __stdcall GetExitCodeThread(void *handle, unsigned long *exit_code)
{
	struct guest_thread *thread = guest_thread_from_handle(handle);

	if (!thread)
		return 0;
	if (exit_code)
		*exit_code = __atomic_load_n(&thread->exited, __ATOMIC_SEQ_CST) ? thread->exit_code : STILL_ACTIVE_CODE;
	return 1;
}

/* CloseHandle (switch_xbox_handles.c) on a thread handle */
int guest_thread_close(void *handle)
{
	struct guest_thread *thread = guest_thread_from_handle(handle);

	if (!thread)
		return 0;
	/* a running thread keeps its slot until it exits; a closed, exited
	one gives it back */
	if (__atomic_load_n(&thread->exited, __ATOMIC_SEQ_CST))
		thread->used = 0;
	return 1;
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

static unsigned long wait_for_thread(struct guest_thread *thread, unsigned long milliseconds)
{
	unsigned long waited = 0;

	while (!__atomic_load_n(&thread->exited, __ATOMIC_SEQ_CST))
	{
		if (milliseconds != 0xFFFFFFFFUL && waited >= milliseconds)
			return 0x102; /* WAIT_TIMEOUT */
		SleepEx(1, 0);
		waited++;
	}
	return 0; /* WAIT_OBJECT_0 */
}

static unsigned long wait_milliseconds_to_result(void *handle, unsigned long milliseconds)
{
	if (guest_thread_from_handle(handle))
		return wait_for_thread(guest_thread_from_handle(handle), milliseconds);
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
