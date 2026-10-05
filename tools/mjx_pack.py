#!/usr/bin/env python3
"""Pack a movie into the .mjx form the Switch port's Bink stand-in reads.

The game asks the Bink SDK for its movies, and there is no Bink decoder
that could answer: the reconstructed libs/binkxbox is missing the decode
math itself (docs/object_matching_logs/bink_handoff_reconciliation_20260919.md),
and Bink is proprietary. But ffmpeg can read Bink, so nothing needs to
decode it here either - the movies are converted once, on a desktop, into
something the port can decode with a few hundred lines it does own.

The form is deliberately not a container library. Both ends are ours, so
the file is a header, a frame table and the frames themselves:

    offset  size  meaning
    0       4     "MJX1"
    4       4     width
    8       4     height
    12      4     frame count
    16      4     frame rate, numerator
    20      4     frame rate, denominator
    24      4     byte offset of the frame table
    28      4     byte offset of the first frame
    32      4     byte offset of the audio track, 0 if there is none
    36      4     the audio track's size in bytes
    40      4     flags (bit 0: an audio track follows)
    44      4     reserved, written as 0
    48      ...   frame_count records of { u32 offset; u32 size; }
                  then the frames, each a whole baseline JPEG
                  then the audio track, if any

Every field is little-endian, matching the guest it is read by.

The audio track is signed 16 bit little endian PCM, interleaved, stereo,
44100 hertz - the one format the reader in port/linux/src/mjx.c accepts,
because it assumes those numbers rather than reading them. That is what it
gets, whether or not the source was anything else. Bink carries its audio as
Vorbis, so this is where that gets unpacked: once, here, on a desktop, rather
than on the console. The cost is size rather than speed - 172 kB a second -
and the saving is that playback never decodes anything at all.

Frames are baseline JPEG (SOF0), which is what -c:v mjpeg writes and what
the port's decoder implements; progressive JPEG is not a thing that needs
to work here, because the encoder is this script. Frame boundaries are
found by scanning for the SOI/EOI markers rather than by trusting a muxer:
JPEG entropy-coded data is byte-stuffed, so a 0xFFD9 cannot appear inside
a scan, and the frame count found this way is checked against ffprobe's
before anything is written.

Usage: mjx_pack.py <input> <output.mjx> [-q QUALITY] [-no-audio]

Any file ffmpeg can read works, so a .bik off the disc is the normal input;
-q is ffmpeg's mjpeg quality, lower is better: 3 is near-transparent and
came out at about 17 kB a frame for Halo's 640x480 movies, 5 trades a
little banding for 20% less, 7 is visibly soft. See PORTING.md.
"""

import os
import struct
import subprocess
import sys

MAGIC = b'MJX1'
HEADER_SIZE = 48
FLAG_HAS_AUDIO = 1


def run(command):
    """Runs ffmpeg/ffprobe and fails loudly if it does."""
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.stderr.write('%s\n' % result.stderr.decode('utf-8', 'replace'))
        raise SystemExit('%s failed' % os.path.basename(command[0]))
    return result.stdout


def probe(path):
    """ffprobe's frame size and count for the input."""
    text = run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                '-show_entries', 'stream=width,height,avg_frame_rate,nb_read_frames',
                '-count_frames', '-of', 'default=noprint_wrappers=1', path])
    info = {}
    for line in text.decode('ascii', 'replace').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            info[key.strip()] = value.strip()
    if not info.get('width') or not info.get('height'):
        raise SystemExit('ffprobe did not report a video stream for %s' % path)
    return info


