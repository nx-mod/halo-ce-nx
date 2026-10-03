# Porting to Switch

Base: `BirchWoodGod/halo-ce-vita` (not `halo-ce-universal`, not `Xita`).
History kept.

## Resolved: ILP32 with a real low fixed guest window — fully verified

`~/switch/nx-mapmem-poc`, on real hardware, all phases clean:

1. `svcMapMemory` moves real data to any address tried from `0x08000000`
   to `0x7FF00000` (all < 2 GiB). It's a **move** (switchbrew docs,
   confirmed on-device): the source is reprotected `Perm_None`, not a
   dual-alias. Control at `0x100000000` fails cleanly ("invalid memory
   region"), matching libdol-nx's finding above 2 GiB exactly.
2. The same holds at real scale: a 128 MiB move, data intact.
3. **Real executable code** at a chosen low address, actually called,
   returned the correct value. Mechanism: `svcCreateCodeMemory` +
   `svcControlCodeMemory` (`MapOwner`/`MapSlave`) — the same one shipped
   homebrew JIT (`mono-nx`) uses. Two gotchas hit and fixed along the
   way, both now documented in `nx-mapmem-poc`'s commit history:
   - D-cache/I-cache aren't coherent on ARM — need `armDCacheFlush` after
     writing, `armICacheInvalidate` before executing.
   - The payload must be written through the **owner view obtained after
     `MapOwner`**, not into the buffer passed to `svcCreateCodeMemory`
     beforehand — the latter leaves the real backing as whatever a fresh
     code-memory page defaults to (`0xffffffff`), not your data.
4. `gcc -mabi=ilp32` on devkitA64's `aarch64-none-elf-gcc` genuinely
   gives `sizeof(long) == sizeof(void*) == 4` and links as real flat
   **ELF32** AArch64 (`EXEC`, confirmed via `readelf`) — no Mach-O/clang
   Apple-target conversion needed at all, unlike Android's port.

**So: the project-wide pointer/`long`-width audit is unnecessary.** Build
the game's own source (already 32-bit-pointer-clean — it's the Vita
decomp) as genuine ILP32 AArch64, same general shape as
`halo-ce-universal`'s Android port, but with a validated real identity
mapping instead of Android's free `mmap(MAP_FIXED)`, and devkitA64's
native ELF toolchain instead of Android's Mach-O conversion pipeline.

Firmware note: self-process use of `svcCreateCodeMemory`/
`svcControlCodeMemory` needs Atmosphere's kernel patch on 5.0.0+ (stock
kernel errors otherwise). Confirmed present on the test hardware. Needs
documenting as a real CFW requirement for players, not just a dev detail.

## Plan: the guest/host split

1. **Guest** (the game, `source/` + whatever of the Vita `port/vita/`
   platform layer is still needed): compiled `-mabi=ilp32`, linked with
   a custom linker script to one fixed base (pick from the validated
   range — `0x40000000` leaves the most headroom below `0x80000000`).
   Produces a flat ELF32 image.
2. **Host** (normal 64-bit code: libnx, SDL2, GLES3/EGL via portlibs):
   loads that ELF32 file, copies `PT_LOAD` segments into memory obtained
   via `svcMapMemory` (data) and the `CreateCodeMemory`/`MapOwner`→write→
   `MapSlave` dance (`.text`), fills in an import table, jumps to entry.
   Same shape as `halo-ce-universal`'s Android
   `port/android/guest/runtime/guest_host.h` import-stub design —
   adapt, don't reinvent.
3. `port/linux/src/tag_relocate.c` (tag-pointer fixup via a runtime bias)
   needs **no changes** — tags only need to be real pointers within this
   process, which they now can be.
4. `port/switch/` (modeled on `port/vita/`): devkitA64 + libnx, SDL2 for
   input/audio/window (mature Switch portlib; Vita's sceGxm/pad calls get
   replaced either way), GLES3 (`GLES3/gl3.h`, `libEGL.a`, `libGLESv2.a`,
   Mesa/NVK via `libnvk`) for whatever `port/vita/platform`'s D3D8 shim
   did via sceGxm.

