/*
GUEST_IMPORTS.S

Import stubs: the guest calls these like normal functions, but the real
address isn't known until the host resolves it at load time - so each
stub loads a 64-bit pointer from its slot in .import_table (host-filled)
and tail-branches to it (BR, not BLR, so the host function's own RET
returns straight to the guest's original caller).

Two imports now (milestone 2): host_log (a NUL-terminated string) and
host_write (an explicit-length buffer, for real stdout/stderr via
__guest_syscall's SYS_write/SYS_writev - see guest_syscall.c). A real
import list becomes a generator script (as halo-ce-universal's
tools/android_imports.py does) once there's more than a couple.
*/

.section .import_table, "aw"
.align 3
.global host_log_slot
host_log_slot:
	.quad 0
.global host_write_slot
host_write_slot:
	.quad 0

.section .import_names, "a"
.global guest_import_names
guest_import_names:
	.asciz "host_log"
	.asciz "host_write"

.section .text
.global host_log
.type host_log, %function
host_log:
	adrp x9, host_log_slot
	add x9, x9, :lo12:host_log_slot
	ldr x9, [x9]
	br x9

.global host_write
.type host_write, %function
host_write:
	adrp x9, host_write_slot
	add x9, x9, :lo12:host_write_slot
	ldr x9, [x9]
	br x9
