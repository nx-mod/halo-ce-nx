/*
HOST_INPUT.C

Controller input for the guest's XInput (switch_xinput_null.c): one Xbox
gamepad, port 0, read from whatever the player holds. HidNpadButton's bit
layout (hid.h) reaches the guest as plain bits; the mapping to Xbox
buttons is switch_xinput_null.c's.

Every player slot is read and merged (padInitializeAny), not player 1
alone: a controller that drops for a moment - Bluetooth does - can come
back in another slot, and reading only player 1 left it dead until the
player detached and re-attached it. Connections and disconnections are
logged. libnx's PadState is not safe to update from two threads at once,
so the reads take a lock.
*/

#include <switch.h>

extern void logf_both(const char *fmt, ...);

static PadState s_pad;
static int s_pad_initialized;
static Mutex s_pad_lock;
static u32 s_last_style;
static int s_last_connected = -1;

static void log_changes(void)
{
	int connected = padIsConnected(&s_pad) ? 1 : 0;
	u32 style = padGetStyleSet(&s_pad);

	if (connected != s_last_connected || style != s_last_style)
	{
		logf_both("controller: %s (style 0x%x, handheld %s)\n", connected ? "connected" : "none connected",
			(unsigned)style, padIsHandheld(&s_pad) ? "yes" : "no");
		s_last_connected = connected;
		s_last_style = style;
	}
}

void host_pad_read(unsigned long long *buttons, int *lx, int *ly, int *rx, int *ry)
{
	HidAnalogStickState left, right;

	mutexLock(&s_pad_lock);
	if (!s_pad_initialized)
	{
		/* all eight player slots and handheld, any standard controller */
		padConfigureInput(8, HidNpadStyleSet_NpadStandard);
		padInitializeAny(&s_pad);
		s_pad_initialized = 1;
	}
	padUpdate(&s_pad);
	log_changes();
	*buttons = (unsigned long long)padGetButtons(&s_pad);
	left = padGetStickPos(&s_pad, 0);
	right = padGetStickPos(&s_pad, 1);
	mutexUnlock(&s_pad_lock);
	*lx = (int)left.x;
	*ly = (int)left.y;
	*rx = (int)right.x;
	*ry = (int)right.y;
}

int host_pad_connected(void)
{
	int connected;

	mutexLock(&s_pad_lock);
	connected = s_pad_initialized && padIsConnected(&s_pad);
	mutexUnlock(&s_pad_lock);
	return connected;
}
