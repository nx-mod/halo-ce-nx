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

**Was going to say 2 files remained** (`lra_cache.c`/`lruv_cache.c`,
the `csmemcmp` family vs. musl's `string.h`) - turned out to be the
*same* wrong-include-path bug milestone 4 below found, not a separate
issue. Fixed along with everything else there.

### Milestone 4 - done: wired into the real build, whole game compiles

`tools/switch_build.py` now exists for real (`generate_switch_build`,
wired into `tools/project_x86.py` alongside linux/android/vita/windows)
- `ninja switch_guest` compiles the game through the normal
`configure.py` pipeline, not a hand-run script. **All 478 real game
object files compile clean** - every object `config/config.json`
doesn't mark `MISSING` (212 of 833 aren't decompiled yet; excluded the
same way every other platform's build already excludes them), the
entire tracked game, not a sample.

One more real bug worth knowing, found going from the 85-file sample
to the whole tree: **game code was seeing musl's *internal* headers
(`src/include`, `src/internal`), not just the public ones** -
copied into the game-compile flags from `build_musl.sh`'s own
(legitimately different) needs, without reconsidering whether game
code should ever see them. It shouldn't: a properly installed musl
(`make install-headers`) never exposes `src/include`/`src/internal` to
client code at all. This one wrong include path caused two separate
failure classes that looked unrelated: `src/include/features.h`'s bare
`#define weak`/`hidden` macros (meant only for musl's own
implementation) silently mis-expanding the same two words wherever
game code happened to use them as ordinary identifiers or attribute
names (`vita_host_time_us(void) __attribute__((weak))` in
`sound_manager.c`, for one), and `csmemcmp`/`csmemset`/`csmemcpy`'s
"conflicting types" errors in `lra_cache.c`/`lruv_cache.c` (milestone
3's "remaining, not yet fixed" item - turned out to BE this, not a
separate thing: resolved itself once game code stopped reaching
`src/include/time.h`'s declarations via `<sched.h>`'s chain). Fixed by
giving game code only `-I musl/include` (public) - the `src/include`/
`src/internal`/`-Drestrict=`/`-D_GNU_SOURCE`-scoping fixes stay in
`build_musl.sh`, for musl's own build only, unaffected.

Also fixed along the way, same "clang recognizes it everywhere, GCC
only on x86 or not at all" pattern as milestone 3: two more
`__declspec` kinds (`noreturn`, `dllexport`/`dllimport` - the latter
meaningless on ELF anyway, there's only one kind of symbol visibility
here), found in vendored `source/bitmaps/libtiff` (real third-party
code bundled in `source/`, not the game's own decompiled C, same
treatment either way).

Not yet done at the time: actually linking the 478 objects together (see
Milestone 5 below); the `-fmax-type-align=1`-style alignment safety
`tools/vita_build.py` carries (a real, hardware-crash-proven concern on
Vita - misaligned float reads faulting a NEON load) has no GCC/AArch64
equivalent applied yet and hasn't been investigated for whether AArch64
needs the same mitigation - watch for it once there's real gameplay to
test, the way Vita's own crash was only found that way, not statically.

### Milestone 5 - done: the game links cleanly down to the platform-layer boundary

Linked all 478 compiled objects plus the guest runtime (`guest_imports.o`,
`guest_syscall.o`, `guest_tp.o`, `guest_softfloat_stubs.o`,
`guest_stdio_shim.o`, `guest_runtime_init.o`) and `libc.a` against
`guest.ld`. Result: zero "multiple definition" errors, zero mystery
compiler-bug surprises - every remaining `undefined reference` (540
unique symbols) is exactly the expected platform-layer boundary (D3D8,
DirectSound, XInput, XNet, Bink, Win32 API, `halo_linux_*`/`halo_sin`-
style platform shims Linux's own `platform.c` provides) plus a handful
of musl pieces `build_musl.sh` deliberately excludes (pthread, soft-float
`__*tf*`/`__*tf2` quad helpers, `__fdopen`/`freopen`, `__secs_to_zone`,
`__syscall_cp`). None of that is a bug - it's `port/switch/platform`'s
job, the next milestone.

Getting here needed one real fix, not just a quoting workaround for the
earlier manual test script (`@file` response files need each path
quoted when any contain spaces, e.g. `source/saved games/...` -
`tools/switch_build.py`'s real ninja rules already handle this
correctly via normal ninja syntax; only my hand-rolled link test had
the bug).

The real fix: seven functions (`dot_product4d`, `limit2d`,
`set_random_seed`, `real_local_random`, `plane2d_from_points`,
`nonuniform_cubic_spline`, `nonuniform_cubic_spline_vector3d`) were
each defined twice with *strong* linkage - once by
`port/linux/game/msvc_comdat.c` (which exists specifically to give
every header `__inline` function one real external definition, for
units that only see a plain prototype) and once more by one specific
game file that also provides its own genuine, non-inline definition of
the same name, guarding against the generic header version via the
`#define NAME NAME_inline ... #include ... #undef NAME_inline` rename
trick (grep any of the six `.c` files above for that pattern). On every
other platform this doesn't clash: the generated `halo_msvc_semantics.h`
marks every such name `#pragma weak`, so `msvc_comdat.o`'s copy loses to
the file's own strong definition at link time - pick-any, like MSVC's
COMDAT.

`tools/switch_build.py` strips every `#pragma weak NAME` line for
Switch (`switch_strip_weak_pragmas`), because GCC hard-errors
("weak declaration of NAME must be public") when NAME already has
*static* linkage in the same TU - which is true for ~470 of our ~480
TUs, since `halo_linux_prefix.h`'s plain `__inline` macro
(`static __inline__`) is what keeps every other TU's copy
link-invisible in the first place. But that blanket strip also hit
`msvc_comdat.c` itself, where it's not just legal but necessary:
`msvc_comdat.c` redefines `__inline`/`__forceinline` to plain,
non-static linkage before including its four headers (`cseries.h`,
`math/real_math.h`, `bitmaps_inlines.h`, `collisions.h`), so every name
in there is external in that one TU - stripping its own weak pragma
turned its copy of these seven names strong too, clashing with the
file that was always supposed to win.

Confirmed via the project's usual "isolated repro" method
(`aarch64-none-elf-gcc -std=gnu89`, plain `#pragma weak foo` before a
non-static `__inline__ foo(void){...}` body → `nm` shows `W`, correctly
weak - the pragma mechanism itself is fine under gnu89, unlike the
`__attribute__((weak))` dead end from milestone 3) and by diffing the
real ninja command (`ninja -t commands`) against a hand-reconstructed
one: recompiling the literal preprocessed text of `msvc_comdat.c` gave
`W`, but the real build gave `T` - the only difference was which
semantics header `switch_cc` passed via `-include`.

Fix: `generate_switch_build` now special-cases `msvc_comdat.c` to
`-include` the *unstripped* `build/linux/halo_msvc_semantics.h`
(`comdat_cflags`/`comdat_implicit` in `tools/switch_build.py`) instead
of the grep-stripped `halo_msvc_semantics_switch.h` every other TU
gets. Nothing about the macro in `halo_linux_prefix.h` needed to
change. One sharp edge: editing `tools/switch_build.py` alone doesn't
change anything - `build.ninja` is generated output, checked into
nothing; re-run `python3 configure.py` after any change there, the same
as after touching `config/config.json`.

### Milestone 6 - done: reused port/linux/src shrinks the link to just the real platform layer

Milestone 5 left 540 unique undefined references - everything the real
platform layer needs to provide. A chunk of those aren't actually
platform-specific: `port/linux/src` already has pure-compute files with
no file/thread/GPU/audio dependency, reusable by the guest exactly as
written. Added to `tools/switch_build.py` (`SWITCH_PLATFORM_FILES`,
compiled gnu11/`-DHALO_LINUX_PLATFORM_LAYER`, same recipe as
`tools/linux_build.py`'s `PLATFORM_FLAGS` - these aren't game code, so
none of the gnu89/MSVC-inline machinery applies):

- `halo_linker_common.c` - the ~80 "tentative COMMON" globals
  (`ai_globals`, every `debug_*`/`collision_debug_*` flag, ...) every
  platform needs as a placeholder until their owning unit is
  reconstructed. Weak data, no linkage concerns at all.
- `msvc_crt.c`, `msvc_wide.c` - the MSVC CRT/wide-char shims
  (`_stricmp`, `msvc_wcs*`, `halo_linux_fopen`, ...). Needed two real
  fixes, not just flags: GCC has no `__builtin_arm_{r,w}sr64` (clang-only
  AArch64 intrinsic, same category as `__builtin_arm_yield` in musl's
  arch files) - replaced with portable `mrs`/`msr` inline asm, which
  works under both compilers; and `_control87`/`_statusfp`/`_clearfp`
  were guarded by `#ifdef HALO_ANDROID` for the portable ARM/C99-fenv
  implementation (vs. the `#else` branch's glibc-specific, x87-shaped
  `fenv_t.__control_word`) - added `HALO_SWITCH` to that condition,
  since it's the same musl guest libc either way. Also found (and fixed
  the same way) that `_ReadWriteBarrier` only existed under a third,
  separate `#elif defined(__arm__)` branch - missing from the AArch64
  one entirely, on every platform including Android; added it there too
  (it's architecture-independent, just an asm memory clobber).
- `bink_null.c` - Bink video's null backend (no real decoder on any
  native port; `BinkOpen` reports "movie can't open" and the game skips
  it, same as a missing file). Resolves every `Bink*`/`RADSetMemory`
  symbol in one shot.
- `port/third_party/musl-math` (`musl_math_sources()`, imported from
  `tools/linux_build.py`) - `halo_sin`/`halo_cos`/... (`port/include/
  halo_math.h`): every native port renames musl's math functions and
  builds them from source rather than linking the host's, so system-link
  games can't desync on the last bit of a libm that differs per platform.
  Already fully portable musl C - no changes needed.

Plus two new guest-runtime files (manually built like the existing
`guest_*.o` set - see the "not a real ninja rule yet" note above),
covering what's left that's self-contained (no new host import needed):

- `guest_platform_stubs.c`: `platform_log`/`platform_show_message`
  (format into a buffer, send through the existing `host_log` import),
  `platform_translate_path` (identity passthrough - no real path scheme
  needed until there's real file I/O to target), `config_boolean`/
  `config_real`/`config_string` (`port_config.c`'s cvar system, not
  ported - every setting reports its zero/off default), `vita_host_time_us`
  (returns 0 - no host-synced clock import yet), `test_input_hold_action`
  (`xinput_sdl.c`'s debug hook, unused - no-op).
- `guest_pthread_stubs.c`: the guest is single-threaded by construction
  (`guest_tp.c`'s one static TLS block), so mutex/condvar ops are
  genuine no-ops (one thread can't contend with itself) and
  `pthread_create` fails outright (`EAGAIN`) rather than faking
  concurrency by running the start routine synchronously - a caller that
  checks the return value loses a background task cleanly instead of
  running reentrant code in the wrong place.
- `guest_softfloat_stubs.c` gained the other half of the quad-float
  trap set (`__divtf3`, `__eqtf2`/`__getf2`/`__letf2`,
  `__extendsftf2`, `__trunctfdf2`/`__trunctfsf2`) - same
  "trap if this ever actually fires" reasoning as the original set.

Net: 540 -> 286 unique undefined references, still zero "multiple
definition" errors. What's left is now a clean boundary - exactly
D3D8, DirectSound, XInput/`XInitDevices`, XNet/`halo_ws_*`, the Win32
file/handle/time API (`CreateFileA`, `ReadFile`, `QueryPerformanceCounter`,
...), the save-game/signature API, and `posix_stat`/`posix_fstat`/
`posix_make_directory`/`posix_truncate` (`port/linux/src/posix.h`'s own
comment: Android compiles these straight into its 64-bit host and calls
them from the ILP32 guest - the same shape Switch needs, with new
host_* imports, once file I/O is real). Plus a handful of musl functions
`build_musl.sh` still excludes for real reasons (`fdopen`/`freopen`/
`__fdopen` need real `ioctl`/syscall-macro support that doesn't
preprocess cleanly yet; `__secs_to_zone`/`__tm_to_tzname` are timezone
database code; `__syscall_cp` is thread-cancellation plumbing with no
second thread to cancel). None of this is a surprise - it's
`port/switch/platform`'s actual job list for the next milestone.

### Not splitting into its own repo (yet)

`port/switch/guest/libc` and the loader mechanism (`switch_guest_abi.h`,
the ELF-load/segment-placement code in `host_main.c`) are genuinely
Halo-agnostic - nothing Halo-specific has leaked into either. Matches
this account's `libdol-nx`/`libgc-nx`/`libwii-nx` pattern in spirit,
but deliberately not split out yet: those were split *after* they had
multiple real game consumers, not pre-emptively, and this design is
still actively churning (mallocng→oldmalloc and three real bugs, this
session alone). Revisit once a second project actually needs it.

Same call for the guest/host import bridge added in the "unstub video"
milestone below (`tools/android_gl_stubs.py`, `tools/android_imports.py`,
`port/switch/host_imports.list`) - also Halo-agnostic in practice (it's
Android's halo-ce-universal generators, unmodified), also staying put
until a second real consumer wants it, not pre-emptively.

## Platform layer plan (next milestone)

The link in milestone 5 gives the exact symbol boundary `port/switch/platform`
needs to fill: D3D8 (`D3DDevice_*`/`D3D*`), DirectSound/`IDirectSound*`,
XInput/`XDEVICE_TYPE_*`, XNet, Bink video, a few Win32 calls, and the
`halo_linux_*`/`halo_sin`-style shims Linux's own `platform.c` provides.
Decided, based on this account's `~/switch/dawn` and `~/switch/wiicompiled`
projects (both proven on real hardware - see PORTING.md history if this
section is ever stale, or ask):

- **Rendering**: GLES3 via devkitPro's portlibs (`libnvk_gl`/`libEGL`/
  `libGLESv2`), adapting `port/vita/platform/d3d8_gxm.c`'s sibling
  `d3d8_gl.c` approach - not raw Vulkan/Dawn directly. `~/switch/dawn`
  proved the underlying NVK Vulkan driver works on real hardware
  (`vkCreateViSurfaceNN`/swapchain/present, 180/180 frames, visually
  confirmed), but Dawn's own WebGPU surface abstraction has no Horizon
  VI-surface type yet (the smoke test bypassed Dawn for raw Vulkan
  specifically because of that gap), and translating D3D8's GL-shaped
  state machine straight to Vulkan's explicit pipeline/descriptor model
  would mean rewriting `d3d8_gl.c` rather than adapting it. GLES3-over-
  NVK gets the same validated driver at the right abstraction level.
- **Audio/input**: libnx's native `audout`/`hid` directly, not SDL2/SDL3.
  `~/switch/wiicompiled`'s own notes: "SDL3 has no Switch backend...
  audio→audout, input→hid" - an established, working choice in this
  account's ecosystem, not a new experiment. (Supersedes an earlier SDL2
  decision made before this precedent was found - no SDL dependency at
  all for Switch.)
- **First real milestone**: a "headless boot" - stub every `D3DDevice_*`/
  rendering call, get the game ticking (wall-clock-driven, matching
  `wiicompiled`'s VI-retrace-driven frame loop), defer real GPU work.
  Same reasoning as `wiicompiled`'s own current milestone: proves game
  logic and the platform-layer *shape* before spending time on a GL
  backend.

### Milestone 7 - done: a "headless boot" null platform layer, generated from real XDK signatures

Wrote `port/switch/platform/` - the Switch-only null backend for
everything D3D8/DirectSound/XInput/XNet/Win32/save-game-API that the
"real platform layer" bucket from Milestone 6 needed: safe no-ops or
failure returns instead of real rendering/audio/input/networking/file
I/O, so the link resolves without yet building any of those real
backends. Six new files (`switch_d3d8_null.c`, `switch_dsound_null.c`,
`switch_xinput_null.c`, `switch_xnet_null.c`, `switch_win32_null.c`,
`switch_posix_null.c`), wired into `tools/switch_build.py` the same way
as `SWITCH_PLATFORM_FILES` (gnu11, `-DHALO_LINUX_PLATFORM_LAYER`).

Every signature is real, not guessed: a one-time script
(`/tmp/gen_stubs.py`, not checked in - this was exploratory, not meant
to be re-run) scraped the exact declaration for ~230 of the ~280
remaining undefined symbols straight out of `port/include/xdk/xdk_pdb.h`
(and `xdk_xbdm.h` for the three `Dm*` debug-monitor calls), generating
`ret name(params) { body }` mechanically. The handful outside pdb.h
(`D3DXGetErrorStringA`, `d3d_find_flipcount`, `halo_d3d_*`,
`halo_screen_*`, `halo_settings_generation`, `halo_linux_mouse_look`,
`halo_render_draw_counts`) were found by grepping their real call sites
in `source/` for an inline `extern` declaration instead.

Default body is `return 0;` (or empty, for `void`) - safe for most of
this (`D3DDevice_SetRenderState_*` et al are genuine no-ops; `XInitDevices`
reporting zero devices is the real, already-handled "no controller"
path). Fixed to the *real* failure sentinel where 0 is actively wrong,
not just a vague placeholder:
- Win32: `INVALID_HANDLE_VALUE` (`CreateFileA`/`FindFirstFileA`),
  `INVALID_FILE_ATTRIBUTES`/`INVALID_SET_FILE_POINTER`/`INVALID_FILE_SIZE`
  (all `0xFFFFFFFF`, not `0`), `WAIT_FAILED` for
  `WaitForSingleObject(Ex)` - a caller checking `== INVALID_HANDLE_VALUE`
  while this returned `NULL` would never notice the failure.
- `halo_ws_*`/`__WSAFDIsSet`: BSD/Winsock's `SOCKET_ERROR`/
  `INVALID_SOCKET` is `-1`, not `0` - `0` from `halo_ws_socket` reads as
  "here is valid file descriptor zero," not "failed."
- `XNetStartup`/`XNetCleanup`/`XNetRegisterKey`/... still return success
  (`0`) deliberately - "the network layer initialized" is a different,
  coarser claim than "a socket call succeeded," and failing it outright
  felt like a worse default than letting individual connections fail
  later (which they will, since every `halo_ws_*` call does).

`D3D__RenderState`/`D3D__TextureState`/`D3D__IndexData` (plain `extern`
arrays the game's own `xdk_d3d8.h` inline functions read/write directly
- no function call involved) and the three `XDEVICE_TYPE_*_TABLE`
device descriptors are zero-initialized data definitions, not stubs.

**Explicitly NOT reviewed for runtime correctness** - every `Create*`/
`Lock*`-style D3D8 function still returns `0` (claims success) with its
output pointer untouched, which the caller will then dereference. That
is fine for *linking* (today's actual goal, deliberately stopping short
of hardware testing - see the top of this doc) but will need a real
pass - most likely real failure returns so the game's own error paths
trigger - before anyone actually runs this on a console. Said plainly in
`switch_d3d8_null.c`'s own header comment too, so it's not lost.

### Milestone 8 - done: the entire game links, zero errors - a complete guest ELF

Linked all 516 objects (478 game + `SWITCH_PLATFORM_FILES` +
`port/switch/platform`'s null backend + `port/third_party/musl-math`)
plus the full guest runtime (`guest_main.o`, `guest_imports.o`,
`guest_syscall.o`, `guest_tp.o`, `guest_softfloat_stubs.o`,
`guest_stdio_shim.o`, `guest_runtime_init.o`, `guest_platform_stubs.o`,
`guest_pthread_stubs.o`, `guest_syscall_cp.o`) and `libc.a` against
`guest.ld`. **Exit code 0. Zero undefined references, zero multiple
definitions.** A real 16.5 MB ELF, entry point resolves to a real
address, and the game's actual `main` (`source/main/main.c`) is linked
in and present (not yet *called* - `guest_main.c`'s `__guest_entry` is
still milestone 2's printf/malloc smoke test, not the real game
bootstrap; wiring that up is the next, now well-scoped step).

Getting the last handful of symbols needed two more real fixes, not
stubs:

- **`__syscall_cp`**: musl's `open`/`close`/`read`/`write` all call this
  unconditionally (the "cancellable syscall" entry a `pthread_cancel` on
  another thread can interrupt), not just when real cancellation is
  possible - `build_musl.sh` never provided it at all. Since this guest
  has no second thread to ever request a cancellation
  (`guest_pthread_stubs.c`), "cancellable" and "ordinary" are the same
  syscall here: `guest_syscall_cp.c` is a one-line pass-through to the
  same `__guest_syscall` dispatcher every other syscall already uses -
  their signatures already matched exactly.
- **`__fdopen`/`fdopen`/`freopen`**: NOT actually missing features -
  real bugs in how this environment preprocesses two vendored musl
  files, found via `-E` and fixed at the source level (reproducible
  patches in `build_musl.sh`, same category as its existing `__inline`
  ones):
  - `include/unistd.h`'s `long syscall(long, ...);` declaration and
    `src/internal/syscall.h`'s `#define syscall(...) __syscall_ret(...)`
    macro collide whenever both are visible in one TU (stdio_impl.h's
    chain activates the macro before `freopen.c`'s own later
    `#include <unistd.h>`): the preprocessor reads the *declaration* as
    a *call*, taking `long` and `...` as its two macro arguments and
    mangling the whole line into garbage (confirmed via `-E`:
    `__syscall1(long,sizeof(1?(...):0ULL) < 8 ? ...)` - the literal
    three dots, not expanded arguments). Fixed with the same whole-file
    push_macro/pop_macro trick already used for `__inline` - scoped to
    `syscall` this time, safe here since the declaration is `unistd.h`'s
    only use of the name.
  - `__fdopen.c`'s one real OS dependency, `<sys/ioctl.h>`'s
    `TIOCGWINSZ` (checked once, to auto-enable line buffering for a
    terminal), doesn't exist in this environment - but it's an optional
    heuristic, not a correctness requirement. Deleted the include and
    the one `if` that used it; every stream just defaults to fully
    buffered (`f->lbf = EOF`, already the function's own fallback)
    instead of line-buffered on a TTY.

  Both fixes are applied twice in the repo: once as a reproducible
  `build_musl.sh` patch (for a from-scratch extraction) and once by
  hand directly against the already-extracted
  `third_party/musl-1.2.5` tree (no network access this session to
  redownload after a from-scratch wipe - the two must be kept in sync
  if either changes again).

Next: wire `guest_main.c`'s entry to call the game's real startup
instead of the smoke test, then actually run this on hardware - at
which point the Milestone 7 "not reviewed for correctness" stubs (file
I/O failing outright, D3D8 Creates claiming success with garbage
pointers) are exactly where to expect the first real crashes, in
roughly that order.

### Milestone 9 - done: "unstub video" - the real D3D8->GLES3 renderer links, guest and host both

Replaced `switch_d3d8_null.c` with the real thing: `port/linux/src/
d3d8_gl.c` (and `d3d8_resources.c`, `gl_functions.c`, `nv2a_vsh.c`,
`nv2a_psh.c`, `xbox_textures.c`, `xgpu_text.c`) already has a complete
GLES3 path - it was written for Android, gated behind `HALO_ANDROID`
throughout (~60 branches across those files). Extending every one of
them to also cover `HALO_SWITCH` (`sed` across the lot, then fixing the
two files the first sweep missed - `xgpu.h`'s `xgpu_capabilities`
struct and `xbox_textures.c`) turned out to be the right amount of
work: this is not a new renderer, it's Android's.

The real new piece is the guest/host bridge the ILP32 guest needs to
reach devkitPro's real (64-bit) GLES3 libraries at all - and Android
already solved this exact problem for its own ILP32-guest-on-a-64-bit-
host split. Reused its generators unmodified:
- `tools/android_gl_stubs.py` reads gl.h's existing Android function
  list against devkitPro's real `GLES3/gl32.h`/`GLES2/gl2ext.h` (same
  Khronos-registry format Android's sysroot headers use) and writes
  `guest_gl.c` (guest-side wrappers calling through `hostgl_<name>`
  imports, widening args across the ABI boundary where needed) plus
  the list of those 98 import names.
- `tools/android_imports.py` turns that list plus `port/switch/
  host_imports.list` (host_log/host_write, the six `host_gl_*` bridge
  functions, and now `platform_video_initialize`/
  `platform_video_drawable_size`/`platform_video_swap`/
  `platform_pump_events`) into the actual assembly trampolines -
  replacing the hand-written 2-entry `guest_imports.S` now that
  there are 106.

New `tools/switch_gl_resolve.py` (not from Android - Android resolves
GL names at runtime via `dlsym` against a dynamically loaded
`libGLESv3.so`; this host statically links devkitPro's real
`libGLESv2.a`/`libEGL.a`, so a plain compile-time name->`&function`
table needs no runtime lookup at all) generates the host's
`hostgl_<name> -> real function` table from the same import list.
`glShaderSource` is the one name needing a hand-written adapter: its
generated guest wrapper widens each string pointer into a fixed array
of 64-bit slots (matching `tools/android_gl_stubs.py`'s own special
case for it), so the host side unwraps that back into a real
`const char *const *` before calling the real `glShaderSource`.

`port/switch/host/source/host_video.c` is the actual new host code:
`platform_video_initialize` runs the exact EGL sequence
`~/switch/nxvk/switch/smoke/gl_egl_tri.c` already proved on this
hardware this session (`eglGetDisplay` -> `eglInitialize` ->
`eglBindAPI` -> `eglChooseConfig` -> `eglCreateWindowSurface` on
`nwindowGetDefault()` -> `eglCreateContext` -> `eglMakeCurrent`), plus
the six `host_gl_*` helpers `xgpu.h`'s Android/Switch branch needs
beyond the raw `hostgl_*` table (`host_gl_get_string`,
`host_gl_has_extension`, `host_gl_read_buffer_word`,
`host_gl_buffer_write`, `host_gl_fence_frame`, `host_gl_wait_frame`).
Guest and host share one process's address space here (unlike
Android's real cross-process split) - every pointer the guest passes
is already a valid address this code can read or write directly, no
marshaling needed.

Real bugs found getting both sides to link, not just stubs:
- **`glGetBufferSubData` doesn't exist in GLES3** (desktop-GL only) -
  `host_gl_read_buffer_word` uses `glMapBufferRange`/`glUnmapBuffer`
  instead.
- **The host NRO's `LIBS` needed `-lstdc++ -lm`** alongside
  `-lEGL -lGLESv2 -lglapi -ldrm_nouveau -lnx` (devkitPro's own
  `es2gears` example has the exact line) - `libEGL.a`'s NVK/Mesa driver
  internals are C++ (`nv50_ir`'s codegen), so even a plain-C project
  needs the C++ runtime for `operator new`/`delete` and friends; and
  Mesa's GLSL constant folder calls libm directly (`powf`, `sinf`, ...).
- **A real, hardware-confirmed bug**, not caught until an actual
  deploy: `switch_guest_abi.h`'s `import_count` field had to become
  "the address of a `uint32_t`" rather than "the count itself" (the
  same reason `import_table`/`import_names` already were addresses -
  a symbol's value is only known at link time, not something a static
  initializer can fold in as a constant). `guest_main.c` and the
  header's comment were fixed for this; `host_main.c`'s reader was
  not, in the same pass - it kept reading the raw address as if it
  were the count directly. Result on real hardware: no crash, just
  `import_count=1076888024` and a `host.log` growing without bound as
  the loop walked billions of "imports" out of whatever bytes
  followed in memory - a hang that looked exactly like a hang, because
  it was one, just not an infinite one. Fixed by dereferencing
  `import_count` through the same vaddr->`data_heap` translation
  `import_table`/`import_names` already got.

Also wired up while deploying, not strictly "video": `port/linux/src/
xiso.c`'s existing `xiso_extract_maps` (used by the desktop ports for
"first start without game data") now runs on the Switch host too, via
a new `HALO_SWITCH_HOST` branch in its own `#include` lines
(`switch_host_posix_shim.h` instead of the full `platform.h`/`posix.h`
- this host doesn't need their XDK/winsock machinery for three plain
POSIX-shaped calls) and a thin `host_xiso.c` wrapper (`#define
HALO_SWITCH_HOST` then `#include` the real, shared `xiso.c` - one
canonical file, not a copy). `host_main.c` extracts
`sdmc:/haloce-nx/halo.xiso` to `sdmc:/haloce-nx/maps` on first run
(a `.extracted` marker file skips re-extracting a multi-GB image on
every launch) and otherwise leaves an already-populated `maps` folder
alone - supports a bare xiso, pre-extracted data, or both sitting
there, same as the desktop ports. Game data lives at `sdmc:/haloce-nx/`
deliberately separate from `sdmc:/switch/halo-ce-nx-guest-poc/`'s own
app binaries/log - different lifecycle (gigabytes, user-supplied,
survives a reinstall) from a homebrew app's usual folder.

**Not yet wired up despite all of this linking and deploying cleanly**:
`guest_main.c`'s `__guest_entry` is *still* the Milestone 2 printf/
malloc smoke test - it never calls `platform_video_initialize` or
anything else in the real renderer, so nothing has actually drawn a
pixel yet. Getting the full link/deploy loop itself exercised (icon,
app title "Halo", the `import_count` hang found and fixed on real
hardware) was this milestone's actual scope. Wiring the entry to the
game's real bootstrap is the next, now fully-scoped step - at which
point the Milestone 7 stubs are where to expect the first real
rendering-side crashes.

## Unexplored

- Whether Switch homebrew has an `mprotect`-equivalent for the desktop
  ports' dirty-page texture tracking (`memory_watch.c`). No user-space
  signal handling on Switch (per libdol-nx). Worst case: drop incremental
  tracking, always re-upload.

## If the guest/host split hits a wall after all

Fall back to the pointer/long-width audit on this source instead: repack
`tag_block`/`tag_data`/`tag_reference` to fixed-width fields, fix
`tag_relocate.c` to store a relative offset instead of a rebased
absolute address, fix the accessors, then the ~133 direct-access call
sites a type-aware audit found (`tools/audit_tag_pointers.py`,
`tools/tag_pointer_audit.json`) — plus an unscoped project-wide `long`
audit, likely larger than those 133. Shouldn't be needed now.