### First concrete milestone - done

`port/switch/guest` + `port/switch/host`, run on real hardware, first
try (`port/switch/milestone-1.log`): guest ELF32 loads, places itself at
`0x40000000` via the CodeMemory dance, calls back into host code through
a resolved import stub, and the string pointer crosses the boundary and
gets dereferenced correctly with **zero translation** - both sides see
the same real address. The whole architecture is now proven, not just
the memory mechanism underneath it.

Currently one merged R-X segment (no mutable guest data yet - a plain
`svcMapMemory` region, no CodeMemory dance, once there's real game
state to place). `tools/android_imports.py`-style stub generation still
needed once there's more than the one hand-written import.

## Guest libc: musl, ported from Android's arm64_32 arch files

devkitA64's own libc (newlib) is LP64-only - "incompatible" when linking
ILP32 objects, same wall Android hit. Fix: build a minimal musl 1.2.5
for the guest. Rather than deriving the ABI type-width table (int/long/
pointer sizes, syscall plumbing, pthread TP access, etc.) from scratch,
pulled Android's actual working `arm64_32` arch adaptation
(`port/android/guest/libc/arch/arm64_32`) and retargeted it for
devkitA64's ELF `-mabi=ilp32` instead of clang's Mach-O `arm64_32-apple-
watchos`. Changes needed, all in `port/switch/guest/libc/arch/
aarch64_ilp32`:

- `weak_alias` back to musl's stock GNU `__attribute__((weak, alias))`
  (Android's version spells it in Mach-O assembler, since they convert
  to ELF afterward - we're already real ELF, no conversion needed).
- `bits/errno.h`: copied from musl's real `arch/generic` (errno numbers
  are universal across architectures).
- `bits/float.h`: this one actually matters and took two tries.
  AArch64's `long double` is genuinely 128-bit IEEE quad - not a choice,
  `sizeof(long double) == 16` regardless of what this header claims -
  unlike Apple's arm64_32 where it truly is 64-bit, so Android's copy
  (claiming `LDBL_MANT_DIG 53`) doesn't fit our hardware. Using the real
  aarch64 128-bit values fixes compilation (a `sizeof` assert in
  `vfprintf.c`) but means musl's printf genuinely needs 128-bit
  (`tf`-mode) soft-float arithmetic at link time, and devkitA64's
  libgcc, same as its libc, is LP64-only. Since this game's MSVC
  heritage means `long double` is always just `double` in practice -
  nothing in 462K lines ever constructs a real 128-bit value - the
  pragmatic fix is `guest_softfloat_stubs.c`: the 9 missing routines
  (`__addtf3`, `__multf3`, `__netf2`, etc.) as loud stubs that log and
  trap if ever actually called, rather than a real quad-math port for a
  path nothing here exercises. If one ever fires, that assumption was
  wrong and this needs revisiting for real.
- `atomic_arch.h`: one clang-only builtin (`__builtin_arm_yield`, used
  in `a_spin`) replaced with the plain `yield` instruction GCC lacks a
  named builtin for.
- `syscall_arch.h` needed **no changes at all** - Android's version
  already routes every syscall through `__guest_syscall()` (implemented
  by the guest runtime, not the arch layer), since Apple's real syscall
  ABI isn't available to them either. Not yet implemented here - next
  concrete task (see below).
- `pthread_arch.h`'s thread-pointer access (`__guest_get_tp`) likewise
  expects the guest runtime to provide it. `guest_tp.c` here is a
  placeholder: one static zeroed `struct pthread`-sized buffer, enough
  for single-threaded operation (errno, locale, file locking all route
  through it). Real multithreading needs a per-host-thread one of these,
  made when the host actually starts a thread - later.

