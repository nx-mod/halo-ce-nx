/*
GUEST_PTHREAD_STUBS.C

build_musl.sh deliberately excludes musl's real pthread_create and friends
(see its own comment) - this guest is single-threaded by design
(guest_tp.c's single static TLS block has no room for a second thread
regardless). Mutex/condvar operations are genuine no-ops: with only one
thread, "locked" and "unlocked" are the same state, and a condvar can
never have another thread to wake it, so wait is a bug to ever reach,
not something to fake correctly.

pthread_create fails outright (EAGAIN) rather than running the start
routine synchronously on the caller's stack: a caller that checks the
return value degrades (loses a background task) instead of silently
running reentrant code in the wrong place. If something load-bearing
needs a second thread, this needs real thread support (an svcCreateThread
host import), not a sharper stub.
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

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) { (void)mutex; (void)attr; return 0; }
int pthread_mutex_lock(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *mutex) { (void)mutex; return 0; }

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) { (void)cond; (void)attr; return 0; }
int pthread_cond_broadcast(pthread_cond_t *cond) { (void)cond; return 0; }

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	/* no other thread exists to signal this - reaching it at all means a
	caller assumed real concurrency that isn't here yet */
	(void)cond; (void)mutex;
	return 0;
}
