/*
HOST_POSIX_IO.C

The host side of the raw open/read/write/close/lseek syscalls
guest_syscall.c now routes here (PORTING.md's "real file I/O"
milestone) - plain POSIX over devkitPro's sdmc: devoptab, which already
works (host_main.c's own fopen/fread for guest.elf and host.log use the
same underlying driver). The guest fd IS the real host fd: guest and
host share one process/one address space, so there is no separate
descriptor table to keep in sync, unlike Android's real cross-process
split.
*/

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <switch.h>

/* One lock over every read, write and seek: the guest's cache file
thread, its filesystem check thread and the main thread share
descriptors (the map file), and a positioned read is a seek then a read.
Unlocked, another thread's seek between the two read the map's textures
and vertices from the wrong offsets - different garbage every run. */
static Mutex s_io_lock;

/* guest_syscall.c passes `flags` straight from the guest's raw SYS_openat
argument - musl's aarch64 O_CREAT/O_EXCL/O_TRUNC/O_APPEND/O_CLOEXEC bit
values (0100/0200/01000/02000/02000000), not devkitA64 newlib's BSD-style
ones (_FCREAT 0x200/_FEXCL 0x800/_FTRUNC 0x400/_FAPPEND 0x8/_FNOINHERIT
0x40000 - see sys/_default_fcntl.h). O_RDONLY/WRONLY/RDWR (0/1/2) are the
one part every libc agrees on, so those two low bits pass through as-is;
everything else is translated explicitly rather than assumed to match. */
#define GUEST_O_CREAT    0100
#define GUEST_O_EXCL     0200
#define GUEST_O_TRUNC    01000
#define GUEST_O_APPEND   02000
#define GUEST_O_CLOEXEC  02000000

long host_open(const char *path, int guest_flags, int mode)
{
	int flags = guest_flags & 03; /* O_RDONLY/O_WRONLY/O_RDWR */
	int fd;

	if (guest_flags & GUEST_O_CREAT) flags |= O_CREAT;
	if (guest_flags & GUEST_O_EXCL) flags |= O_EXCL;
	if (guest_flags & GUEST_O_TRUNC) flags |= O_TRUNC;
	if (guest_flags & GUEST_O_APPEND) flags |= O_APPEND;
	if (guest_flags & GUEST_O_CLOEXEC) flags |= O_CLOEXEC;
	fd = open(path, flags, mode);
	return fd < 0 ? -errno : fd;
}

long host_read(int fd, void *buf, unsigned long count)
{
	long result;

	mutexLock(&s_io_lock);
	result = (long)read(fd, buf, count);
	if (result < 0)
		result = -errno;
	mutexUnlock(&s_io_lock);
	return result;
}

long host_write_fd(int fd, const void *buf, unsigned long count)
{
	long result;

	mutexLock(&s_io_lock);
	result = (long)write(fd, buf, count);
	if (result < 0)
		result = -errno;
	mutexUnlock(&s_io_lock);
	return result;
}

/* pread/pwrite as one operation under the lock, leaving the descriptor's
own position where it was, as the real calls do */
long host_pread(int fd, void *buf, unsigned long count, long long offset)
{
	long result;
	off_t saved;

	mutexLock(&s_io_lock);
	saved = lseek(fd, 0, SEEK_CUR);
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
		result = -errno;
	else
	{
		result = (long)read(fd, buf, count);
		if (result < 0)
			result = -errno;
	}
	if (saved >= 0)
		lseek(fd, saved, SEEK_SET);
	mutexUnlock(&s_io_lock);
	return result;
}

long host_pwrite(int fd, const void *buf, unsigned long count, long long offset)
{
	long result;
	off_t saved;

	mutexLock(&s_io_lock);
	saved = lseek(fd, 0, SEEK_CUR);
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
		result = -errno;
	else
	{
		result = (long)write(fd, buf, count);
		if (result < 0)
			result = -errno;
	}
	if (saved >= 0)
		lseek(fd, saved, SEEK_SET);
	mutexUnlock(&s_io_lock);
	return result;
}

long host_close(int fd)
{
	return close(fd) < 0 ? -errno : 0;
}

long long host_lseek(int fd, long long offset, int whence)
{
	long long result;

	mutexLock(&s_io_lock);
	result = (long long)lseek(fd, (off_t)offset, whence);
	if (result < 0)
		result = -errno;
	mutexUnlock(&s_io_lock);
	return result;
}

/* the guest's unlinkat/renameat (guest_syscall.c): xbox_files.c's
DeleteFileA and MoveFileA. Without these every delete failed, and the
saved game code's create-over-an-old-file failed with it. */
long host_unlink(const char *path)
{
	return unlink(path) < 0 ? -errno : 0;
}

long host_rename(const char *from, const char *to)
{
	return rename(from, to) < 0 ? -errno : 0;
}
