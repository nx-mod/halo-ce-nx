/*
GUEST_PTHREAD_STUBS.C

The pthread API the game and the shared port code use, on top of the
same host threads as the game's own CreateThread (switch_xbox_threads.c,
host_threads.c). build_musl.sh excludes musl's own pthread_create.

- pthread_create: a real host thread with its own TLS block (guest_tp.c).
  It used to return EAGAIN, so d3d8_gl.c's vertical blank thread never
  ran - and the main loop, which paces itself on the flip count that
  thread advances, waited forever.
- mutexes: a spinlock over the first word of the opaque pthread_mutex_t;
  PTHREAD_MUTEX_INITIALIZER is all-zero, which is "unlocked".
- condition variables: a sequence number in the first word. A wait
  unlocks, naps until a signal or broadcast changes it, then relocks.
  Spurious wakeups are allowed, so naps are short (0.2 ms).
*/

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

extern long host_create_thread(unsigned int guest_entry, unsigned int guest_arg, unsigned int stack_size,
	unsigned int tls_block);
extern void __guest_enable_locks(void);

#define GUEST_PTHREAD_STACK_SIZE 0x40000

struct guest_pthread_start
{
	void *(*start)(void *);
	void *arg;
};

static void guest_pthread_entry(void *context)
{
	struct guest_pthread_start start = *(struct guest_pthread_start *)context;

	free(context);
	start.start(start.arg);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
	void *(*start)(void *), void *arg)
{
	struct guest_pthread_start *context;
	void *tls_block;

	(void)attr;
	{
		extern void platform_log(const char *format, ...);

		platform_log("pthread_create: start routine %p", (void *)start);
	}
	__guest_enable_locks();
	context = malloc(sizeof(*context));
	tls_block = calloc(1, 512);
	if (!context || !tls_block)
	{
		free(context);
		free(tls_block);
		return EAGAIN;
	}
	context->start = start;
	context->arg = arg;
	/* musl's struct pthread begins with its self pointer (guest_tp.c) */
	*(uintptr_t *)tls_block = (uintptr_t)tls_block;
	if (!host_create_thread((unsigned int)(uintptr_t)guest_pthread_entry, (unsigned int)(uintptr_t)context,
		GUEST_PTHREAD_STACK_SIZE, (unsigned int)(uintptr_t)tls_block))
	{
		free(context);
		free(tls_block);
		return EAGAIN;
	}
	if (thread)
		*thread = (pthread_t)tls_block;
	return 0;
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

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	(void)attr;
	__atomic_store_n((int *)cond, 0, __ATOMIC_SEQ_CST);
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) { (void)cond; return 0; }

int pthread_cond_signal(pthread_cond_t *cond)
{
	__atomic_fetch_add((int *)cond, 1, __ATOMIC_SEQ_CST);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
	__atomic_fetch_add((int *)cond, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static long long clock_now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* deadline_ns < 0: no deadline. Every clock is the same counter here
(guest_syscall.c), so an absolute time from any clock compares. */
static int cond_wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, long long deadline_ns)
{
	static const struct timespec nap = {0, 200000};
	int sequence = __atomic_load_n((int *)cond, __ATOMIC_SEQ_CST);
	int result = 0;

	pthread_mutex_unlock(mutex);
	while (__atomic_load_n((int *)cond, __ATOMIC_SEQ_CST) == sequence)
	{
		if (deadline_ns >= 0 && clock_now_ns() >= deadline_ns)
		{
			result = ETIMEDOUT;
			break;
		}
		nanosleep(&nap, NULL);
	}
	pthread_mutex_lock(mutex);
	return result;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	return cond_wait_until(cond, mutex, -1);
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
	return cond_wait_until(cond, mutex, (long long)abstime->tv_sec * 1000000000LL + abstime->tv_nsec);
}
