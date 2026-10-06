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

## Later

- **Resolution options**, including 1080p docked: the GPU has headroom.
- **Optional 16:9 movies** (stretched or cropped); they are 4:3 now.
- **Bloom and colour grading**, alongside FXAA and sharpening.

## Help wanted

Bug reports with logs are the most useful contribution. The game's own log
is `sdmc:/haloce-nx/debug.txt`; the host's is `host.log` beside the NRO.
Pull requests are welcome, especially for performance.