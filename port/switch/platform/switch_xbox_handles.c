/*
GUEST_XBOX_HANDLES.C

xbox_files.c (now part of SWITCH_PLATFORM_FILES, PORTING.md's "real file
I/O" milestone) needs a handful of generic Win32-kernel helpers
(platform_handle_new/get, platform_set_last_error_from_errno,
platform_unix_time_to_filetime/filetime_to_unix_time, platform_queue_apc)
that normally live in port/linux/src/xbox_kernel.c alongside Thread/
Event/Mutex/Wait support this guest has no use for yet (no real
threading - guest_pthread_stubs.c) and its own platform_log (which
would collide with guest_platform_stubs.c's). Copied here instead of
pulling in the whole file: these six are self-contained (no vita_host.h,
no SDL), and copying keeps every symbol this guest doesn't need yet -
CreateThread, CreateEvent, CreateMutex, WaitForSingleObject's real
backend - out of the link entirely, same as every other not-yet-ported
subsystem (PORTING.md's milestone 7).
*/

#include "platform.h"

#include <errno.h>
#include <stdlib.h>

#define PLATFORM_HANDLE_SIGNATURE 0x686e646cUL /* 'hndl' */

/* ---------- handles */

struct platform_handle *platform_handle_new(long type, void *data,
	void (*destroy)(struct platform_handle *handle))
{
	struct platform_handle *handle = calloc(1, sizeof(*handle));
	pthread_mutexattr_t attributes;

	if (!handle)
		return NULL;
	handle->signature = PLATFORM_HANDLE_SIGNATURE;
	handle->type = type;
	handle->data = data;
	handle->destroy = destroy;
	pthread_mutexattr_init(&attributes);
	pthread_mutex_init(&handle->lock, &attributes);
	pthread_mutexattr_destroy(&attributes);
	pthread_cond_init(&handle->condition, NULL);
	return handle;
}

struct platform_handle *platform_handle_get(HANDLE handle, long type)
{
	struct platform_handle *result = (struct platform_handle *)handle;

	/* GetCurrentProcess() and GetCurrentThread() are the pseudo handles -1
	and -2; any other value in the top page cannot be a heap pointer */
	if (!result || (unsigned long)handle >= 0xfffff000UL ||
		result->signature != PLATFORM_HANDLE_SIGNATURE ||
		(type && result->type != type))
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return NULL;
	}
	return result;
}

void platform_handle_signal(struct platform_handle *handle)
{
	pthread_mutex_lock(&handle->lock);
	handle->signaled = TRUE;
	pthread_cond_broadcast(&handle->condition);
	pthread_mutex_unlock(&handle->lock);
}

BOOL WINAPI CloseHandle(HANDLE object)
{
	struct platform_handle *handle = platform_handle_get(object, 0);

	if (!handle)
		return FALSE;
	if (handle->destroy)
		handle->destroy(handle);
	handle->signature = 0;
	pthread_cond_destroy(&handle->condition);
	pthread_mutex_destroy(&handle->lock);
	free(handle);
	return TRUE;
}

/* ---------- last error */

DWORD platform_set_last_error_from_errno(int error_number)
{
	DWORD error;

	switch (error_number)
	{
	case 0: error = ERROR_SUCCESS; break;
	case ENOENT: error = ERROR_FILE_NOT_FOUND; break;
	case ENOTDIR: error = ERROR_PATH_NOT_FOUND; break;
	case EACCES: case EPERM: case EROFS: error = ERROR_ACCESS_DENIED; break;
	case EEXIST: error = ERROR_ALREADY_EXISTS; break;
	case ENOTEMPTY: error = ERROR_DIR_NOT_EMPTY; break;
	case ENOSPC: error = ERROR_DISK_FULL; break;
	case ENOMEM: error = ERROR_NOT_ENOUGH_MEMORY; break;
	case EBADF: error = ERROR_INVALID_HANDLE; break;
	case EINVAL: error = ERROR_INVALID_PARAMETER; break;
	case EMFILE: case ENFILE: error = ERROR_TOO_MANY_OPEN_FILES; break;
	case EBUSY: error = ERROR_BUSY; break;
	default: error = ERROR_GEN_FAILURE; break;
	}
	SetLastError(error);
	return error;
}

/* ---------- asynchronous procedure calls (ReadFileEx/WriteFileEx) */

struct platform_apc
{
	struct platform_apc *next;
	platform_apc_routine routine;
	void *context[3];
};

static struct platform_apc *platform_apc_head;
static struct platform_apc *platform_apc_tail;

void platform_queue_apc(platform_apc_routine routine, void *context0, void *context1, void *context2)
{
	struct platform_apc *apc = calloc(1, sizeof(*apc));

	if (!apc)
		return;
	apc->routine = routine;
	apc->context[0] = context0;
	apc->context[1] = context1;
	apc->context[2] = context2;
	if (platform_apc_tail)
		platform_apc_tail->next = apc;
	else
		platform_apc_head = apc;
	platform_apc_tail = apc;
}

long platform_run_apcs(void)
{
	long count = 0;

	while (platform_apc_head)
	{
		struct platform_apc *apc = platform_apc_head;

		platform_apc_head = apc->next;
		if (!platform_apc_head)
			platform_apc_tail = NULL;
		apc->routine(apc->context[0], apc->context[1], apc->context[2]);
		free(apc);
		count++;
	}
	return count;
}

/* ---------- file times */

/* seconds between 1601-01-01 and 1970-01-01 */
#define FILETIME_UNIX_EPOCH_SECONDS 11644473600ULL

void platform_unix_time_to_filetime(unsigned long seconds, unsigned long nanoseconds, FILETIME *file_time)
{
	unsigned long long value = ((unsigned long long)seconds + FILETIME_UNIX_EPOCH_SECONDS) * 10000000ULL +
		nanoseconds / 100;

	file_time->dwLowDateTime = (DWORD)value;
	file_time->dwHighDateTime = (DWORD)(value >> 32);
}

void platform_filetime_to_unix_time(const FILETIME *file_time, unsigned long *seconds, unsigned long *nanoseconds)
{
	unsigned long long value = ((unsigned long long)file_time->dwHighDateTime << 32) | file_time->dwLowDateTime;
	unsigned long long total_seconds = value / 10000000ULL;

	*seconds = total_seconds > FILETIME_UNIX_EPOCH_SECONDS ? (unsigned long)(total_seconds - FILETIME_UNIX_EPOCH_SECONDS) : 0;
	*nanoseconds = (unsigned long)(value % 10000000ULL) * 100;
}
