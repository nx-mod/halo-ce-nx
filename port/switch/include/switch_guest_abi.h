/*
SWITCH_GUEST_ABI.H

The contract between the two halves of the Switch port (see PORTING.md):

- the guest: the game, compiled -mabi=ilp32 (devkitA64's native AArch64
  ILP32 ABI - real pointers are hardware-truncated to 32 bits, same idea
  as halo-ce-universal's Android port, but a real ELF ABI here, not a
  Mach-O conversion), linked flat at GUEST_IMAGE_BASE;
- the host: an ordinary 64-bit NRO (libnx, SDL2, GLES3) that loads the
  guest's ELF32 image, places its segments with svcMapMemory (data) and
  svcCreateCodeMemory/svcControlCodeMemory (.text - see PORTING.md for
  the two gotchas: cache maintenance, and writing through the owner view),
  fills in the import table, and jumps to the entry point.

The guest calls the host through import stubs that jump through a table
of 64-bit function pointers the host fills in at load time (mirrors
halo-ce-universal's port/android/include/halo_android_abi.h). Only types
whose layout agrees between ILP32 and LP64 cross this boundary: 32-bit
integers, 64-bit integers (long long), floats, and pointers (which
-mabi=ilp32 always passes zero-extended).

Included by both halves.
*/

#ifndef __SWITCH_GUEST_ABI_H
#define __SWITCH_GUEST_ABI_H

#include <stdint.h>

/* chosen from the range nx-mapmem-poc validated end to end on hardware
(0x08000000-0x7FF00000): leaves the most room below 0x80000000 for the
guest's own heap/window growth. */
#define GUEST_IMAGE_BASE 0x40000000u

#define GUEST_MAGIC 0x4f4c4148u /* 'HALO' */
#define GUEST_ABI_VERSION 1

/* at GUEST_IMAGE_BASE */
struct guest_header
{
	uint32_t magic;
	uint32_t abi_version;
	uint32_t image_end;        /* end of .bss, size to commit */
	uint32_t import_table;     /* address of uint64_t[import_count], filled by the host */
	uint32_t import_names;     /* address of import_count NUL-terminated names, back to back */
	uint32_t import_count;     /* address of a uint32_t holding the count - not the
	                               count itself, since that's only known at link time
	                               as a symbol's value, not a compile-time constant
	                               this static initializer could fold in directly
	                               (mirrors halo-ce-universal's Android host_loader.c) */
	uint32_t entry;            /* void __guest_entry(void) */
};

#endif /* __SWITCH_GUEST_ABI_H */
