/*
SWITCH_POSIX_NULL.C

posix.h's boundary functions (port/linux/src/posix.h's own comment:
compiled under the host ABI, called from the game's MSVC-ABI side -
on Android, built straight into the 64-bit host and called from the
ILP32 guest, same shape Switch would need). No real host-side file
bridge exists yet (same milestone as D3D8/DirectSound/XInput/XNet -
see PORTING.md), so every call fails with ENOENT rather than guessing:
msvc_crt.c's halo_linux_fopen family calls these and already has a
real "file not found" path the game is built to handle.
*/

#include <errno.h>

#include "posix.h"

int posix_stat(const char *path, struct posix_file_information *information)
{
	(void)path;
	(void)information;
	errno = ENOENT;
	return -1;
}

int posix_fstat(int descriptor, struct posix_file_information *information)
{
	(void)descriptor;
	(void)information;
	errno = ENOENT;
	return -1;
}

int posix_truncate(int descriptor, posix_ulong size_low, posix_ulong size_high)
{
	(void)descriptor;
	(void)size_low;
	(void)size_high;
	errno = ENOENT;
	return -1;
}

int posix_make_directory(const char *path)
{
	(void)path;
	errno = ENOENT;
	return -1;
}
