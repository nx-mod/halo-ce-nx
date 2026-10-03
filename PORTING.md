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
