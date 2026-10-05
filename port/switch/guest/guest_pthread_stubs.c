/*
GUEST_PTHREAD_STUBS.C

build_musl.sh deliberately excludes musl's real pthread_create and
friends (see its own comment) - nothing in this guest calls musl's
own pthread API to create a thread, so this stays rejected (EAGAIN)
regardless of the below.

Two real threads now exist (PORTING.md's "real threading" and "wire
in audio/controls" milestones: the cache-file worker and the audio
mixer, both created through the game's own CreateThread,
switch_xbox_threads.c/host_threads.c - a different, real path from
musl's own pthread_create, each with its own TLS block, guest_tp.c).
port/linux/src/dsound_sdl.c's mixer_lock/parameter_lock (now part of
SWITCH_PLATFORM_FILES, guarding real concurrent access between the
game/tick thread and the audio thread) are genuinely load-bearing, so
mutex (not condvar - that file never uses one, and nothing else here
does either) is real now: a plain spinlock over the first word of the
opaque pthread_mutex_t, using the same compiler atomics real AArch64
hardware backs regardless of which real OS thread (main, cache,
audio) is spinning - no host import needed, this needs nothing a host
import could do that a guest-local atomic instruction doesn't already.
PTHREAD_MUTEX_INITIALIZER is all-zero (pthread.h), matching "0 =
unlocked" exactly, so statically-initialized mutexes (dsound_sdl.c's)
need no separate runtime init to be correct.

pthread_create (musl's own) fails outright (EAGAIN) rather than
running the start routine synchronously on the caller's stack: a
caller that checks the return value degrades (loses a background
task) instead of silently running reentrant code in the wrong place.
*/

#include <errno.h>
#include <pthread.h>

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
	void *(*start)(void *), void *arg)
{
	(void)thread; (void)attr; (void)start; (void)arg;
	return EAGAIN;
}

int pthread_attr_init(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) { (void)attr; (void)state; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size) { (void)attr; (void)size; return 0; }

int pthread_attr_getstack(const pthread_attr_t *attr, void **addr, size_t *size)
{
	(void)attr;
	if (addr) *addr = 0;
	if (size) *size = 0;
	return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr) { (void)thread; (void)attr; return 0; }
int pthread_detach(pthread_t thread) { (void)thread; return 0; }

int pthread_mutexattr_init(pthread_mutexattr_t *attr) { (void)attr; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) { (void)attr; return 0; }

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	(void)attr;
	__atomic_store_n((int *)mutex, 0, __ATOMIC_SEQ_CST);
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) { (void)mutex; return 0; }

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int expected;

	do
	{
		expected = 0;
	} while (!__atomic_compare_exchange_n((int *)mutex, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	int expected = 0;

	return __atomic_compare_exchange_n((int *)mutex, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
		? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	__atomic_store_n((int *)mutex, 0, __ATOMIC_SEQ_CST);
	return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) { (void)cond; (void)attr; return 0; }
int pthread_cond_destroy(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_broadcast(pthread_cond_t *cond) { (void)cond; return 0; }

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	/* real now for mutex (above); no caller anywhere in the build
	actually waits on a condvar that's ever signaled for real (checked
	directly - dsound_sdl.c, the one file that needed real mutexes,
	uses none), so this still just returns immediately rather than
	genuinely blocking */
	(void)cond; (void)mutex;
	return 0;
}
