/*
HOST_NVK_SHIMS.C

Only in the ZINK=1 build (the host on nxvk's OpenGL ES: Zink over the NVK
Vulkan driver). Mesa's utility code references a few POSIX functions
devkitA64's newlib lacks. Most sit on optional paths (the disk cache's
locking, driconf's per-app matching) where "not available" is a safe
answer; sysconf is the exception, since NVK sizes its memory heaps from
_SC_PHYS_PAGES and fails device creation on a negative answer.

Translated from nx-mod/dawn-nx's switch_smoke_test/nvk_switch_stubs.cpp,
which wii-nx runs on; its expat stubs are not needed here, the real
switch-libexpat being linked instead.
*/

#ifdef HOST_ZINK

#include <errno.h>
#include <malloc.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *pointer;

	if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0)
		return EINVAL;
	pointer = memalign(alignment, size);
	if (!pointer)
		return ENOMEM;
	*memptr = pointer;
	return 0;
}

long sysconf(int name)
{
	switch (name)
	{
	case _SC_PAGESIZE:
		return 0x1000;
	case _SC_PHYS_PAGES:
	{
		u64 total = 0;

		if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
			return -1;
		return (long)(total / 0x1000);
	}
	case _SC_NPROCESSORS_ONLN:
	case _SC_NPROCESSORS_CONF:
		/* the three cores an application gets: -1 collapsed every worker
		pool sized from it, the shader compiler's included, to one thread */
		return 3;
	default:
		return -1;
	}
}

uid_t geteuid(void) { return 0; }
uid_t getuid(void) { return 0; }
gid_t getegid(void) { return 0; }
gid_t getgid(void) { return 0; }

int getpwuid_r(uid_t uid, struct passwd *password, char *buffer, size_t size, struct passwd **result)
{
	(void)uid;
	(void)password;
	(void)buffer;
	(void)size;
	*result = NULL;
	return 0;
}

/* one process: no contention to arbitrate */
int flock(int fd, int operation)
{
	(void)fd;
	(void)operation;
	return 0;
}

int dirfd(DIR *directory)
{
	(void)directory;
	errno = ENOSYS;
	return -1;
}

int fstatat(int fd, const char *path, struct stat *buffer, int flag)
{
	(void)fd;
	(void)path;
	(void)buffer;
	(void)flag;
	errno = ENOSYS;
	return -1;
}

/* "compiled, matches nothing", which driconf's app matching tolerates */
int regcomp(regex_t *expression, const char *pattern, int flags)
{
	(void)pattern;
	(void)flags;
	if (expression)
		expression->re_nsub = 0;
	return 0;
}

int regexec(const regex_t *expression, const char *string, size_t count, regmatch_t *matches, int flags)
{
	(void)expression;
	(void)string;
	(void)count;
	(void)matches;
	(void)flags;
	return REG_NOMATCH;
}

void regfree(regex_t *expression)
{
	(void)expression;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
	(void)how;
	(void)set;
	if (old)
		sigemptyset(old);
	return 0;
}

#endif
