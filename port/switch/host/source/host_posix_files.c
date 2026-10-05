/*
HOST_POSIX_FILES.C

port/linux/src/posix_files.c, unmodified, compiled for this host
(PORTING.md's "real file I/O" milestone): posix.h's own comment already
describes exactly this shape ("Android compiles these files into its
64-bit host and calls it from its ILP32 guest") - the file is already
generic enough (its own __LP64__ branch handles the opaque directory
handle indirection a 32-bit guest needs) that it needed no changes at
all, same as host_xiso.c's own #include of xiso.c.
*/

#define _FILE_OFFSET_BITS 64

/* devkitA64's newlib only exposes UTIME_OMIT/UTIME_NOW in sys/stat.h
under __CYGWIN__ or __rtems__, not for this (libnx) target - even
though utimensat() itself is declared and presumably implemented
against newlib's own shared sentinel values regardless (unlike musl's
very different 0x3fffffff/0x3ffffffe, which would be meaningless here -
this calls the HOST's own utimensat, not musl's). Matching exactly what
that same header spells out for its other targets, since that's the
best evidence available for what this implementation actually checks. */
#define UTIME_NOW -2L
#define UTIME_OMIT -1L

/* libnx's SD card driver has no chmod (ENOSYS), and FAT has no Unix
permission bits to set. posix_set_read_only - SetFileAttributesA, which
the game's file_delete calls before DeleteFileA - failed every time,
so no delete was ever attempted (error 0x1f, ERROR_GEN_FAILURE). */
#include <sys/stat.h>
static int switch_chmod(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return 0;
}
#define chmod switch_chmod

#include "../../../linux/src/posix_files.c"
