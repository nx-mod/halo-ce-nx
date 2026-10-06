# Roadmap

Where Halo CE for Switch is and where it is going. Dates are aims, not
promises: every change is tested on real hardware before it ships.

## Released

### Playable (October 2026)

The campaign from the main menu, natively on the Switch's CPU, with sound
and controller input. The game's own Direct3D code runs as a 32-bit-pointer
guest inside a 64-bit homebrew host, and its rendering is translated to
OpenGL ES.

### Smooth, with movies (October 6, 2026)

- **60 fps** for most of a10 (from about 20): draw uploads no longer wait on
  the GPU, lens flare tests no longer stall it, and frames really are
  interpolated between the 30 Hz ticks.
- **Vulkan underneath**: OpenGL ES through nxvk (Zink over NVK, Mesa 26),
  with a shader disk cache, so each shader compiles once, not every session.
- **Movies**: the intro, attract and credits movies, played from the
  disc's Bink files through FFmpeg, and copied off the disc at first launch.
- **One folder**, `sdmc:/haloce-nx/`, for the app and the game: the first
  launch extracts everything and restarts itself into the game.
- **A settings file**, `config.toml`: frame cap, vsync, interpolation, FXAA,
  sharpening, anisotropic filtering, lens flares, the overlay.
- Saves that keep loading across updates; decals and text that no longer
  flicker or garble; three crashes fixed (menus, startup, interpolation).
- The Vita fork's 111 fixes since September merged in.

## Known issues

Listed in the [README](README.md#known-issues).

## Next

- **Big battles at 60**: they run 30-45 fps. Being tried: the game alone
  on core 0 with the driver's threads on core 2, and `display.render_scale`
  for the GPU-bound moments.
- **A native renderer, optional** (`display.renderer = "gl" | "webgpu"`,
  GL by default until it is complete), to drop Zink's GL-to-Vulkan layer
  and its CPU cost per draw. Built the way nx-mod's GameCube/Wii ports
  draw: [aurora-nx](https://github.com/nx-mod/aurora-nx) (console GPU to
  WebGPU, as reference for pipeline caching, recording and texture
  conversion) on [dawn-nx](https://github.com/nx-mod/dawn-nx) and
  [vulkan-nx](https://github.com/nx-mod/vulkan-nx). It lives in the 64-bit
  host, since WebGPU's and Vulkan's structs hold pointers the 32-bit game
  cannot share: the game hands it a compact list of each frame's draws.
- **Ship a filled shader cache** from a full playthrough.

## The clean native port

Where this is going: a clean, fast, native port with the Switch's own
implementations throughout. Done on a branch off `switch`, merged a stage at
a time, each stage playing at least as well as before.

1. **A Switch-owned platform layer.** Video, audio, input, files, threads,
   memory and settings as `port/switch` modules behind one interface. Today
   the Switch build leans on the Linux port's layer (33k lines) and borrows
   the other ports' tools and names (`android_imports.py`,
   `android_gl_stubs.py`, `vita_build.py`, `vita_host_*`). Dead parts go:
   the Mesa 20 build, the text demo, unused stubs, the `.mjx` path.
2. **A renderer interface**, the GL renderer one backend behind it and the
   native one above (on WebGPU) beside it. The case for it, measured in a
   battle: 69 object draws cost 3.3 ms of CPU, about 47 us each, nearly all
   of it the GL-on-Zink path.
3. **A coarse boundary between game and host**: a frame's draw list, an
   audio buffer and an input snapshot cross it, not hundreds of single GL
   calls. Cleaner, and the host can work on one frame while the game builds
   the next.
4. **One 64-bit program**, no guest. The largest step by far, and why it is
   last:
   - Map files are loaded as they are on disc, and their tag data holds
     32-bit pointers inside the game's structures (`struct tag_block`,
     `struct tag_reference`): under 64 bits those structures change size.
     Maps would be converted at load, driven by the game's own tag field
     definitions, into 64-bit layouts.
   - `long` is 64 bits on 64-bit Switch: the game's headers use it 3,300
     times, 700+ as fields of its data. Each becomes an explicit 32-bit
     type where the layout matters.
   - Saves are a snapshot of game memory, pointers included (why the
     arena is pinned at `0x41000000`): saves would need a new format or a
     conversion, and old saves would not carry over as they are.
   - It touches most of `source/`, which makes merging the Vita fork's
     work much harder from then on.

   Stages 1-3 get most of the speed and the cleanliness; this one is
   about the last of both.

## Later

- **Resolution options**, including 1080p docked: the GPU has headroom.
- **Optional 16:9 movies** (stretched or cropped); they are 4:3 now.
- **Bloom and colour grading**, alongside FXAA and sharpening.

## Help wanted

Bug reports with logs are the most useful contribution. The game's own log
is `sdmc:/haloce-nx/debug.txt`; the host's is `host.log` beside the NRO.
Pull requests are welcome, especially for performance.