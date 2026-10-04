/*
SWITCH_HOST_POSIX_SHIM.H

The three port/linux/src/platform.h/posix.h names xiso.c actually calls
(platform_log, posix_seek, posix_make_directory) plus the two scalar
typedefs it uses (posix_ulong, posix_long) - real implementations in
host_xiso_support.c, using libnx's real sdmc: filesystem access, not
placeholders. See xiso.c's own comment for why this exists instead of
including the real platform.h/posix.h here.
*/

#ifndef __SWITCH_HOST_POSIX_SHIM_H
#define __SWITCH_HOST_POSIX_SHIM_H

typedef long posix_long;
typedef unsigned long posix_ulong;

void platform_log(const char *format, ...);
int posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high);
int posix_make_directory(const char *path);

#endif
