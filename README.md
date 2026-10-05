# Halo: Combat Evolved for Switch

A native Nintendo Switch port of **Halo: Combat Evolved**, built from the
Xbox decompilation. Not an emulator — the game's own code runs natively
on the Switch's CPU, and its Direct3D rendering is translated to GLES3.

**Status: playable.** On real hardware it boots to the main menu and plays
the campaign with sound and controller input, at a 30 fps cap (about
20–30 in combat). Still rough in places; see [Known issues](#known-issues)
and [PORTING.md](PORTING.md).

**No game data is included.** You need your own Xbox copy of Halo:
Combat Evolved — the PC version's maps don't work.

## Screenshots

![The main menu, running on a Switch](port/switch/media/menu.jpg)

[![A firefight, running on a Switch (click for the full-quality video)](port/switch/media/gameplay.gif)](port/switch/media/gameplay.mp4)

*Captured on a Switch. The numbers along the top of the clip come from a
performance overlay running alongside, not from the game.*

## Installing

1. Copy `host.nro` and `guest.elf` to `sdmc:/switch/halo-ce-nx-guest-poc/`.
2. Put your game data in `sdmc:/haloce-nx/`: either the Xbox disc image
   as `halo.xiso` (extracted to `maps/` on first launch, which takes a
   while) or an already-extracted `maps/` folder. Copy the disc's
   `default.xbe` there too; the loading screen's picture comes from it.
3. Launch it from the homebrew menu.

Saves, the map cache and `debug.txt` (the game's own log) go in
`sdmc:/haloce-nx/`; `host.log` goes beside the NRO.

## Building

Needs devkitPro (devkitA64 and libnx), Python 3 and Ninja. The game runs
as a 32-bit-pointer (ILP32) guest inside a normal 64-bit homebrew host,
so there are two builds:

```sh
# the guest: the game's objects, then a hand link
python3 configure.py
ninja switch_guest
python3 -c "import pathlib; objs=sorted(pathlib.Path('build/switch/obj').rglob('*.o')); open('/tmp/game_objs.txt','w').write(''.join('\"%s\"\n'%o for o in objs))"
aarch64-none-elf-gcc -mabi=ilp32 -nostdlib -ffreestanding -Wl,-T,port/switch/guest/guest.ld \
  @/tmp/game_objs.txt port/switch/guest/guest_main.o port/switch/guest/guest_text_demo.o \
  port/switch/guest/guest_syscall.o port/switch/guest/guest_tp.o \
  port/switch/guest/guest_softfloat_stubs.o port/switch/guest/guest_stdio_shim.o \
  port/switch/guest/guest_runtime_init.o port/switch/guest/guest_platform_stubs.o \
  port/switch/guest/guest_pthread_stubs.o port/switch/guest/guest_syscall_cp.o \
  port/switch/guest/build/musl/libc.a -o guest.elf -Wl,-e,__guest_entry

# the host
make -C port/switch/host -j2
```

`guest.elf` is not produced by Ninja; the link above is the only way to
make it (see PORTING.md). The guest runtime objects in `port/switch/guest/`
and its musl are built separately, as PORTING.md describes.

## Known issues

- **Hitches the first time an effect appears**: each new shader takes
  10–70 ms to compile and link, and this driver can't cache compiled
  programs. A precompiled shader pack is the next piece of work.
- Text can look garbled for a moment while it loads, and some letters
  show a thin box around them.
- Decals (bullet holes, blood) can still appear late or briefly look wrong.
- Shadows look a little off.
- No intro movies (Bink video isn't supported); the game skips them.
- Saves made with an older build may not load in a newer one.

## Credits

- **Bungie** made Halo: Combat Evolved. Halo is a trademark of Microsoft.
- **[punpckhdq/halo](https://github.com/punpckhdq/halo)** /
  **[bnunu/halo-1](https://github.com/bnunu/halo-1)**: the decompilation
  this is built on, of Xbox build 2342 (`cachebeta.exe`).
- **[cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal)**:
  the Linux/Windows/Android port whose platform layer, renderer and
  netcode this tree carries (`port/linux`, `port/windows`, `port/android`).
  The Switch guest's libc (`port/switch/guest/libc`) adapts their
  Android guest's musl port for devkitA64's ILP32 ABI.
- **[musl](https://musl.libc.org)**: the guest's C library.
- **[BirchWoodGod/halo-ce-vita](https://github.com/BirchWoodGod/halo-ce-vita)**:
  the PS Vita port this repo is forked from.
- **[Xita](https://github.com/Xita-Project/xita)**: earlier Vita work
  (GPU translation, threading) that fed into the Vita port.
- **[Invader](https://github.com/SnowyMouse/invader)** by SnowyMouse:
  the tag definitions `tag_layouts.h` is generated from.

## License

GPLv3 ([LICENSE](LICENSE)) — `port/linux/src/tag_layouts.h` is generated
from Invader's GPL-3.0 tag definitions. The decompilation and
halo-ce-universal are CC0 ([LICENSES/CC0-1.0.txt](LICENSES/CC0-1.0.txt)).
Halo's maps, executable and other game content belong to their owners
and are not included.

Not affiliated with or endorsed by Microsoft or Bungie. Contains no game
assets.
