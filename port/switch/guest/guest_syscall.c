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
extern long host_unlink(const char *path);
extern long host_rename(const char *from, const char *to);
extern long host_pread(int fd, void *buf, unsigned long count, long long offset);
extern long host_pwrite(int fd, const void *buf, unsigned long count, long long offset);

extern char __guest_heap_start[];
extern char __guest_heap_end[];

#define SYS_fcntl 25
#define SYS_unlinkat 35
#define SYS_renameat 38
#define SYS_renameat2 276
#define SYS_openat 56
#define SYS_close 57
#define SYS_lseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_readv 65
#define SYS_writev 66
#define SYS_pread64 67
#define SYS_pwrite64 68
#define SYS_munmap 215
#define SYS_madvise 233
#define SYS_brk 214
#define SYS_mmap 222
#define SYS_exit 93
#define SYS_exit_group 94
#define SYS_futex 98
#define SYS_nanosleep 101
#define SYS_clock_gettime 113
#define SYS_clock_nanosleep 115
#define SYS_gettimeofday 169

#define ENOMEM 12
#define ENOSYS 38

static uintptr_t heap_cursor;

struct guest_iovec
{
	uint32_t iov_base; /* a real pointer in this process, just 32-bit-wide */
	uint32_t iov_len;
};

/* Regions given back by munmap. oldmalloc mmaps every large allocation
on its own and munmaps it on free, with the same page-rounded length;
without reuse every freed large block leaked (and the 32 MB heap ran
out a few seconds into the main loop). */
#define MAXIMUM_FREE_REGIONS 256
static struct { uintptr_t base, size; } free_regions[MAXIMUM_FREE_REGIONS];
static int free_region_count;

static void remove_free_region(int index)
{
	free_regions[index] = free_regions[--free_region_count];
}

static long do_mmap(long long length)
{
	uintptr_t aligned = (length + 0xfff) & ~(uintptr_t)0xfff;
	int i;

	if (!heap_cursor)
		heap_cursor = (uintptr_t)__guest_heap_start;
	for (i = 0; i < free_region_count; i++)
	{
		if (free_regions[i].size >= aligned)
		{
			uintptr_t result = free_regions[i].base;

			free_regions[i].base += aligned;
			free_regions[i].size -= aligned;
			if (!free_regions[i].size)
				remove_free_region(i);
			/* anonymous mmap is zero-filled; oldmalloc's calloc relies on it */
			__builtin_memset((void *)result, 0, aligned);
			return (long)result;
		}
	}
	if (heap_cursor + aligned > (uintptr_t)__guest_heap_end)
	{
		host_log("__guest_syscall: mmap: guest heap exhausted (see guest.ld's HEAP_SIZE)");
		return -ENOMEM;
	}
	uintptr_t result = heap_cursor;
	heap_cursor += aligned;
	return (long)result;
}

static long do_munmap(uintptr_t base, long long length)
{
	uintptr_t size = (length + 0xfff) & ~(uintptr_t)0xfff;
	int i;

	if (base < (uintptr_t)__guest_heap_start || base + size > heap_cursor)
		return 0;
	/* merge with free neighbours */
	for (i = 0; i < free_region_count; )
	{
		if (free_regions[i].base + free_regions[i].size == base)
		{
			base = free_regions[i].base;
			size += free_regions[i].size;
			remove_free_region(i);
		}
		else if (base + size == free_regions[i].base)
		{
			size += free_regions[i].size;
			remove_free_region(i);
		}
		else
			i++;
	}
	if (base + size == heap_cursor)
		heap_cursor = base;
	else if (free_region_count < MAXIMUM_FREE_REGIONS)
	{
		free_regions[free_region_count].base = base;
		free_regions[free_region_count].size = size;
		free_region_count++;
	}
	return 0;
}

/* Time. Every clock is the same monotonic counter (CNTPCT_EL0, readable
at EL0): no wall clock yet, and every caller found so far only measures
intervals. With time_t and long both 32-bit here and no 64-bit time
syscall, musl hands its own struct timespec/timeval straight through:
two 32-bit words, seconds then nanoseconds/microseconds. */
static unsigned long long now_ns(void)
{
	unsigned long long tick, frequency;

	__asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (tick));
	__asm__("mrs %0, cntfrq_el0" : "=r" (frequency));
	/* split, so tick * 1e9 cannot overflow */
	return (tick / frequency) * 1000000000ULL + (tick % frequency) * 1000000000ULL / frequency;
}

extern long host_event_create(int auto_clear);
extern long host_event_wait(long handle, long long timeout_ns);

