/*
GUEST_MAIN.C

Milestone 2 (PORTING.md): exercises the ported musl for real - malloc,
printf (through stdout, so through write() and __guest_syscall), a
snprintf round trip - not just a single hand-crafted host_log call.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/switch_guest_abi.h"

extern void host_log(const char *text);
extern char __guest_image_end[];
extern uint64_t host_log_slot;
extern char guest_import_names[];

void __guest_entry(void);

__attribute__((section(".guest_header"), used)) const struct guest_header guest_header = {
	.magic = GUEST_MAGIC,
	.abi_version = GUEST_ABI_VERSION,
	.image_end = (uint32_t)(uintptr_t)__guest_image_end,
	.import_table = (uint32_t)(uintptr_t)&host_log_slot,
	.import_names = (uint32_t)(uintptr_t)guest_import_names,
	.import_count = 2,
	.entry = (uint32_t)(uintptr_t)__guest_entry,
};

extern void __guest_runtime_init(void);

void __guest_entry(void)
{
	__guest_runtime_init();
	host_log("guest entry reached; exercising musl now");

	printf("printf works: %d + %d = %d\n", 2, 2, 2 + 2);
	fflush(stdout);

	char *buf = malloc(128);
	if (!buf)
	{
		host_log("malloc(128) returned NULL");
		return;
	}
	snprintf(buf, 128, "malloc'd at %p, strlen says %zu", buf, strlen("round trip"));
	host_log(buf);
	free(buf);

	/* a few more, to exercise the bump allocator past one block */
	for (int i = 0; i < 8; i++)
	{
		char *p = malloc(4096);
		if (!p)
		{
			host_log("malloc(4096) returned NULL partway through the loop");
			return;
		}
		memset(p, (unsigned char)i, 4096);
		free(p);
	}

	host_log("guest entry done - musl held up");
}
