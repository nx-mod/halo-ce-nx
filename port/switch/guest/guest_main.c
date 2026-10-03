/*
GUEST_MAIN.C

The first milestone (PORTING.md): a minimal guest image that loads at a
fixed low address and calls back into the host across the import
boundary, proving the whole pipeline before any game code shows up.
*/

#include <stdint.h>

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
	.import_count = 1,
	.entry = (uint32_t)(uintptr_t)__guest_entry,
};

void __guest_entry(void)
{
	host_log("hello from the guest, running at a fixed low address");
}
