/*
HOST_XISO_SUPPORT.C

Real implementations of switch_host_posix_shim.h's three names, for
xiso.c's benefit specifically (HALO_SWITCH_HOST) - plain POSIX/libnx
calls on the host's own sdmc: filesystem access, the same real access
host_main.c already uses for host.log and guest.elf.
*/

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "switch_host_posix_shim.h"

void platform_log(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

int posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high)
{
	long long offset = ((long long)(unsigned long)offset_high << 32) | (unsigned long)offset_low;
	long long result = lseek(descriptor, offset, whence);

	if (result < 0)
		return -1;
	if (position_low)
		*position_low = (posix_ulong)(unsigned long long)result;
	if (position_high)
		*position_high = (posix_ulong)((unsigned long long)result >> 32);
	return 0;
}

int posix_make_directory(const char *path)
{
	if (mkdir(path, 0777) == 0)
		return 0;
	return -1;
}