def encode(source, quality):
    """The input as a raw stream of whole baseline JPEG frames.

    The pictures end up full range, and that is not a choice: JFIF's YCbCr is
    0-255 with no range flag, so ffmpeg's mjpeg encoder always expands a
    limited-range source up to it. Bink's own video is limited (ffprobe says
    color_range=tv), so the pictures are stretched by 255/219 on the way in
    whether or not -pix_fmt asks for it - and asking for yuv420p changes
    nothing. Do not be surprised by it, and do not "fix" it by converting
    again on the port: the conversion here is the correct one, and
    port/linux/src/bink_mjx.c reads the result as full range.
    """
    raw = subprocess.run(
        ['ffmpeg', '-v', 'error', '-i', source, '-an', '-c:v', 'mjpeg',
         '-q:v', str(quality), '-pix_fmt', 'yuvj420p', '-f', 'mjpeg', '-'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if raw.returncode != 0 or not raw.stdout:
        sys.stderr.write(raw.stderr.decode('utf-8', 'replace'))
        raise SystemExit('ffmpeg could not encode %s' % source)
    return raw.stdout


def extract_audio(source):
    """The soundtrack as signed 16 bit little endian stereo PCM at 44100 hertz.

    Those four properties are not negotiable: the reader in
    port/linux/src/mjx.c hard-codes them and does no decoding of its own, so
    anything else here would arrive at the sound system as noise. ffmpeg
    resamples and re-channels on request, so the result is the same whatever
    the source was. Bink stores its audio as Vorbis, and this is the point
    where that stops being our problem.
    """
    raw = subprocess.run(
        ['ffmpeg', '-v', 'error', '-i', source, '-vn', '-f', 's16le',
         '-ar', '44100', '-ac', '2', '-'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if raw.returncode != 0:
        sys.stderr.write(raw.stderr.decode('utf-8', 'replace'))
        raise SystemExit('ffmpeg could not read the audio out of %s' % source)
    # Trim to a whole sample frame, so the reader's division by the frame size
    # cannot come out short and turn the last few samples into noise.
    return raw.stdout[:len(raw.stdout) - len(raw.stdout) % 4]


def split_frames(blob):
    """The byte range of each frame in a raw MJPEG stream."""
    frames = []
    at = 0
    while True:
        start = blob.find(b'\xff\xd8', at)
        if start < 0:
            break
        end = blob.find(b'\xff\xd9', start)
        if end < 0:
            raise SystemExit('a frame at byte %d never ends' % start)
        frames.append((start, end + 2 - start))
        at = end + 2
    return frames


def check_baseline(blob, frames):
    """Refuses anything a baseline decoder could not read."""
    counts = {}
    for start, length in frames:
        at = start + 2
        while at < start + length - 1:
            if blob[at] != 0xFF:
                at += 1
                continue
            marker = blob[at + 1]
            if marker in (0xC0, 0xC1, 0xC2):
                counts[marker] = counts.get(marker, 0) + 1
                break
            at += 2 + int.from_bytes(blob[at + 2:at + 4], 'big')
    if set(counts) - {0xC0, 0xC1}:
        raise SystemExit('not all frames are baseline: %s'
                         % ', '.join('SOF%x x%d' % (m, c) for m, c in sorted(counts.items())))
    return counts


def parse_arguments(argv):
    """The command line, or a complaint about it."""
    source = None
    destination = None
    quality = 3
    audio = True
    at = 0
    while at < len(argv):
        argument = argv[at]
        if argument in ('-q', '--quality'):
            at += 1
            if at == len(argv):
                raise SystemExit('%s needs a number after it' % argument)
            quality = int(argv[at])
        elif argument in ('-no-audio', '--no-audio'):
            audio = False
        elif argument in ('-h', '--help'):
            raise SystemExit(__doc__.strip())
        elif argument.startswith('-'):
            raise SystemExit('unknown option %s; try --help' % argument)
        elif source is None:
            source = argument
        elif destination is None:
            destination = argument
        else:
            raise SystemExit('%s is in the way; there is only one output' % argument)
        at += 1
    if not source or not destination:
        raise SystemExit('usage: mjx_pack.py <input> <output.mjx> [-q QUALITY] [-no-audio]')
    return source, destination, quality, audio


def main():
    source, destination, quality, audio = parse_arguments(sys.argv[1:])
    info = probe(source)
    blob = encode(source, quality)
    frames = split_frames(blob)
    if not frames:
        raise SystemExit('no frames came out of %s' % source)
    expected = info.get('nb_read_frames')
    if expected and expected != 'N/A' and int(expected) != len(frames):
        raise SystemExit('found %d frames but ffprobe counted %s; the frame scan is wrong'
                         % (len(frames), expected))
    counts = check_baseline(blob, frames)
    numerator, _, denominator = info.get('avg_frame_rate', '0/1').partition('/')
    pcm = extract_audio(source) if audio else b''

    audio_at = HEADER_SIZE + 8 * len(frames) + sum(length for _, length in frames)
    with open(destination, 'wb') as out:
        out.write(MAGIC)
        out.write(struct.pack('<IIIIIIIIII', int(info['width']), int(info['height']),
                              len(frames), int(numerator or 0), int(denominator or 1),
                              HEADER_SIZE, HEADER_SIZE + 8 * len(frames),
                              audio_at if pcm else 0, len(pcm),
                              FLAG_HAS_AUDIO if pcm else 0))
        out.write(b'\0' * 4)
        at = HEADER_SIZE + 8 * len(frames)
        for start, length in frames:
            out.write(struct.pack('<II', at, length))
            at += length
        for start, length in frames:
            out.write(blob[start:start + length])
        if pcm:
            out.write(pcm)

    seconds = len(frames) * float(denominator or 1) / float(numerator or 30)
    print('%s: %dx%d, %d frames, %s, %s, %d bytes (%.1f kB a frame)'
          % (destination, int(info['width']), int(info['height']), len(frames),
             ', '.join('SOF%x x%d' % (m, c) for m, c in sorted(counts.items())),
             ('%.1f s of 44100 hertz stereo audio' % seconds) if pcm else 'no audio',
             os.path.getsize(destination),
             os.path.getsize(destination) / 1024.0 / len(frames)))


main()