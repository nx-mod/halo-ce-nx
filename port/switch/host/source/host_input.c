/*
HOST_INPUT.C

Real controller input (PORTING.md's "wire in audio/controls"
milestone), on the single real `PadState` libnx's own pad.h is built
around - this game only ever reads one gamepad (port 0; the Xbox never
had more here either), so there's no per-player table to manage, same
shape as host_video.c's own single EGL surface. HidNpadButton's bit
layout (hid.h) is exposed to the guest as plain `unsigned long long`
bits rather than pulled in as a type, same reasoning as host_main.c's
posix.h struct-pointer parameters: switch_xinput_null.c (the only
caller) never needs the real enum, just the bit values, which are
fixed by the header, not by this file.
*/

#include <switch.h>

static PadState s_pad;
static int s_pad_initialized;

void host_pad_read(unsigned long long *buttons, int *lx, int *ly, int *rx, int *ry)
{
	HidAnalogStickState left, right;

	if (!s_pad_initialized)
	{
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&s_pad);
		s_pad_initialized = 1;
	}
	padUpdate(&s_pad);
	*buttons = (unsigned long long)padGetButtons(&s_pad);
	left = padGetStickPos(&s_pad, 0);
	right = padGetStickPos(&s_pad, 1);
	*lx = (int)left.x;
	*ly = (int)left.y;
	*rx = (int)right.x;
	*ry = (int)right.y;
}

int host_pad_connected(void)
{
	return s_pad_initialized && padIsConnected(&s_pad);
}
