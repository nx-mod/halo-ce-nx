/*
GUEST_TP.C

musl's per-thread state (errno, locale, file locks - see
pthread_arch.h's __get_tp) needs a valid struct pthread to point at.
struct pthread's first field is a self-pointer (TLS_ABOVE_TP is not
defined for this arch, so __pthread_self() is just __get_tp() with no
offset - the pointer below needs no adjustment either). Generously
sized and over-aligned since this file never includes musl's internal
pthread_impl.h to get the real size.

Real multithreading (PORTING.md's own milestone) needs a different one
of these per real thread, not one static buffer good only for the
first thread ever to ask - the host's own real __thread (already
correctly virtualized per real OS thread, since it's backed by the
same compiler/libnx machinery the host's own code uses) is the single
piece of plumbing that lets __guest_get_tp answer correctly regardless
of which real thread is calling, with nothing guest-side needing to
know how many real threads exist or which one this is.
*/

#include <stdint.h>

extern void *host_get_guest_tp(void);
extern void host_set_guest_tp(void *ptr);

static unsigned char main_thread_pthread_struct[512] __attribute__((aligned(16)));

uintptr_t __guest_get_tp(void)
{
	return (uintptr_t)host_get_guest_tp();
}

/* the last 64 bytes of the 512-byte block (struct pthread is 112 bytes,
at the start): ../include/switch_guest_thread.h */
#include "../include/switch_guest_thread.h"

struct guest_thread_port_data *__guest_thread_port_data(void)
{
	return (struct guest_thread_port_data *)((unsigned char *)host_get_guest_tp() + 448);
}

/* called once, from __guest_entry, before any musl function that might
need __get_tp (match_malloc et al. - see guest_runtime_init.c, called
right alongside it for the same reason). Every other real thread sets
its own block up itself, in switch_xbox_threads.c's trampoline. */
void __guest_tp_init_main_thread(void)
{
	*(uintptr_t *)main_thread_pthread_struct = (uintptr_t)main_thread_pthread_struct;
	host_set_guest_tp(main_thread_pthread_struct);
}