**Compiles clean: 778/778** of halo-ce-universal's curated musl file
list (`build_musl.sh`), after additionally excluding what needs real-OS
headers neither our arch dir nor Android's provides (confirms they
exclude these too, not just us): `dirent/*`, `poll`/`ppoll`, `statvfs`,
`__fdopen`/`fopencookie`/`__stdout_write`/`freopen`/`pclose`/
`__stdio_seek`, `isatty`/`tcgetpgrp`/`tcsetpgrp`/`faccessat`/`nice`,
`sysconf`, `__tz` (timezone - needs real zoneinfo data), the whole
`network/*` group (real sockets are a separate, later task), plus
`env/__libc_start_main.c` (we have our own guest entry) and
`thread/pthread_create.c`/`thread/__syscall_cp.c` (real thread creation
is later; cancellation points had an unrelated type-conflict bug not
yet investigated).

### Milestone 2 - done: musl actually running, on hardware

`port/switch/milestone-2.log`: real `printf` (through buffered stdio,
`write()`, `__guest_syscall`, a new `host_write` import to real
stdout/stderr), `malloc`/`snprintf`/`strlen`/`free`, and a loop of 4 KiB
malloc/memset/free - all clean on real hardware. The guest now has a
genuine two-segment layout (`guest.ld`, explicit `PHDRS`): R-X code via
the CodeMemory dance, RW data+32 MiB heap via plain `svcMapMemory`.
`guest_syscall.c`'s `__guest_syscall` dispatcher: `mmap` as a bump
allocator over that heap, `write`/`writev` to `host_write`, `brk`/
`munmap` as no-ops, `exit` halts.

Three real bugs found and fixed getting here, each worth knowing about
for whatever comes next:

- **Linker script bug**: the heap reservation wasn't inside an output
  section, so it never counted toward the segment's `memsz` - the host
  would have allocated far too little and the heap symbols would have
  pointed past the real mapped region. Fixed with an explicit `.heap
  (NOLOAD)` section (counted in `memsz`, not `filesz` - same treatment
  `.bss` gets automatically, just needs saying explicitly for a custom
  section).
- **`libc.auxv` is NULL**: no real ELF loader ever ran for this guest,
  so musl's global `libc` struct starts zeroed. mallocng's (then
  oldmalloc's `calloc`'s hook, same shape of issue) `get_random_secret()`
  scans `libc.auxv` for `AT_RANDOM` with no NULL check - crashed inside
  the allocator on the very first `malloc()`. Fixed: `guest_runtime_init.c`
  sets a static empty `auxv` and a real `page_size` before anything else
  runs.
- **mallocng → oldmalloc**: mallocng's `get_meta()` asserts a "secret"
  value stored at allocation time still matches at `free()` time (a
  hardening check) - crashed (`Undefined Instruction`/`BRK`) on the very
  first `free()`. Not root-caused; switched to musl's simpler classic
  allocator instead, which has no such cross-call invariant and is a
  better fit for this minimal a runtime regardless. Both fall back to
  `mmap()` the same way (`__expand_heap` tries `SYS_brk` first, our stub
  always fails that check, falls through to `mmap()` cleanly either way).

### Milestone 3 - done: real game source compiles, 83/85 on a representative sample

Compiled `source/math`, `source/cseries`, `source/memory`, `source/ai`
(85 files - a deliberately mixed sample: math, core utilities, caching,
AI) against the Switch ILP32 toolchain for the first time. Flags
modeled on `tools/vita_build.py`'s `VITA_ABI_FLAGS` (the closest
precedent - 32-bit ARM, same MSVC-ABI concerns), not `linux_build.py`'s
(x86-specific: `-malign-double`, `-freg-struct-return` don't exist on
ARM and aren't needed - AArch64's natural alignment already matches
what those flags force on x86). `-DHALO_RELOCATABLE_TAG_CACHE=1`
carried over from Vita too - same reasoning applies identically here.

Every fix below is in `halo_linux_prefix.h`, gated behind `HALO_SWITCH`
unless noted, because **every other platform here uses clang, which is
lenient about several things GCC (devkitA64; the only GCC target in
this whole project) treats as hard errors.** The pattern repeats
enough to name: clang has broad MSVC-compatibility built in across
every target it supports; GCC's `-fms-extensions` covers much less of
it, and only natively on x86 for the calling-convention/type-keyword
parts.

