/*
GUEST_TP.C

musl's per-thread state (errno, locale, file locks - see
pthread_arch.h's __get_tp) needs a valid struct pthread to point at.
Single-threaded for now (thread/pthread_create.c is excluded from the
build - see build_musl.sh): one static, zeroed buffer, generously sized
and over-aligned since this file never includes musl's internal
pthread_impl.h to get the real size. struct pthread's first field is a
self-pointer (TLS_ABOVE_TP is not defined for this arch, so
__pthread_self() is just __get_tp() with no offset - the pointer below
needs no adjustment either).

Real multithreading - a per-host-thread one of these, created when the
host actually starts a thread - is a later milestone.
*/

#include <stdint.h>

static unsigned char main_thread_pthread_struct[512] __attribute__((aligned(16)));

uintptr_t __guest_get_tp(void)
{
	*(uintptr_t *)main_thread_pthread_struct = (uintptr_t)main_thread_pthread_struct;
	return (uintptr_t)main_thread_pthread_struct;
}
