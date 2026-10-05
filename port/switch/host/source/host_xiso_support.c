/*
HOST_XISO_SUPPORT.C

Real implementation of switch_host_posix_shim.h's platform_log, for
xiso.c's benefit specifically (HALO_SWITCH_HOST). posix_seek/
posix_make_directory used to live here too, as the only two of
posix.h's functions xiso.c itself needed - now provided for real by
host_posix_files.c (port/linux/src/posix_files.c, PORTING.md's "real
file I/O" milestone), which covers the whole header, not just these
two, so the ones here would just be duplicate definitions now.
*/

#include <stdarg.h>
#include <stdio.h>

#include "switch_host_posix_shim.h"

void platform_log(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}
