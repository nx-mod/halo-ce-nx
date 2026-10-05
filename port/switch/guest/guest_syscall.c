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

/* PORTING.md's "real file I/O" milestone: xbox_files.c's CreateFileA/
ReadFile/etc (now part of SWITCH_PLATFORM_FILES) call plain POSIX open/
read/write/close/lseek, which musl routes here same as every other
syscall. Real host-side file descriptors (port/switch/host/source/
host_posix_io.c) back them - the guest fd IS the host fd, since both
sides already share one address space/one process, there's no separate
table to maintain here. */
extern long host_open(const char *path, int flags, int mode);
extern long host_read(int fd, void *buf, unsigned long count);
extern long host_write_fd(int fd, const void *buf, unsigned long count);
extern long host_close(int fd);
extern long long host_lseek(int fd, long long offset, int whence);

extern char __guest_heap_start[];
extern char __guest_heap_end[];

#define SYS_fcntl 25
#define SYS_openat 56
#define SYS_close 57
#define SYS_lseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_writev 66
#define SYS_pread64 67
#define SYS_pwrite64 68
#define SYS_munmap 215
#define SYS_brk 214
#define SYS_mmap 222
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
		/* stdout/stderr keep going through host_write (host.log + the
		devkitA64 console, host_main.c); every other fd is a real file */
		if (a == 1 || a == 2)
		{
			host_write((const char *)(uintptr_t)b, c);
			return c;
		}
		return host_write_fd((int)a, (const void *)(uintptr_t)b, (unsigned long)c);
	case SYS_writev:
	{
		const struct guest_iovec *iov = (const struct guest_iovec *)(uintptr_t)b;
		long long total = 0;

		for (long long i = 0; i < c; i++)
		{
			if (a == 1 || a == 2)
				host_write((const char *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
			else
				host_write_fd((int)a, (const void *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
			total += iov[i].iov_len;
		}
		return total;
	}
	case SYS_openat:
		/* dirfd (a) is always AT_FDCWD here - every path xbox_files.c
		passes down is already an absolute, translated sdmc: path
		(platform_translate_path), never a relative one needing a real
		directory fd to resolve against */
		return host_open((const char *)(uintptr_t)b, (int)c, (int)d);
	case SYS_fcntl:
		/* musl's open() always issues F_SETFD/FD_CLOEXEC separately
		when the caller passed O_CLOEXEC (xbox_files.c always does) -
		on top of, not instead of, passing O_CLOEXEC to the openat
		syscall itself, where host_open already honors it for real.
		musl doesn't check this call's result, and nothing here execs
		a second program for close-on-exec to matter against anyway;
		answering anything but ENOSYS just stops it logging noise on
		every single file open. */
		return 0;
	case SYS_close:
		return host_close((int)a);
	case SYS_read:
		return host_read((int)a, (void *)(uintptr_t)b, (unsigned long)c);
	case SYS_lseek:
		return (long)host_lseek((int)a, (long long)b, (int)c);
	case SYS_pread64:
		/* xbox_files.c's read_some() positioned path - cache_file_read's
		own call chain (source/cache/cache_files_windows.c), the one
		call site that will matter most once startup gets this far:
		every real map/tag read goes through here. Same seek-then-read
		reasoning as SYS_pwrite64 below. */
		host_lseek((int)a, (long long)d, 0 /* SEEK_SET */);
		return host_read((int)a, (void *)(uintptr_t)b, (unsigned long)c);
	case SYS_pwrite64:
		/* xbox_files.c's write_at() - the one real caller, for its
		OVERLAPPED/positioned WriteFile path (cache file header
		writes) - calls plain pwrite(), not two separate lseek()+
		write() calls, so there's no separate SYS_lseek this guest
		already makes to piggyback on. No real fd is ever shared
		between threads here (checked: host_threads.c's one extra
		real thread never touches file I/O), so a plain seek-then-
		write is exactly as atomic as this guest needs, not just a
		convenient shortcut. */
		host_lseek((int)a, (long long)d, 0 /* SEEK_SET */);
		return host_write_fd((int)a, (const void *)(uintptr_t)b, (unsigned long)c);
	case SYS_exit:
	case SYS_exit_group:
		host_log("__guest_syscall: guest called exit()");
		for (;;)
			;
	default:
	{
		extern void platform_log(const char *format, ...);

		/* the bare host_log this used to be never said *which* number -
		spent more time guessing than it should have, more than once */
		platform_log("__guest_syscall: unimplemented syscall number %lld (a=%lld b=%lld c=%lld)",
			n, a, b, c);
		return -ENOSYS;
	}
	}
}
