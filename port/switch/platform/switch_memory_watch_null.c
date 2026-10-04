/*
SWITCH_MEMORY_WATCH_NULL.C

The real port/linux/src/memory_watch.c tracks which pages of a texture's
guest-memory backing changed since it was last uploaded by
write-protecting them and catching the SIGSEGV on the next write. No
user-space signal handling exists on Switch (per libdol-nx - PORTING.md's
"Unexplored" section flagged this before any of this file's callers
existed yet).

Fallback already decided there: drop incremental tracking, always
re-upload. memory_watch_generation returning a strictly increasing value
on every single call makes every caller's "has this changed since my
last stored generation" check always true - not a real generation count,
just a free-running counter that guarantees "yes, dirty, re-upload",
every time, for every address. Correct, just unoptimized: every texture
update becomes a full re-upload instead of only the changed pages.
*/

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	static unsigned long counter;

	(void)address;
	(void)size;
	return ++counter;
}

unsigned long memory_watch_serial(void)
{
	static unsigned long counter;

	return ++counter;
}
