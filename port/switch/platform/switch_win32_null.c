/*
SWITCH_WIN32_NULL.C

The "headless boot" Win32 file/handle/thread/time/memory API: every
entry point source/ calls directly (not through posix.h's boundary -
those are real, needing a host-side bridge, see switch_posix_null.c).
Failure returns use the real Win32 sentinel values where they differ
from plain 0 (INVALID_HANDLE_VALUE, INVALID_FILE_ATTRIBUTES,
INVALID_SET_FILE_POINTER, INVALID_FILE_SIZE, WAIT_FAILED) - getting
these wrong is exactly the kind of bug a caller's `== 0` vs
`== INVALID_HANDLE_VALUE` check would hide until a very confusing crash
later. See switch_d3d8_null.c's header comment for the generation
method and caveats; GetTickCount/QueryPerformanceCounter return 0 like
vita_host_time_us (guest_platform_stubs.c) for the same reason - no
host-synced clock import exists yet.
*/

#include "platform.h"

void *__stdcall CreateFileA(const char *, unsigned long, unsigned long, void *, unsigned long, unsigned long, void *)
{
	return (void *)(long)-1;
}

int __stdcall ReadFile(void *, void *, unsigned long, unsigned long *, struct _OVERLAPPED *)
{
	return 0;
}

int __stdcall WriteFile(void *, const void *, unsigned long, unsigned long *, struct _OVERLAPPED *)
{
	return 0;
}

int __stdcall ReadFileEx(void *, void *, unsigned long, struct _OVERLAPPED *, void (__stdcall *)(unsigned long, unsigned long, struct _OVERLAPPED *))
{
	return 0;
}

int __stdcall WriteFileEx(void *, const void *, unsigned long, struct _OVERLAPPED *, void (__stdcall *)(unsigned long, unsigned long, struct _OVERLAPPED *))
{
	return 0;
}

int __stdcall CloseHandle(void *)
{
	return 0;
}

unsigned long __stdcall GetFileSize(void *, unsigned long *)
{
	return 0xFFFFFFFF;
}

int __stdcall GetFileTime(void *, struct _FILETIME *, struct _FILETIME *, struct _FILETIME *)
{
	return 0;
}

int __stdcall SetFileTime(void *, const struct _FILETIME *, const struct _FILETIME *, const struct _FILETIME *)
{
	return 0;
}

unsigned long __stdcall SetFilePointer(void *, long, long *, unsigned long)
{
	return 0xFFFFFFFF;
}

int __stdcall SetEndOfFile(void *)
{
	return 0;
}

int __stdcall DeleteFileA(const char *)
{
	return 0;
}

int __stdcall MoveFileA(const char *, const char *)
{
	return 0;
}

int __stdcall CopyFileA(const char *, const char *, int)
{
	return 0;
}

int __stdcall CreateDirectoryA(const char *, void *)
{
	return 0;
}

int __stdcall RemoveDirectoryA(const char *)
{
	return 0;
}

void *__stdcall FindFirstFileA(const char *, struct _WIN32_FIND_DATAA *)
{
	return (void *)(long)-1;
}

int __stdcall FindNextFileA(void *, struct _WIN32_FIND_DATAA *)
{
	return 0;
}

unsigned long __stdcall GetFileAttributesA(const char *)
{
	return 0xFFFFFFFF;
}

int __stdcall GetFileAttributesExA(const char *, enum _GET_FILEEX_INFO_LEVELS, void *)
{
	return 0;
}

int __stdcall SetFileAttributesA(const char *, unsigned long)
{
	return 0;
}

int __stdcall GetDiskFreeSpaceExA(const char *, union _ULARGE_INTEGER *, union _ULARGE_INTEGER *, union _ULARGE_INTEGER *)
{
	return 0;
}

long __stdcall CompareFileTime(const struct _FILETIME *, const struct _FILETIME *)
{
	return 0;
}

void *__stdcall CreateThread(void *, unsigned long, unsigned long (__stdcall *)(void *), void *, unsigned long, unsigned long *)
{
	return 0;
}

unsigned long __stdcall ResumeThread(void *)
{
	return 0;
}

int __stdcall SetThreadPriority(void *, int)
{
	return 0;
}

int __stdcall GetExitCodeThread(void *, unsigned long *)
{
	return 0;
}

int __stdcall SwitchToThread(void)
{
	return 0;
}

void __stdcall Sleep(unsigned long)
{
	
}

unsigned long __stdcall SleepEx(unsigned long, int)
{
	return 0;
}

void *__stdcall CreateEventA(void *, int, int, const char *)
{
	return 0;
}

int __stdcall SetEvent(void *)
{
	return 0;
}

int __stdcall ResetEvent(void *)
{
	return 0;
}

void *__stdcall CreateMutexA(void *, int, const char *)
{
	return 0;
}

int __stdcall ReleaseMutex(void *)
{
	return 0;
}

unsigned long __stdcall WaitForSingleObject(void *, unsigned long)
{
	return 0xFFFFFFFF;
}

unsigned long __stdcall WaitForSingleObjectEx(void *, unsigned long, int)
{
	return 0xFFFFFFFF;
}

unsigned long __stdcall GetLastError(void)
{
	return 0;
}

void __stdcall SetLastError(unsigned long)
{
	
}

unsigned long __stdcall GetTickCount(void)
{
	return 0;
}

int __stdcall QueryPerformanceCounter(union _LARGE_INTEGER *)
{
	return 0;
}

int __stdcall QueryPerformanceFrequency(union _LARGE_INTEGER *)
{
	return 0;
}

void *__stdcall GlobalAlloc(unsigned int, unsigned long)
{
	return 0;
}

void *__stdcall GlobalReAlloc(void *, unsigned long, unsigned int)
{
	return 0;
}

void __stdcall GlobalMemoryStatus(struct _MEMORYSTATUS *)
{
	
}

void *__stdcall LocalFree(void *)
{
	return 0;
}

unsigned long __stdcall LocalSize(void *)
{
	return 0;
}

void __stdcall OutputDebugStringA(const char *)
{
	
}

int __stdcall VirtualProtect(void *, unsigned long, unsigned long, unsigned long *)
{
	return 0;
}

int __stdcall SystemTimeToFileTime(const struct _SYSTEMTIME *, struct _FILETIME *)
{
	return 0;
}

void __stdcall GetSystemTime(struct _SYSTEMTIME *)
{
	
}
