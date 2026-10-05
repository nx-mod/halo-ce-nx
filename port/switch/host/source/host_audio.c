/*
HOST_AUDIO.C

Real audio output (PORTING.md's "wire in audio/controls" milestone):
libnx's audout, fed from a dedicated real guest thread (switch_
xbox_threads.c/host_threads.c) running port/linux/src/dsound_sdl.c's
own mixer (now part of SWITCH_PLATFORM_FILES) - the actual DSP (ADPCM
decode, resampling, 3D rolloff/panning) is reused exactly as proven on
Linux/Vita, not rewritten; only the "feed PCM to the speaker" bottom
layer is native here instead of going through SDL (Switch has none).

Explicit per-buffer in-flight tracking rather than relying on
audoutPlayBuffer's documented-but-unconfirmed-here blocking behavior:
never overwrites a buffer's memory until audoutGetReleasedAudioOutBuffer
has actually named it released, polling with a short real sleep
between checks rather than busy-spinning the audio thread. Real timing/
underrun behavior is the one piece of this whole session's work that
genuinely needs hardware to validate, not just a clean link - noted
here plainly rather than claimed as working.
*/

#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BUFFER_COUNT 4
/* matches dsound_sdl.c's own MIX_CHUNK_FRAMES - one host_audio_write
call per mix() call, no resampling needed between them */
#define AUDIO_BUFFER_FRAMES 1024

static AudioOutBuffer s_buffers[AUDIO_BUFFER_COUNT];
static short *s_buffer_memory[AUDIO_BUFFER_COUNT];
static int s_buffer_in_flight[AUDIO_BUFFER_COUNT];
static int s_initialized;

int host_audio_open(void)
{
	u32 sample_rate_out, channel_count_out;
	PcmFormat format;
	AudioOutState state;
	Result rc;
	int i;
	size_t buffer_bytes = (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS * sizeof(short) + 0xfff) & ~(size_t)0xfff;

	if (s_initialized)
		return 1;
	rc = audoutInitialize();
	if (R_FAILED(rc))
		return 0;
	rc = audoutOpenAudioOut(NULL, NULL, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, &sample_rate_out, &channel_count_out,
		&format, &state);
	if (R_FAILED(rc))
	{
		audoutExit();
		return 0;
	}
	for (i = 0; i < AUDIO_BUFFER_COUNT; i++)
	{
		s_buffer_memory[i] = aligned_alloc(0x1000, buffer_bytes);
		if (!s_buffer_memory[i])
		{
			audoutExit();
			return 0;
		}
		memset(s_buffer_memory[i], 0, buffer_bytes);
		s_buffers[i].next = NULL;
		s_buffers[i].buffer = s_buffer_memory[i];
		s_buffers[i].buffer_size = buffer_bytes;
		s_buffers[i].data_size = AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS * sizeof(short);
		s_buffers[i].data_offset = 0;
	}
	audoutStartAudioOut();
	s_initialized = 1;
	return 1;
}

static void reclaim_released_buffers(void)
{
	for (;;)
	{
		AudioOutBuffer *released = NULL;
		u32 count = 0;
		Result rc = audoutGetReleasedAudioOutBuffer(&released, &count);
		int i;

		if (R_FAILED(rc) || !released)
			return;
		for (i = 0; i < AUDIO_BUFFER_COUNT; i++)
			if (released == &s_buffers[i])
				s_buffer_in_flight[i] = 0;
		if (count <= 1)
			return;
	}
}

/* AUDIO_BUFFER_FRAMES frames of interleaved int16 stereo PCM - blocks
(briefly sleeping, never busy-spinning) until a buffer slot is
actually free, giving the guest's mixer thread real-time backpressure */
void host_audio_write(const short *pcm)
{
	int slot;

	if (!s_initialized)
		return;
	for (;;)
	{
		reclaim_released_buffers();
		for (slot = 0; slot < AUDIO_BUFFER_COUNT; slot++)
			if (!s_buffer_in_flight[slot])
				break;
		if (slot < AUDIO_BUFFER_COUNT)
			break;
		svcSleepThread(1000000); /* 1 ms */
	}
	memcpy(s_buffer_memory[slot], pcm, AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS * sizeof(short));
	s_buffer_in_flight[slot] = 1;
	audoutAppendAudioOutBuffer(&s_buffers[slot]);
}

int host_audio_frames_per_buffer(void)
{
	return AUDIO_BUFFER_FRAMES;
}