- **`static` + `#pragma weak`**: GCC hard-errors "weak declaration of X
  must be public" when a `#pragma weak` target already has internal
  (`static`) linkage - unconditionally, regardless of pragma order
  (verified with isolated repros). The generated `halo_msvc_semantics.h`
  (`tools/linux_msvc_semantics.py`)'s whole "COMDAT inline functions"
  section is exactly this situation, for every such name. Clang only
  warns. Fix, found empirically: `__attribute__((weak))` **directly on
  the declaration** is only a warning under GCC too (`-Wattributes`,
  silenced by `-w`) - the bare pragma specifically is what's rejected,
  not the underlying weak-ness. So for `HALO_SWITCH`: skip the
  generated pragma section (`build/switch/halo_msvc_semantics_switch.h`
  - pragma lines stripped; not yet wired into the real build, done by
  hand for this test batch) and put `__attribute__((__weak__))`
  **and drop the forced `static`** directly into the `__inline`/
  `_inline`/`__forceinline` macros instead. Dropping `static` entirely
  (rather than keeping it alongside the attribute) matters: plenty of
  the source already writes `static __inline` itself (genuinely both,
  in MSVC terms), which would double up into "static static" - a
  *different* GCC hard error, covered next.
- **`static` + `static` ("static static")**: `#define __inline
  static __inline__` (every platform but Switch) doubles up wherever
  source already writes `static __inline`/`static __forceinline`
  itself (18 files in this sample alone - likely far more in the full
  tree). Clang only warns ("Clang only warns about the resulting
  `static static`" - the header's own pre-existing comment,
  written for exactly this). GCC hard-errors. Fixed by the same
  `static`-dropping change above: `__weak__` alone already gives every
  TU's copy pick-any (COMDAT-equivalent) linkage, whether or not the
  source's own `static` is present too - and `static` + `__attribute__
  ((weak))` together compiles fine (GCC-only warning, silenced).
- **Inverse case**: a handful of functions are forward-declared
  *without* `static` and defined *with* it (e.g.
  `ai_debug_drawstack_setup`) - genuinely inconsistent in the source
  itself (clang tolerates it; GCC doesn't: "static declaration follows
  non-static declaration"). Patched the one found in this sample
  directly (added the missing `static` to the declaration, matching
  the definition's "private code" section intent) rather than touching
  the macro again - this direction isn't fixable generically the way
  the other two were, but is rare.
- **`weak` the bare word**: `halo_linux_prefix.h`'s own
  `DECLSPEC_SELECTANY` used bare `__attribute__((weak))`, which musl's
  `#define weak __attribute__((__weak__))` (`src/include/features.h`)
  then expands *again* inside itself once its guest libc is in the
  include path. Fixed by using the reserved `__weak__`/`__always_inline__`
  spellings throughout (not `HALO_SWITCH`-gated - strictly safer for
  every platform, since musl isn't in their include path and the
  double-underscore spelling means exactly the same thing either way).
- **`__stdcall`/`__cdecl`/`__fastcall`, `__int64`/`__int32`/`__int16`/
  `__int8`, `__declspec(align(N)|selectany|naked)`**: all real,
  MSVC-compatibility keywords clang recognizes on every target
  (no-op on non-x86 for the calling-convention ones - there's only one
  calling convention on AArch64 anyway); GCC doesn't know any of them
  outside x86, not even with `-fms-extensions`. Defined directly
  (`__declspec` via the standard `##`-pastes-onto-the-first-token-only
  trick, since the preprocessor can't otherwise distinguish
  `align(4)` from `selectany` as one macro parameter).
- **`restrict`**: C99 keyword, not reserved under this project's
  `-std=gnu89` (deliberate, project-wide, unrelated to Switch) - so
  musl's own headers (written assuming a C99+ compiler) using bare
  `restrict` as a type qualifier collide with it being just an ordinary
  (and in a couple of spots, reused-with-different-types) identifier.
  `-Drestrict=__restrict__` on the compile command - GCC's own
  `__restrict__` is a keyword unconditionally, regardless of `-std`.
