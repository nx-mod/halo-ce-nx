/*
HOST_AUDIO.C

Real audio output: libnx's audren (audio renderer) with one stereo voice,
fed from a dedicated guest thread running port/linux/src/dsound_sdl.c's
mixer (switch_audio_thread) - the DSP itself (ADPCM decode, resampling,
3D rolloff/panning) is the shared, already-proven code; only "PCM to the
speaker" is native here.

audren, not audout: audoutOpenAudioOut failed on hardware ("cannot open
an audio device; sound is silent"), and ~/switch/libdol-nx moved off
audout for the same kind of trouble. This follows its working setup:
one page-aligned mempool holding every buffer, and buffer ownership read
from AudioDriverWaveBuf.state, which only the driver writes - refreshed
by audrvUpdate before every check, or a full ring looks full forever.
*/

#include <string.h>
#include <switch.h>

extern void logf_both(const char *fmt, ...);

#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLE_RATE 48000
/* matches dsound_sdl.c's MIX_CHUNK_FRAMES: one host_audio_write per mix() */
#define AUDIO_BUFFER_FRAMES 1024
#define AUDIO_BUFFER_BYTES (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS * sizeof(s16))
/* about 128 ms queued at 48 kHz */
#define AUDIO_BUFFER_COUNT 6
#define AUDIO_VOICE 0

static const AudioRendererConfig s_config =
{
	.output_rate = AudioRendererOutputRate_48kHz,
	.num_voices = 4,
	.num_effects = 0,
	.num_sinks = 1,
	.num_mix_objs = 1,
	.num_mix_buffers = 2,
};

/* the mempool must be page-aligned and a whole number of pages; each
buffer is exactly one 4 KiB page */
static s16 s_samples[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS] __attribute__((aligned(0x1000)));
static AudioDriverWaveBuf s_wavebufs[AUDIO_BUFFER_COUNT];
static AudioDriver s_driver;
static int s_initialized;

int host_audio_open(void)
{
	static const u8 sink_channels[AUDIO_CHANNELS] = {0, 1};
	Result rc;
	int pool;
	int i;

	if (s_initialized)
		return 1;
	rc = audrenInitialize(&s_config);
	if (R_FAILED(rc))
	{
		logf_both("host_audio_open: audrenInitialize failed, rc=0x%x\n", rc);
		return 0;
	}
	rc = audrvCreate(&s_driver, &s_config, AUDIO_CHANNELS);
	if (R_FAILED(rc))
	{
		logf_both("host_audio_open: audrvCreate failed, rc=0x%x\n", rc);
		audrenExit();
		return 0;
	}
	pool = audrvMemPoolAdd(&s_driver, s_samples, sizeof(s_samples));
	if (pool < 0 || !audrvMemPoolAttach(&s_driver, pool) ||
		audrvDeviceSinkAdd(&s_driver, AUDREN_DEFAULT_DEVICE_NAME, AUDIO_CHANNELS, sink_channels) < 0)
	{
		logf_both("host_audio_open: mempool/sink setup failed (pool=%d)\n", pool);
		audrvClose(&s_driver);
		audrenExit();
		return 0;
	}
	rc = audrvUpdate(&s_driver);
	if (R_SUCCEEDED(rc))
		rc = audrenStartAudioRenderer();
	if (R_FAILED(rc) || !audrvVoiceInit(&s_driver, AUDIO_VOICE, AUDIO_CHANNELS, PcmFormat_Int16, AUDIO_SAMPLE_RATE))
	{
		logf_both("host_audio_open: renderer start/voice init failed, rc=0x%x\n", rc);
		audrvClose(&s_driver);
		audrenExit();
		return 0;
	}
	audrvVoiceSetDestinationMix(&s_driver, AUDIO_VOICE, AUDREN_FINAL_MIX_ID);
	audrvVoiceSetMixFactor(&s_driver, AUDIO_VOICE, 1.0f, 0, 0);
	audrvVoiceSetMixFactor(&s_driver, AUDIO_VOICE, 1.0f, 1, 1);
	audrvVoiceStart(&s_driver, AUDIO_VOICE);
	memset(s_samples, 0, sizeof(s_samples));
	for (i = 0; i < AUDIO_BUFFER_COUNT; i++)
	{
		memset(&s_wavebufs[i], 0, sizeof(s_wavebufs[i]));
		s_wavebufs[i].data_raw = s_samples[i];
		s_wavebufs[i].size = AUDIO_BUFFER_BYTES;
		s_wavebufs[i].start_sample_offset = 0;
		s_wavebufs[i].end_sample_offset = AUDIO_BUFFER_FRAMES;
		s_wavebufs[i].state = AudioDriverWaveBufState_Free;
	}
	armDCacheFlush(s_samples, sizeof(s_samples));
	audrvUpdate(&s_driver);
	logf_both("host_audio_open: audren running, %d x %d frames at %d Hz\n",
		AUDIO_BUFFER_COUNT, AUDIO_BUFFER_FRAMES, AUDIO_SAMPLE_RATE);
	s_initialized = 1;
	return 1;
}

/* AUDIO_BUFFER_FRAMES frames of interleaved int16 stereo. Blocks (1 ms
sleeps, not a spin) until a buffer is free: real-time backpressure for
the mixer thread. */
void host_audio_write(const short *pcm)
{
	int slot;

	if (!s_initialized)
		return;
	for (;;)
	{
		audrvUpdate(&s_driver);
		for (slot = 0; slot < AUDIO_BUFFER_COUNT; slot++)
			if (s_wavebufs[slot].state == AudioDriverWaveBufState_Free ||
				s_wavebufs[slot].state == AudioDriverWaveBufState_Done)
				break;
		if (slot < AUDIO_BUFFER_COUNT)
			break;
		svcSleepThread(1000000);
	}
	memcpy(s_samples[slot], pcm, AUDIO_BUFFER_BYTES);
	armDCacheFlush(s_samples[slot], AUDIO_BUFFER_BYTES);
	audrvVoiceAddWaveBuf(&s_driver, AUDIO_VOICE, &s_wavebufs[slot]);
	if (!audrvVoiceIsPlaying(&s_driver, AUDIO_VOICE))
		audrvVoiceStart(&s_driver, AUDIO_VOICE);
	audrvUpdate(&s_driver);
}

int host_audio_frames_per_buffer(void)
{
	return AUDIO_BUFFER_FRAMES;
}
