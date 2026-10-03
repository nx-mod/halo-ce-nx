/*
GUEST_RUNTIME_INIT.C

No real ELF loader ever ran for this guest - host_main.c just copies
segments and jumps to the entry point - so musl's global `libc` struct
(normally set up by __init_libc.c/__libc_start_main.c, both excluded;
see build_musl.sh) starts zeroed. That's mostly fine (it's designed to
have safe defaults), but two fields are dereferenced unconditionally
and crash: `auxv` is scanned by mallocng's get_random_secret()
(glue.h) for AT_RANDOM with no NULL check, and `page_size` backs the
PAGE_SIZE macro mallocng also depends on for its size-class math.

Call this once, before any other musl function, from __guest_entry.
*/

#include "third_party/musl-1.2.5/src/internal/libc.h"

/* AT_NULL-terminated and empty: nothing in here ever needs scanning
for real, just needs to not be a NULL pointer scanned at index 0. */
static size_t empty_auxv[2] = {0, 0};

void __guest_runtime_init(void)
{
	libc.auxv = empty_auxv;
	libc.page_size = 4096;
}
