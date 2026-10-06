# Halo: Combat Evolved for Switch

A native Nintendo Switch port of **Halo: Combat Evolved**, built from the
Xbox decompilation. Not an emulator — the game's own code runs natively
on the Switch's CPU, and its Direct3D rendering is translated to GLES3.

**Status: playable.** On real hardware it plays the intro movie, boots to
the main menu and plays the campaign with sound and controller input, at
**60 fps** with interpolation for most of a10 (50–57 in the cryo bay's
heaviest views). Rendering goes through Vulkan (Zink over NVK), and compiled
shaders are kept on the SD card between sessions. Still rough in places; see
[Known issues](#known-issues) and [PORTING.md](PORTING.md).

**No game data is included.** You need your own Xbox copy of Halo:
Combat Evolved — the PC version's maps don't work.

## Screenshots

![The main menu, running on a Switch](port/switch/media/menu.jpg)

[![A firefight, running on a Switch (click for the full-quality video)](port/switch/media/gameplay.gif)](port/switch/media/gameplay.mp4)

*Captured on a Switch. The numbers along the top of the clip come from a
performance overlay running alongside, not from the game.*

## Installing

1. Copy `host.nro` and `guest.elf` to `sdmc:/haloce-nx/`, the game's own
   folder.
2. Put your game data there too: either the Xbox disc image
   as `halo.xiso` (extracted to `maps/` on first launch, which takes a
   while) or an already-extracted `maps/` folder. Copy the disc's
   `default.xbe` there too; the loading screen's picture comes from it.
3. Optional, for the intro, attract and credits movies: convert the
   disc's `bink/*.bik` files with `tools/mjx_pack.py` and put the results
   in `sdmc:/haloce-nx/bink/` (see [Movies](#movies)). Without them the
   game skips its movies, as it always could.
4. Launch `sdmc:/haloce-nx/host.nro` with full memory: make a
   [Sphaira](https://github.com/ITotalJustice/sphaira) forwarder for it (a
   home-menu icon), or open it from a game's title takeover (hold R while
   starting a game). Both run it as an application with the console's
   full memory; the homebrew menu opened from the album runs as an applet,
   with much less.

Saves, the map cache, `config.toml`, the shader cache and `debug.txt` (the
game's own log) go in `sdmc:/haloce-nx/`, and `host.log` beside the NRO.
Saves keep loading across updates: the game state sits at a fixed address
(saves made before October 6, 2026 builds don't load in later ones).

## Movies

Halo's movies are Bink, which nothing on the Switch can decode, so they are
converted once on a computer: `tools/mjx_pack.py intro.bik intro.mjx -q 3`
rewrites every frame as a baseline JPEG and the soundtrack as plain PCM
(ffmpeg reads Bink). The game plays `.mjx` files through the same Bink
calls it always made, the host decoding each picture with libjpeg. Five
are used: `intro`, `attract1`–`attract3` (the main menu, left idle) and
`credits`. They're 4:3, as on the Xbox. See
[docs/mjx_movies.md](docs/mjx_movies.md).

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

# the host: OpenGL ES through nxvk (Zink over the NVK Vulkan driver, Mesa 26)
make -C port/switch/host -j2
```

The host links [nxvk](https://github.com/nx-mod/nxvk)'s OpenGL ES, installed as a
devkitPro portlib, and devkitPro's `switch-libexpat` and `switch-libjpeg-turbo`.
Mesa's shader disk cache keeps compiled shaders in
`sdmc:/haloce-nx/mesa_shader_cache/`, so a shader is compiled once, not every
session, and `sdmc:/haloce-nx/shader_programs.bin` records every program the game
has used. `make -C port/switch/host MESA20=1` builds the earlier host on devkitPro's
Mesa 20.1 instead (`host_mesa20.nro`, no shader cache), for comparison.

`guest.elf` is not produced by Ninja; the link above is the only way to
make it (see PORTING.md). The guest runtime objects in `port/switch/guest/`
and its musl are built separately, as PORTING.md describes.

## Settings

The game writes `sdmc:/haloce-nx/config.toml` on its first launch, with
every setting at its default and a comment explaining it. Edit it with any
text editor; changes apply at the next launch. Your edits and comments are
kept, settings a newer version adds are appended, and a mistake is reported
in `debug.txt` with the default used instead. Delete the file to start over.

| setting | default | what it does |
|---|---|---|
| `display.interpolation` | `true` | draws frames between the game's 30 ticks a second, for smooth motion above 30 fps; `false` draws 30 |
| `display.frame_rate` | `60` | the cap: `60`, `30`, or `0` for none |
| `display.vsync` | `true` | waits for the display; `false` presents at once (tearing), sleeping to the cap if there is one |
| `display.fxaa` | `true` | smooths jagged edges (about 1 ms of GPU time) |
| `display.sharpen` | `0.4` | contrast-adaptive sharpening, `0.0` to `1.0` |
| `display.anisotropy` | `4` | texture sharpness at a slant: `1` (original), `2`, `4`, `8`, `16` |
| `display.lens_flares` | `true` | lens flares and the glow around lights |
| `display.lens_flare_test_every` | `2` | test each light's visibility every Nth frame: `1` is the original, higher is cheaper |
| `overlay.enabled` | `true` | the frame rate overlay |
| `overlay.position` | `"top"` | `"top"` or `"bottom"` |
| `overlay.frame_time` | `true` | the slowest frame of the last second, in ms — where a stutter shows |
| `overlay.shaders` | `true` | programs linked / compiled fresh: the second number is first-time stutter, and stays put once the shader cache is warm |
| `audio.enabled`, `audio.volume` | `true`, `1.0` | sound, and its volume from 0.0 to 1.0 |
| `game.language` | `""` | `"ja"`, `"de"`, `"fr"`, `"es"` or `"it"`; empty for English |
| `debug.gl_debug` | `false` | logs the GL driver's error messages (slower) |
| `debug.memory_watch_*` | | how often game memory is rechecked for changes the GPU needs; see the file's comments |
| `debug.environment` | `""` | `HALO_*` switches for testing, e.g. `"HALO_FRAME_TIMING=300 HALO_RENDER_PROFILE=1"` logs where each frame's time goes |

The overlay reads like `60 FPS  21 MS  212/3`: frames a second, the slowest
frame of the last second, and shader programs linked / compiled fresh.

## Known issues

- **A hitch the first time a shader is needed.** Compiled shaders are kept
  in `sdmc:/haloce-nx/mesa_shader_cache/`, so each one is compiled once and
  not again in later sessions; the overlay's last number counts the fresh
  ones. A cache filled by one full playthrough could ship with a release so
  nobody meets them; that, and compiling ahead on the idle third core from
  the recorded program list, is the work in progress.
- The picture can look a little darker than expected, and a model was seen
  to flash dark once; under investigation.
- Some letters in menu text sometimes show a thin box around them.
- Shadows look a little off.

## Credits
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
