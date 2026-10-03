# Porting to Switch

Base: `BirchWoodGod/halo-ce-vita` (not `halo-ce-universal`, not `Xita`).
History kept, remote detached.

## Resolved: a real low, fixed guest window works

The open question below is answered. `~/switch/nx-mapmem-poc`, run on
real hardware (five iterations, log: `nx-mapmem-poc/results.log`):

- `svcMapMemory` accepted every candidate tried from `0x08000000` up to
  `0x7FF00000` (all < 2 GiB).
- It's a **move**, not a dual-alias (matches switchbrew's docs): the
  source pointer is reprotected to `Perm_None` once mapped to the
  destination. The first two test runs crashed reading through the old
  source pointer afterward — that's correct behavior, not corruption; the
  test was wrong to expect both pointers to stay valid.
- Fixed that, retested destination-only: data survives the move, writes
  persist, clean unmap. Confirmed on 6 different low addresses.
- Control at `0x100000000` (4 GiB) failed cleanly with the kernel's
  "invalid memory region" error, matching libdol-nx's finding above 2 GiB
  exactly, and confirming the test methodology.

**So: an ILP32 build with a real identity-mapped guest window below
2 GiB is viable.** The project-wide `long`/pointer-width audit below is
no longer necessary. `libdol-nx`'s "flat alias unavailable on Switch" was
specifically about dual-aliasing a full 4 GiB region for their flat PPC
memory model — a different, larger ask than what halo-ce-nx needs (a
one-time move of a single guest region, never touched through the old
pointer again).

**Still open, not yet tested:**
- Only a single 4 KiB page was moved. A guest window needs ~128 MB–2 GB.
  Need to confirm a much larger `svcMapMemory` works the same way (may
  need several calls if the kernel caps a single move's size).
- Only tested as *data* (`PROT_READ|WRITE`). Guest **code** also needs to
  end up at a low fixed address and be executable — untested whether a
  moved region can be marked executable, or whether code needs a
  different mechanism (e.g. `svcSetProcessMemoryPermission`).
- Not yet verified that `gcc -mabi=ilp32` on devkitA64's
  `aarch64-none-elf-gcc` actually produces `sizeof(long) == sizeof(void*)
  == 4` (it compiled a trivial file without error, "deprecated" warning
  only — never checked the actual data model it produces).

## Plan

1. Verify `-mabi=ilp32` actually gives 4-byte `long`/pointers (a
   `static_assert`-style test file, check with `objdump`).
2. Extend the PoC: move a large (~128 MB) region, and test marking a
   moved region executable.
3. If both hold: build the game's own source (`source/`, already
   32-bit-pointer-clean — it's the Vita decomp) as a genuine ILP32
   AArch64 ELF with devkitA64's native toolchain — no Mach-O/clang
   Apple-target conversion needed at all, unlike Android's port, since
   `-mabi=ilp32` is a real ELF ABI devkitA64 already supports.
4. Host side (normal 64-bit code: libnx, SDL2, GLES3/EGL via portlibs)
   loads that image, `svcMapMemory`-moves its segments to the fixed base,
   and bridges syscalls/SDL/GL calls across — the same import-table
   design `halo-ce-universal`'s Android port uses
   (`port/android/guest/runtime/guest_host.h`), adapted to this codebase.
   `port/linux/src/tag_relocate.c` (tag-pointer fixup via a runtime bias)
   needs no changes at all under this plan — tags only ever need to be
   real pointers within *this* process, which they now can be.
5. `port/switch/` (modeled on `port/vita/`): devkitA64 + libnx, SDL2
   (portlib, mature on Switch — Vita's own sceGxm/pad calls get replaced
   either way) for input/audio/window, GLES3 (portlibs have
   `GLES3/gl3.h`, `libEGL.a`, `libGLESv2.a`, Mesa/NVK via `libnvk`) for
   whatever `port/vita/platform`'s D3D8 shim did via sceGxm.

## Unexplored

- Whether Switch homebrew has an `mprotect`-equivalent for the desktop
  ports' dirty-page texture tracking (`memory_watch.c`). No user-space
  signal handling on Switch (per libdol-nx). Worst case: drop incremental
  tracking, always re-upload.
- GLES3 vs whatever the D3D8 shim assumes from sceGxm.

## If step 1 or 2 above fails after all

Fall back to the pointer/long-width audit on this source instead
(abandoned above, kept here in case): repack `tag_block`/`tag_data`/
`tag_reference` to fixed-width fields, fix `tag_relocate.c` to store a
relative offset instead of a rebased absolute address (the current
scheme truncates past 4 GiB of bias), fix the accessors, then the ~133
direct-access call sites a type-aware audit found (script and full list:
`tools/audit_tag_pointers.py`, `tools/tag_pointer_audit.json`) — plus an
unscoped project-wide `long` audit, likely larger than those 133.
