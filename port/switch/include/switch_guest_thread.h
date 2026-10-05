#ifndef SWITCH_GUEST_THREAD_H
#define SWITCH_GUEST_THREAD_H

/* Per-thread state the platform layer keeps without __thread: the guest
ELF has no TLS segment, so a __thread variable is addressed from
tpidr_el0 - the host's libnx thread area, shared with other threads'
slots and their IPC buffers. This lives instead in the last 64 bytes of
each thread's 512-byte guest TLS block (guest_tp.c; musl's struct
pthread takes the first 112). */
struct guest_thread_port_data
{
	unsigned long last_error;
	void *apc_head;
	void *apc_tail;
};

struct guest_thread_port_data *__guest_thread_port_data(void);

#endif
