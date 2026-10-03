/*
GUEST_IMPORTS.S

Import stubs: the guest calls host_log() like any normal function, but
the real address isn't known until the host resolves it at load time
(ASLR, etc.) - so each stub just loads a 64-bit pointer from a fixed
slot in .import_table (which the host fills in) and tail-branches to
it. A plain BR (not BLR) so the host function's own RET returns straight
to the guest's original caller, skipping back over this stub.

One hand-written stub for this first milestone; a real import list
becomes a generator script (as halo-ce-universal's tools/android_imports.py
does) once there's more than one.
*/

.section .import_table, "aw"
.align 3
.global host_log_slot
host_log_slot:
	.quad 0

.section .import_names, "a"
.global guest_import_names
guest_import_names:
	.asciz "host_log"

.section .text
.global host_log
.type host_log, %function
host_log:
	adrp x9, host_log_slot
	add x9, x9, :lo12:host_log_slot
	ldr x9, [x9]
	br x9
