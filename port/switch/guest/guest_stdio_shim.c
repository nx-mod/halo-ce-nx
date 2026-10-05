/*
GUEST_STDIO_SHIM.C

stdio/stdout.c's static FILE initializer references __stdout_write and
__stdio_seek, which are excluded from the musl build (build_musl.sh) -
both need bits/ioctl.h for a terminal check that's meaningless for a
guest stdout that's never a real terminal, just always headed to
host_write via __guest_syscall's SYS_writev path (guest_syscall.c).
Minimal stand-ins instead of porting the ioctl header for a check that
would always take the same branch anyway.
*/

#include <stddef.h>

#include "third_party/musl-1.2.5/src/internal/stdio_impl.h"

size_t __stdout_write(FILE *f, const unsigned char *buf, size_t len)
{
	f->write = __stdio_write;
	return __stdio_write(f, buf, len);
}

/* musl's own (src/stdio/__stdio_seek.c). Every FILE uses this, not just
stdout: a -1 stand-in failed every fseek/ftell in the game - progress_bar.c
sized default.xbe as -1 and halted on free(NULL). lseek on stdout's fd
fails on its own, as on Linux. */
off_t __lseek(int fd, off_t offset, int whence);

long long __stdio_seek(FILE *f, long long off, int whence)
{
	return __lseek(f->fd, off, whence);
}
