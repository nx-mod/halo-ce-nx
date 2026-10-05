# Roadmap

Where Halo CE for Switch is and where it is going. Dates are aims, not
promises: every change is tested on real hardware before it ships.

## Released

### Playable (October 2026)

The campaign from the main menu, natively on the Switch's CPU, with sound
and controller input. The game's own Direct3D code runs as a 32-bit-pointer
guest inside a 64-bit homebrew host, and its rendering is translated to
GLES3. Presented at 60 fps.

## Known issues

Listed in the [README](README.md#known-issues). The ones that matter most:

- **A hitch the first time an effect appears.** 10–70 ms per new shader,
  and this driver offers nowhere to cache a compiled program that survives
  to the next run. All three routes were built and measured closed; see
  [PORTING.md](PORTING.md#shaders-why-there-is-no-precompiled-pack).
- **No intro movies.** Bink video isn't supported; the game skips them.
- Saves made with an older build may not load in a newer one.

## Next

- **Bink video**, so the intro movies and any in-game videos play. The
  Xbox Bink decoder has been partially decompiled already
  (`libs/binkxbox/`), so the work is finishing it and wiring it to the
  video and audio paths — not starting from nothing. Options range from
  completing that port to transcoding the movies to a format the Switch can
  already play; the latter is much easier and needs a conversion pass, the
  former needs no per-video work.
- **Holding 60 fps in combat.** It presents at 60 but settles near 30,
  because rendering an interpolated frame costs about what a simulation
  tick costs. The most promising fix is not re-posing static geometry,
  which is most of a level.
- **Text**: garbled for a moment while it loads, and a thin box around some
  letters.
- **Decals** can appear late or briefly look wrong.
- **Shadows** look a little off.

## Later

- **The main menu's music**, which the port doesn't currently play.
- **Fullscreen and resolution options.** It runs at a native 1280x720;
  there is a downscale lever on fragment cost if 60 fps in combat needs
  one.
- **16:9 movies shown at their own aspect ratio**, once there are movies.

## Help wanted

Bug reports with logs are the most useful contribution. The game's own log
is `sdmc:/haloce-nx/debug.txt`; the host's is `host.log` beside the NRO.
Pull requests are welcome, especially for performance, and for Bink —
that's the largest single piece of work left, and a partial decoder that
plays the existing Xbox `.bik` files directly is more useful than a
transcode.