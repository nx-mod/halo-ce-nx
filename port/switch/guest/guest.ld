/*
GUEST.LD

Layout of the guest image. Linked flat at GUEST_IMAGE_BASE
(switch_guest_abi.h): the host copies .text to memory it got from the
CreateCodeMemory/MapOwner/MapSlave dance, and .rodata/.data/.bss to
memory it got from svcMapMemory - both placed at the exact addresses
this script assigns, so the guest's own absolute addresses (every
ADRP/ADD pair the compiler emits) resolve correctly once copied.
*/

ENTRY(__guest_entry)

SECTIONS
{
	. = 0x40000000;
	.guest_header : { KEEP(*(.guest_header)) }
	.text : { *(.text .text.*) }
	. = ALIGN(0x1000);
	__guest_text_end = .;

	.rodata : { *(.rodata .rodata.*) }
	.import_table : { KEEP(*(.import_table)) }
	.import_names : { KEEP(*(.import_names)) }
	.data : { *(.data .data.*) }
	.bss : { *(.bss .bss.*) *(COMMON) }
	. = ALIGN(0x1000);
	__guest_image_end = .;

	/DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
