/*
SWITCH_DSOUND_NULL.C

The "headless boot" DirectSound backend: every sound entry point the
game calls, as a safe no-op/failure. Real audio is libnx's audout
(PORTING.md), not written yet. See switch_d3d8_null.c's header comment
for the generation method and caveats - same here.
*/

#include "platform.h"

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	(void)stream;
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	(void)stream;
	return 0;
}

long __stdcall DirectSoundCreate(struct _GUID *, struct IDirectSound **, struct IUnknown *)
{
	return 0;
}

long __stdcall DirectSoundCreateBuffer(const struct _DSBUFFERDESC *, struct IDirectSoundBuffer **)
{
	return 0;
}

void __stdcall DirectSoundDoWork(void)
{
	
}

void __stdcall DirectSoundUseFullHRTF(void)
{
	
}

long __stdcall IDirectSoundBuffer_Play(struct IDirectSoundBuffer *, unsigned long, unsigned long, unsigned long)
{
	return 0;
}

unsigned long __stdcall IDirectSoundBuffer_Release(struct IDirectSoundBuffer *)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_SetBufferData(struct IDirectSoundBuffer *, void *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_SetCurrentPosition(struct IDirectSoundBuffer *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_SetLoopRegion(struct IDirectSoundBuffer *, unsigned long, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_SetPitch(struct IDirectSoundBuffer *, long)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_SetVolume(struct IDirectSoundBuffer *, long)
{
	return 0;
}

long __stdcall IDirectSoundBuffer_Stop(struct IDirectSoundBuffer *)
{
	return 0;
}

long __stdcall IDirectSound_CommitDeferredSettings(struct IDirectSound *)
{
	return 0;
}

long __stdcall IDirectSound_CreateSoundBuffer(struct IDirectSound *, const struct _DSBUFFERDESC *, struct IDirectSoundBuffer **, struct IUnknown *)
{
	return 0;
}

long __stdcall IDirectSound_CreateSoundStream(struct IDirectSound *, const struct _DSSTREAMDESC *, struct IDirectSoundStream **, struct IUnknown *)
{
	return 0;
}

long __stdcall IDirectSound_DownloadEffectsImage(struct IDirectSound *, const void *, unsigned long, const struct _DSEFFECTIMAGELOC *, struct _DSEFFECTIMAGEDESC **)
{
	return 0;
}

long __stdcall IDirectSound_GetCaps(struct IDirectSound *, struct _DSCAPS *)
{
	return 0;
}

long __stdcall IDirectSound_GetSpeakerConfig(struct IDirectSound *, unsigned long *)
{
	return 0;
}

unsigned long __stdcall IDirectSound_Release(struct IDirectSound *)
{
	return 0;
}

long __stdcall IDirectSound_SetDistanceFactor(struct IDirectSound *, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetI3DL2Listener(struct IDirectSound *, const struct _DSI3DL2LISTENER *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetMixBinHeadroom(struct IDirectSound *, unsigned long, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetOrientation(struct IDirectSound *, float, float, float, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetPosition(struct IDirectSound *, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetRolloffFactor(struct IDirectSound *, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSound_SetVelocity(struct IDirectSound *, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetConeAngles(struct IDirectSoundStream *, unsigned long, unsigned long, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetConeOrientation(struct IDirectSoundStream *, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetConeOutsideVolume(struct IDirectSoundStream *, long, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetFrequency(struct IDirectSoundStream *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetI3DL2Source(struct IDirectSoundStream *, const struct _DSI3DL2BUFFER *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetMaxDistance(struct IDirectSoundStream *, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetMinDistance(struct IDirectSoundStream *, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetMixBins(struct IDirectSoundStream *, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetMixBinVolumes(struct IDirectSoundStream *, unsigned long, const long *)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetMode(struct IDirectSoundStream *, unsigned long, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetPosition(struct IDirectSoundStream *, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetVelocity(struct IDirectSoundStream *, float, float, float, unsigned long)
{
	return 0;
}

long __stdcall IDirectSoundStream_SetVolume(struct IDirectSoundStream *, long)
{
	return 0;
}
