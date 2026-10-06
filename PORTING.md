# Porting notes

How the Switch port works, how to build and debug it, and the traps that
cost time. The development diary this replaced (milestones 1–13) is in git
history: `git show e2a09e4:PORTING.md`.

## Shape

- **Guest**: the game (`source/`) plus the reusable platform layer
  (`port/linux/src`, `port/linux/game`, `port/switch/platform`), built
  `-mabi=ilp32` (32-bit pointers, as the game's data structures assume) into
  a flat ELF32, `guest.elf`, with musl 1.2.5 as its libc
  (`port/switch/guest/libc`, built by `build_musl.sh`).
- **Host**: a normal 64-bit libnx NRO (`port/switch/host`). It maps the
  guest's code (`svcCreateCodeMemory`, needs Atmosphère) and data
  (`svcMapMemory`) at fixed low addresses, fills the guest's import table,
  and calls its entry. One process, one address space: guest pointers are
  host pointers, zero-extended.
- **Address window** (`guest.ld`, reserved by `virtmemAddReservation`
  before anything else is mapped): code at `0x40000000`; the contiguous
  memory arena (game state, tag/texture/sound caches) pinned at
  `0x41000000`, so saves keep loading across builds; the heap after it, up
  to `0x4c000000`.
- **Imports**: `port/switch/host_imports.list` → `tools/android_imports.py`
  → `build/switch/gen/guest_imports.s` (guest trampolines), resolved by name
  in `host_main.c`'s table. GL goes through generated wrappers:
  `tools/android_gl_stubs.py` (guest side, `guest_gl.c`) and
  `tools/switch_gl_resolve.py` (host side, `host_gl_resolve.c`), with
  `hostgl_*` overrides in `host_shader_stats.c`.

## Build

The README has the full commands. In short:

```sh
python3 configure.py && ninja switch_guest   # game + platform objects
# guest runtime objects in port/switch/guest/, by hand, when changed:
CF="-mabi=ilp32 -O2 -nostdinc -ffreestanding -std=gnu11 -Iobj/include \
    -Ilibc/arch/aarch64_ilp32 -Ithird_party/musl-1.2.5/include"
#   guest_syscall.c guest_platform_stubs.c guest_pthread_stubs.c: $CF
#   guest_main.c guest_runtime_init.c guest_stdio_shim.c guest_tp.c:
#   -mabi=ilp32 -nostdinc -fno-builtin -ffreestanding -fno-stack-protector
#   -Iobj/include -Ilibc/arch/aarch64_ilp32 -Ilibc/src_include
#   -Ithird_party/musl-1.2.5/src/internal -Ithird_party/musl-1.2.5/src/include
#   -Ithird_party/musl-1.2.5/include -D__linux__=1 -D__unix__=1
# then the hand link (README), and:
make -C port/switch/host -j2                 # host.nro, on nxvk
```

- The link takes every `.o` under `build/switch/obj`: delete objects of
  files dropped from the build (e.g. `bink_null.o`), or they collide.
- Re-run `configure.py` after changing `tools/switch_build.py` or
  `host_imports.list`; a failing `configure.py` leaves the old
  `build.ninja` silently in place.
- `__inline` is `static __inline__` in `halo_linux_prefix.h`: write
  `__inline`, not `static __inline` (a hard GCC error).
- Commits are `nx-mod <31334932+nx-mod@users.noreply.github.com>`, with no
  trailers. `main` tracks upstream; work goes on `switch`.

## Deploy and debug

- Everything lives in `sdmc:/haloce-nx/`: `host.nro`, `guest.elf`, game
  data, `config.toml`, saves, shader cache, `debug.txt` (the game's log) and
  `host.log` (the host's; the previous run's is `host.prev.log`).
- Launch as an application (Sphaira forwarder, or title takeover).
- `config.toml`'s `debug.environment` sets `HALO_*` switches:
  `HALO_FRAME_TIMING=300` (tick / render / present per frame) and
  `HALO_RENDER_PROFILE=1` (render phases) find where time goes;
  `HALO_TICK_THREAD=1` runs the tick on core 2 (experimental).
- Crashes: Atmosphère's report in `atmosphere/crash_reports/` gives PCs as
  `host + 0x…` and raw guest addresses; `aarch64-none-elf-addr2line -f -e`
  on `port/switch/host/host.elf` or the deployed `guest.elf` names them.
- A failed `match_assert` names only itself; `csmemcpy` logs its caller
  when handed NULL.

## Renderer rules

`port/linux/src/d3d8_gl.c` translates the game's Direct3D 8 to OpenGL ES.

- **The memory watch.** The GPU keeps copies of game memory (vertex data,
  textures). Linux catches every write with page faults; Switch hashes pages
  instead (`switch_memory_watch.c`), **at most once a frame**. Anything
  written more than once between draws must say so with
  `memory_watch_forget`: decal batches do, and `Lock`/`LockRect`/`LockBox`
  do for the game. A write it misses draws stale for a frame or two.
- **Uploads never wait.** `host_gl_buffer_write` maps unsynchronized: the
  stream buffers rotate per frame behind fences, and each upload gets fresh
  room. `glBufferSubData` there stalled on every small draw (20 → 60 fps).
- **Queries** come from a ring of free objects; beginning a query object the
  GPU still owes a result for waits for the GPU.
- Frames: the guest is always unthrottled; the host paces presents
  (`display.vsync`, `display.frame_rate`). Interpolation draws between the
  30 Hz ticks; without it the game caps itself at 30. The first frame after
  a map load is not shown (it came out in wrong colours).
- The present is a post pass (FXAA, sharpening) or a blit.

## Shaders

The host links [nxvk](https://github.com/nx-mod/nxvk) (Zink over NVK, Mesa
26). Mesa's disk cache keeps compiled shaders in
`sdmc:/haloce-nx/mesa_shader_cache/`. `host_shader_stats.c` counts a
program whose compile and link took over 4 ms as compiled fresh (the
overlay's last number) and records every program to
`shader_programs.bin`. Next: compile that list on core 2 at boot, and ship
a cache filled by a full playthrough. `make MESA20=1` builds the old Mesa
20.1 host, which can't cache (no program binaries, no shared programs).

NVK on GM20B needs `NVK_I_WANT_A_BROKEN_VULKAN_DRIVER=1`; Mesa's utility
code needs a few POSIX functions newlib lacks (`host_nvk_shims.c`).

## Movies

Bink can't be decoded here, so the movies are `.mjx` transcodes
(`tools/mjx_pack.py`): baseline JPEG pictures, PCM audio. `bink_mjx.c`
answers the game's Bink calls; the host decodes each picture
(`host_mjx.c`, libjpeg is 64-bit only). See `docs/mjx_movies.md`.

## Traps

- **`long double`** is quad precision in software on ILP32, with no ILP32
  libgcc: musl's `strtod`, `printf("%Lf")` and friends hit the
  `guest_softfloat_stubs.c` traps. The guest has its own `strtod`.
- **Anonymous `mmap` must be zero**: musl's `calloc` trusts it. The guest's
  `mmap` clears recycled memory below the heap's high-water mark.
- **Sleeps** are `svcSleepThread` (`host_sleep_ns`); a shared event wait
  corrupted libnx's waiter list across threads.
- **No `__thread`** in the guest (it has no TLS segment); per-thread data is
  the last 64 bytes of each thread's 512-byte TLS block
  (`switch_guest_thread.h`).
- **Physical ↔ virtual** addresses are an offset from the arena, not
  OR/AND-NOT masks (`platform.h`); the masks garbled every resource.
- The guest window must be reserved before any thread exists: libnx puts
  stacks at random addresses.
- `-mcpu=cortex-a57` for the guest; the host is tuned the same.
