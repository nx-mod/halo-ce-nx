# Movies without Bink: the `.mjx` pipeline

> On Switch the disc's `.bik` files now play directly through FFmpeg
> (`port/switch/host/source/host_bik.c`), extracted at first launch; a
> `.mjx` beside a `.bik` is still played first. The rest of this document
> describes the `.mjx` form, which other ports and pre-converted installs
> can still use.

Halo's intro and ending movies are Bink, and Bink cannot be decoded here. The
RAD SDK is proprietary, and the reconstructed `libs/binkxbox` is missing the
decode math itself (see
`docs/object_matching_logs/bink_handoff_reconciliation_20260919.md`). Three
files replace it:

| file | what it is |
|---|---|
| `tools/mjx_pack.py` | converts a `.bik` into `.mjx` once, on a desktop |
| `port/linux/src/mjx.c`, `.h` | reads a `.mjx`: baseline JPEG frames to YUV planes |
| `port/linux/src/bink_mjx.c` | answers the Bink SDK calls `source/bink/bink_playback.c` makes |

`port/linux/src/bink_null.c` is what ships today. It reports every movie as
missing, which is a supported state — the game skips missing movies — but no
movie ever plays. `bink_mjx.c` is a drop-in replacement for it and defines the
same symbols.

## Why JPEG and not Bink

ffmpeg can read Bink; nothing else can. So the movies are transcoded rather
than decoded: `tools/mjx_pack.py` rewrites each frame as a whole baseline
JPEG, which libjpeg already knows how to decode. devkitPro ships libjpeg-turbo
2.1.2 for Switch aarch64 (`/opt/devkitpro/portlibs/switch/lib/libjpeg.a`), and
`libjpeg`'s `jpeg_read_raw_data` hands back the picture as separate YUV planes
with no colour conversion, which is exactly the shape Bink delivers — the
game's YUV to RGB step is unchanged.

An earlier attempt wrote its own baseline JPEG decoder behind the Bink API.
It decoded, it was fast, and it was subtly wrong in ways that took longer to
find than the decoder took to write. Do not do that again.

## Packing

```
tools/mjx_pack.py bik/intro.bik movies/intro.mjx -q 3
```

`-q` is ffmpeg's mjpeg quality, lower is better: 3 is near-transparent at
about 17 kB a frame for Halo's 640x480 movies, 5 trades a little banding for
20% less, 7 is visibly soft. `-no-audio` leaves the soundtrack out. The audio
comes out as 44100 hertz signed 16 bit little endian stereo PCM, which
`mjx.c` hard-codes and does no decoding of; Bink carries Vorbis, so ffmpeg
unpacks it here rather than on the console. That costs 172 kB a second and
buys playback that never decodes anything.

The packer checks the frame count against ffprobe's and refuses anything that
is not baseline, because it is the only thing standing between a bad encode
and a decoder that quietly mis-reads it.

### One thing that will look wrong and is not

The pictures in a `.mjx` are **full range**, even though Bink's are limited
(`ffprobe` says `color_range=tv`). That is not a mistake to fix: JFIX's YCbCr
has no range flag, so ffmpeg's mjpeg encoder always expands a limited-range
source by 255/219 on the way in. A luma of 193 is stored as 207. Therefore
`bink_mjx.c` converts with **full-range** BT.601 coefficients (`359/88/183/454`,
`Y << 8`). Using the limited-range coefficients that `port/vita/host/vita_movie.c`
uses — because its source is an H.264 file the Vita's own player handles —
stretches the picture a second time. Measured against ffmpeg's own `rgb24`,
full range agrees to a max difference of 3 and 51.8 dB; limited range was
24.2 dB.

## Build wiring (done for the Switch)

