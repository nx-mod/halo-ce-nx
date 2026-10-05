/*
GUEST.LD

Two real segments now, not one merged RWX region (see PORTING.md,
milestone 2): :text (header + code, placed via the CodeMemory dance,
R-X) and :data (rodata, import table/names, data, bss, heap - plain
svcMapMemory, RW). The heap is unbacked address space past the real
.bss - __guest_syscall's mmap bump allocator (guest_syscall.c) hands
it out; the host zeroes the whole segment on load, so it starts clean
with no file content to copy there.
*/

ENTRY(__guest_entry)

PHDRS
{
	text PT_LOAD FLAGS(5); /* R-X */
	data PT_LOAD FLAGS(6); /* RW- */
}

HEAP_SIZE = 64M;

SECTIONS
{
	. = 0x40000000;
	.guest_header : { KEEP(*(.guest_header)) } :text
	.text : { *(.text .text.*) } :text
	. = ALIGN(0x1000);
	__guest_text_end = .;

	.rodata : { *(.rodata .rodata.*) } :data
	.import_table : { KEEP(*(.import_table)) } :data
	.import_names : { KEEP(*(.import_names)) } :data
	.data : { *(.data .data.*) } :data
	.bss : { *(.bss .bss.*) *(COMMON) } :data
	/* The Xbox contiguous memory window (switch_contiguous_memory.c's
	arena: the game state, tag, texture and sound caches) at a fixed
	address, not wherever the code before it ends. A campaign save loads
	only with the game state where it was when saved, so with the arena
	following the code, every build that grew the program by a page made
	every earlier save unloadable. ~10 MB of room for the code to grow;
	the link fails ("overlaps") rather than move it. */
	.contiguous 0x41000000 (NOLOAD) : { KEEP(*(.contiguous_arena)) } :data
	/* NOLOAD: counted in this segment's memsz (so the host actually
	allocates and zeroes it) but not its filesz (no 32 MiB of zero
	bytes bloating guest.elf on disk) - the same SHT_NOBITS treatment
	.bss itself gets, just needs saying explicitly for a custom section. */
	.heap (NOLOAD) : {
		__guest_heap_start = .;
		. += HEAP_SIZE;
		__guest_heap_end = .;
	} :data
	. = ALIGN(0x1000);
	__guest_image_end = .;

	/DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
