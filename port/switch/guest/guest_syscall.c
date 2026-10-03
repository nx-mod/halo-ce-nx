/*
GUEST_SYSCALL.C

syscall_arch.h (libc/arch/aarch64_ilp32) routes every musl syscall
through __guest_syscall - this is that dispatcher. Minimal for
milestone 2: mmap as a bump allocator over the heap region guest.ld
reserves (__guest_heap_start/__guest_heap_end), munmap/brk as no-ops
(the bump allocator never frees; mallocng barely uses brk at all - see
PORTING.md), write/writev routed to the host for real stdout/stderr,
exit routed to the host and then halted.

Real per-region unmapping, and anything else a syscall number shows up
needing, is a later problem - everything unhandled logs loudly and
returns -ENOSYS rather than silently doing nothing.
*/

#include <stdint.h>

extern void host_log(const char *text);
extern void host_write(const char *data, long long length);

extern char __guest_heap_start[];
extern char __guest_heap_end[];

#define SYS_write 64
#define SYS_writev 66
#define SYS_mmap 222
#define SYS_munmap 215
#define SYS_brk 214
#define SYS_exit 93
#define SYS_exit_group 94

#define ENOMEM 12
#define ENOSYS 38

static uintptr_t heap_cursor;

struct guest_iovec
{
	uint32_t iov_base; /* a real pointer in this process, just 32-bit-wide */
	uint32_t iov_len;
};

static long do_mmap(long long length)
{
	uintptr_t aligned = (length + 0xfff) & ~(uintptr_t)0xfff;

	if (!heap_cursor)
		heap_cursor = (uintptr_t)__guest_heap_start;
	if (heap_cursor + aligned > (uintptr_t)__guest_heap_end)
	{
		host_log("__guest_syscall: mmap: guest heap exhausted (see guest.ld's HEAP_SIZE)");
		return -ENOMEM;
	}
	uintptr_t result = heap_cursor;
	heap_cursor += aligned;
	return (long)result;
}

long __guest_syscall(long long n, long long a, long long b, long long c, long long d, long long e, long long f)
{
	switch (n)
	{
	case SYS_mmap:
		return do_mmap(b);
	case SYS_munmap:
		return 0; /* the bump allocator never frees */
	case SYS_brk:
		return 0; /* "can't extend brk" - mallocng's real path is mmap anyway */
	case SYS_write:
		host_write((const char *)(uintptr_t)b, c);
		return c;
	case SYS_writev:
	{
		const struct guest_iovec *iov = (const struct guest_iovec *)(uintptr_t)b;
		long long total = 0;

		for (long long i = 0; i < c; i++)
		{
			host_write((const char *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
			total += iov[i].iov_len;
		}
		return total;
	}
	case SYS_exit:
	case SYS_exit_group:
		host_log("__guest_syscall: guest called exit()");
		for (;;)
			;
	default:
		host_log("__guest_syscall: unimplemented syscall number");
		return -ENOSYS;
	}
}
