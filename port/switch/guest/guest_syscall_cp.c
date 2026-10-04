/*
GUEST_SYSCALL_CP.C

musl's "cancellable syscall" entry point (src/internal/syscall.h:
__syscall_cp(n,a,b,c,d,e,f), used by open/close/read/write so a blocking
call can be interrupted by pthread_cancel on another thread) - needed
even with no real thread support, since those four functions call it
unconditionally, not just when cancellation is actually possible.

This guest has no second thread to ever request a cancellation
(guest_pthread_stubs.c: pthread_create fails outright), so "cancellable"
and "ordinary" are the same syscall here - a plain pass-through to the
same dispatcher every other syscall already goes through
(libc/arch/aarch64_ilp32/syscall_arch.h's __syscallN wrappers).
*/

long __guest_syscall(long long n, long long a, long long b, long long c, long long d, long long e, long long f);

long __syscall_cp(long long n, long long a, long long b, long long c, long long d, long long e, long long f)
{
	return __guest_syscall(n, a, b, c, d, e, f);
}