- **`clockid_t`/`locale_t` unknown**: musl's *public* headers only
  `#define __NEED_clockid_t` etc. when `_GNU_SOURCE`/`_POSIX_C_SOURCE`/
  etc. is defined - none of which this project defines, on purpose
  (`-D__STRICT_ANSI__` exists specifically to hide POSIX names like
  `random()`/`strnlen()` from colliding with the game's own, across
  every platform, not just Switch). But musl's own *internal* headers
  (`src/include/time.h`, `src/include/pthread.h`) need these types for
  their own `hidden`-linkage declarations, which are never visible to
  game code anyway (name-mangled, can't collide with anything). Can't
  just define `_GNU_SOURCE` for every game file - that would undo the
  exact protection `__STRICT_ANSI__` provides. Fixed with a narrow,
  reproducible patch in `build_musl.sh` (not a one-off hand-edit to the
  gitignored, re-fetched vendored source): wrap only these two
  internal headers' own single `#include` line in a
  `#define _GNU_SOURCE 1` / `#undef _GNU_SOURCE` pair, scoped so it
  never reaches any later `#include` in the same translation unit.
- **MSVC integer suffixes** (`0xff00000000ui64`, `byte_swapping.c`):
  GCC doesn't support the suffix spelling under any flag. Patched the
  one file directly (`ULL`, same type and value, zero behavior change
  - this is purely a spelling choice, not a decompilation-fidelity
  concern).

**Remaining, not yet fixed**: 2 files (`source/memory/lra_cache.c`,
`lruv_cache.c`) hit a real but narrow pre-existing issue: the game's
own `#define memcmp csmemcmp` family (cseries.h) redirects musl's
`string.h` declarations too once both are in scope, and musl's
`size_t`-based signature doesn't literally match `csmemcmp`'s own
`unsigned long`-based one - same width under ILP32 (both 4 bytes), but
GCC treats `long` and `int` as distinct types for strict prototype
matching regardless. Only 2/85 files in this sample hit the specific
include order that surfaces it. Not yet root-caused further.

Not yet done: wiring any of this into the real `configure.py`/ninja
build (`tools/switch_build.py` doesn't exist yet - this was all run
by hand, mirroring `tools/vita_build.py`'s flags, to get a fast,
cheap signal before investing in that); testing against the *whole*
`source/` tree (this was a representative sample, not everything);
`halo_msvc_semantics_switch.h` is a manually-stripped copy, not yet a
real generator mode (`tools/linux_msvc_semantics.py --help` would need
a new flag, or a small switch-specific post-process step).

### Not splitting into its own repo (yet)

`port/switch/guest/libc` and the loader mechanism (`switch_guest_abi.h`,
the ELF-load/segment-placement code in `host_main.c`) are genuinely
Halo-agnostic - nothing Halo-specific has leaked into either. Matches
this account's `libdol-nx`/`libgc-nx`/`libwii-nx` pattern in spirit,
but deliberately not split out yet: those were split *after* they had
multiple real game consumers, not pre-emptively, and this design is
still actively churning (mallocng→oldmalloc and three real bugs, this
session alone). Revisit once a second project actually needs it.

## Unexplored

- Whether Switch homebrew has an `mprotect`-equivalent for the desktop
  ports' dirty-page texture tracking (`memory_watch.c`). No user-space
  signal handling on Switch (per libdol-nx). Worst case: drop incremental
  tracking, always re-upload.
- GLES3 vs whatever the D3D8 shim assumes from sceGxm.
- Exact scope of what from `port/vita/platform` (sceGxm-specific) vs
  `port/linux/src` (more portable) the Switch platform layer should
  start from.

## If the guest/host split hits a wall after all

Fall back to the pointer/long-width audit on this source instead: repack
`tag_block`/`tag_data`/`tag_reference` to fixed-width fields, fix
`tag_relocate.c` to store a relative offset instead of a rebased
absolute address, fix the accessors, then the ~133 direct-access call
sites a type-aware audit found (`tools/audit_tag_pointers.py`,
`tools/tag_pointer_audit.json`) — plus an unscoped project-wide `long`
audit, likely larger than those 133. Shouldn't be needed now.