`tools/switch_build.py` lists `bink_mjx.c` and `mjx.c` in place of
`bink_null.c`. One correction to the plan below: devkitPro's `libjpeg.a` is
built for the 64-bit ABI (`ELF 64-bit`, like everything in `portlibs`), so the
ILP32 guest cannot link it. The **host** links it instead, and decodes each
picture for the guest: `mjx.c` reads the frame's bytes as before, then calls
the host import `host_mjx_decode` (`port/switch/host/source/host_mjx.c`), which
runs the same `jpeg_read_raw_data` loop into the guest's plane storage, laid
out by `port/linux/src/mjx_host.h`. On Switch `mjx.h` does not include
`jpeglib.h` at all (its `boolean` clashes with the game's). Desktop and Vita
are unchanged.

Movies go in `sdmc:/haloce-nx/bink/` as `<name>.mjx`.

## The original wiring plan

`tools/switch_build.py:99` and `tools/vita_build.py:110` both list
`bink_null.c`. For the Switch:

1. In `tools/switch_build.py`, in the list at line 99, replace `"bink_null.c"`
   with `"bink_mjx.c"` and add `"mjx.c"`.
2. Add `-L/opt/devkitpro/portlibs/switch/lib -ljpeg` to the **guest link**.
   `build.ninja` has no link step for the guest ("No link step yet - see
   port/switch/PORTING.md"), so this is a hand-typed command line, and the
   include path is already there: `-I/opt/devkitpro/portlibs/switch/include`
   is in the Switch compile flags, so `#include <jpeglib.h>` resolves as-is.
   Only the library has to be named.
3. Leave `bink_null.c` in the tree. It is what the desktop and Vita builds
   still use, and the Vita has its own player.

**libjpeg is a new dependency of the guest.** Nothing in the guest links it
yet, and nothing else in the port needs it.

### Two ABI facts worth knowing before editing these files

- **The guest is ILP32.** The Linux guest compiles `--target=i686-linux-gnu
  -m32`; Switch needs `-mabi=ilp32`. `port/linux/include/halo_linux_prefix.h`
  refuses to compile otherwise, with the reason "game data structures assume
  32-bit pointers". So `unsigned long` is 4 bytes and
  `sizeof(BINKSUMMARY) == 0x7C` really does hold.
- **`bink_mjx.c` spells the stats structs with `unsigned int`, not
  `unsigned long`**, even though `bink_playback.c:216` uses `unsigned long`.
  Both are 4 bytes here, so they agree — but the file carries the same static
  asserts `bink_playback.c:323` does, which is the point: if that ever stops
  being true, this file fails to compile instead of quietly writing 8-byte
  fields over a 4-byte layout and corrupting the debug overlay's counters.

`BinkOpen` maps `d:\bink\<name>.bik` to the same path with `.mjx` and hands it
to `platform_translate_path`, so the movies go wherever `paths.data` points.

## Audio

`BinkOpenDirectSound` opens a real streaming voice on the game's `IDirectSound`
(`IDirectSound_CreateSoundStream`, 44100 Hz stereo s16) and hands it back as the
Bink SDK's sound system. There is no RAD-proprietary sound-system layout to
reproduce here: `bink_playback.c` only passes the open proc to
`BinkSetSoundSystem` and never touches the result, so the token and everything
behind it are this file's own business.

Four things about the mixer in `port/linux/src/dsound_sdl.c` drive the design,
and all four are easy to get wrong:

- **It copies the `XMEDIAPACKET` and only reads `pvBuffer` later**, on its own
  thread (`dsound_sdl.c:988`). A packet's samples must therefore stay put and
  stay unmodified until the mixer has finished with them, which rules out one
  shared staging buffer. There is a ring of 16, each recycled only once its
  `*pdwStatus` has left `XMEDIAPACKET_STATUS_PENDING`.
- **It drains at exactly the format's sample rate and never corrects.** A stream
  that runs dry just goes silent until the next packet. So what has to be right
  is how many sample frames are handed over per *picture* frame — too many and
  the soundtrack runs ahead of the picture, too few and it lags, and "queue the
  next packet" gets neither right on its own. Each picture frame's share comes
  from the same rational rate the pictures use, as a running total:
  `accounted * 44100 * fps_denominator / fps_numerator`. For 2997/100 that is
  1471.47 sample frames per picture frame, and truncating it per frame instead
  would throw away a twentieth of a sample each time — 12 ms of silence across
  the movie.
- **`dwMaxSize` is a byte count**, not a frame count.
- **Completion is a status the mixer writes from another thread.** There is no
  event to wait on: the status pointer is polled. (`dsound_sdl.c` does
  `SetEvent` or a callback if the packet asks for one; this producer asks for
  neither and polls.)

`BinkDoFrame` queues one picture frame's share, and `BinkOpen` queues six frames
first so the mixer is never waiting on the decode for data. That preroll is
queue *depth*, not a delay: steady state keeps it there.

`BinkClose` flushes before freeing. `IDirectSoundStream_Flush` completes every
packet the mixer still holds, synchronously, before it returns, which is what
makes freeing the ring's buffers immediately afterwards safe.

### Measured

Driving the real `bink_mjx.c` through the game's own call order, with a fake
mixer that honours the same contract:

| | packets | sample frames queued | skips | queue depth | corrupted buffers |
|---|---|---|---|---|---|
| mixer retiring 1 packet/frame (realistic) | 481 | 707,777 | 0 | 6 | 0 |
| mixer retiring 3 packets/frame | 481 | 707,777 | 0 | 6 | 0 |
| mixer stalled (0 packets/frame) | 16 | 23,543 | 684,234 | 16 | 0 |

707,777 is exactly `481 * 44100 * 100 / 2997`, and the byte stream the mixer
received is **byte-identical to the container's soundtrack** (verified by an
order-sensitive rolling hash over the first 2,825,224 and, separately,
2,831,108 bytes).

Reading audio costs 8-11 ms across the whole 16 s movie, so it is not in the
per-frame budget. The stalled-mixer row is the one worth keeping: the ring caps
at 16, the surplus is counted as skipped rather than queued late, and video
playback is unaffected at 11.97 ms worst.

## What is not finished

Nothing in the `.mjx` pipeline is stubbed. Both halves are implemented and
measured.

What is *not* verified is the whole thing running on hardware: these numbers come
from the aarch64 host with the system `libjpeg.so.62` standing in for
devkitPro's newlib build, and the DirectSound calls were exercised against a
fake mixer that follows `dsound_sdl.c`'s contract, not the real one. The Switch
mixer path (`switch_audio_thread`, `host_audio_open`) is untouched and untested
here.

Video: 481 frames decode at 0.53 ms mean / 1.32 ms worst, and decode plus YUV to
RGB runs 8-11 ms at worst against the 33.4 ms a 29.97 fps frame allows.
