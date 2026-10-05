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

/* Must match posix.h's own __LP64__ branch exactly, not just be "some
32-bit-capable type": posix_seek is now also defined for real in
host_posix_files.c (port/linux/src/posix_files.c, included through the
real posix.h there), and both declarations resolve to the same linked
symbol. Plain `long` here, on this LP64 host, is 64-bit - a real
scalar-width ABI mismatch against that other, 32-bit, declaration of
the very same function, not just an unlikely-to-matter technicality. */
#ifdef __LP64__
typedef int posix_long;
typedef unsigned int posix_ulong;
#else
typedef long posix_long;
typedef unsigned long posix_ulong;
#endif

void platform_log(const char *format, ...);
int posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high);
int posix_make_directory(const char *path);

#endif