/* a real wait on a host event nothing ever signals */
static void sleep_ns(long long ns)
{
	static long event;

	if (ns <= 0)
		return;
	if (!event)
		event = host_event_create(1);
	if (event)
		host_event_wait(event, ns);
}

static long long timespec_ns(const int32_t *ts)
{
	return (long long)ts[0] * 1000000000LL + ts[1];
}

long __guest_syscall(long long n, long long a, long long b, long long c, long long d, long long e, long long f)
{
	switch (n)
	{
	case SYS_mmap:
	case SYS_munmap:
	{
		/* oldmalloc calls these outside its own locks, from every real
		thread (cache worker, audio, input, main) */
		static int heap_lock;
		long result;

		while (__atomic_exchange_n(&heap_lock, 1, __ATOMIC_ACQUIRE))
			;
		result = n == SYS_mmap ? do_mmap(b) : do_munmap((uintptr_t)a, b);
		__atomic_store_n(&heap_lock, 0, __ATOMIC_RELEASE);
		return result;
	}
	case SYS_readv:
	{
		/* musl's FILE reads (__stdio_read: the caller's buffer, then the
		FILE's own) - fread failed without it */
		const struct guest_iovec *iov = (const struct guest_iovec *)(uintptr_t)b;
		long long total = 0;

		for (long long i = 0; i < c; i++)
		{
			long got;

			if (!iov[i].iov_len)
				continue;
			got = host_read((int)a, (void *)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
			if (got < 0)
				return total ? total : got;
			total += got;
			if ((unsigned long)got < iov[i].iov_len)
				break;
		}
		return total;
	}
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
		/* one locked host operation: a seek then a read here raced with
		other threads reading the same descriptor (host_posix_io.c) */
		return host_pread((int)a, (void *)(uintptr_t)b, (unsigned long)c, (long long)d);
	case SYS_pwrite64:
		return host_pwrite((int)a, (const void *)(uintptr_t)b, (unsigned long)c, (long long)d);
	case SYS_clock_gettime:
	{
		/* d3d8_gl.c's vertical blank thread paces itself with this and
		clock_nanosleep; both missing made it run untimed */
		int32_t *ts = (int32_t *)(uintptr_t)b;
		unsigned long long ns = now_ns();

		ts[0] = (int32_t)(ns / 1000000000ULL);
		ts[1] = (int32_t)(ns % 1000000000ULL);
		return 0;
	}
	case SYS_gettimeofday:
	{
		int32_t *tv = (int32_t *)(uintptr_t)a;
		unsigned long long ns = now_ns();

		if (tv)
		{
			tv[0] = (int32_t)(ns / 1000000000ULL);
			tv[1] = (int32_t)(ns % 1000000000ULL / 1000);
		}
		return 0;
	}
	case SYS_nanosleep:
		sleep_ns(timespec_ns((const int32_t *)(uintptr_t)a));
		return 0;
	case SYS_clock_nanosleep:
	{
		long long ns = timespec_ns((const int32_t *)(uintptr_t)c);

		if (b & 1) /* TIMER_ABSTIME */
			ns -= (long long)now_ns();
		sleep_ns(ns);
		return 0;
	}
	case SYS_unlinkat:
		/* dirfd is always AT_FDCWD (paths are absolute sdmc: paths);
		flags would be AT_REMOVEDIR for rmdir, which nothing calls */
		return host_unlink((const char *)(uintptr_t)b);
	case SYS_renameat:
	case SYS_renameat2:
		return host_rename((const char *)(uintptr_t)b, (const char *)(uintptr_t)d);
	case SYS_madvise:
		/* oldmalloc's free hands large free spans back (MADV_DONTNEED) on
		every call; the memory just stays mapped here. Logging it as
		unimplemented - an SD card commit per call, 15000+ of them by the
		main menu - is what ran the game at about a frame a second. */
		return 0;
	case SYS_futex:
		/* musl's __wait/__wake (malloc's lock under contention). No real
		futex: a wait returns at once and __wait's caller re-checks the
		lock word, so contention becomes a short spin across cores -
		fine for malloc's brief critical sections. */
		return 0;
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
		/* the first few of each number, then every 1000th: one chatty
		syscall must not turn into an SD card write per call */
		static unsigned int counts[512];
		unsigned int count = n >= 0 && n < 512 ? ++counts[n] : 1;

		if (count <= 3 || count % 1000 == 0)
			platform_log("__guest_syscall: unimplemented syscall number %lld (a=%lld b=%lld c=%lld), call %u",
				n, a, b, c, count);
		return -ENOSYS;
	}
	}
}
