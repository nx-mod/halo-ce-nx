/*
SWITCH_XINPUT_NULL.C

The "headless boot" input/save-game/debug-monitor backend: the Xbox
save-game API (real backend: a host file-I/O bridge, same deferred
category as posix_stat/fstat) and the Xbox debug monitor (xbdm) calls
the game probes for at startup. See switch_d3d8_null.c's header
comment for the generation method and caveats.

Controller input is real now (PORTING.md's "wire in audio/controls"
milestone) - host_input.c's single real PadState, port 0 only (the
Xbox never had more here either). XInitDevices/XGetDeviceChanges for
anything other than the one real gamepad (memory units, the debug
keyboard) still report zero devices; every XInputOpen caller checks
for a null handle already (so does the Xbox port for a disconnected
controller) - a key difference from switch_d3d8_null.c's Create*
stubs, where "report failure" isn't always safe: here it's the
designed, already-handled path.
*/

#include "platform.h"

#include <string.h>

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

extern void host_pad_read(unsigned long long *buttons, int *lx, int *ly, int *rx, int *ry);
extern int host_pad_connected(void);

void *__stdcall XInputOpen(struct _XPP_DEVICE_TYPE *device_type, unsigned long port, unsigned long slot,
	struct _XINPUT_POLLING_PARAMETERS *polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	/* one real device: the gamepad, port 0 - XGetDeviceChanges only ever
	reports that one as inserted, so this is the only combination a
	well-behaved caller (input_xbox.c) ever actually asks to open; any
	other request (another port, memory units, the debug keyboard)
	correctly finds no device here either */
	if (device_type == XDEVICE_TYPE_GAMEPAD && port == 0)
		return (void *)1;
	return 0;
}

void __stdcall XInputClose(void *)
{

}

static short clamp_axis(int value)
{
	return (short)(value > 32767 ? 32767 : value < -32767 ? -32767 : value);
}

unsigned long __stdcall XInputGetState(void *device, struct _XINPUT_STATE *state)
{
	static unsigned long packet_number;
	unsigned long long buttons;
	int lx, ly, rx, ry;

	if (!device || !state)
		return ERROR_DEVICE_NOT_CONNECTED;
	memset(state, 0, sizeof(*state));
	host_pad_read(&buttons, &lx, &ly, &rx, &ry);
	/* bumped unconditionally rather than only when something actually
	changed: callers that compare packet numbers to skip redundant work
	just do that work every poll instead - never wrong, only ever a
	cheap, harmless miss of an optimization input_xbox.c doesn't even
	make (it reads Gamepad fields directly, not dwPacketNumber) */
	state->dwPacketNumber = ++packet_number;
	/* HidNpadButton bit values (hid.h) - fixed by that header, not
	guessed; see host_input.c's own comment for why the enum itself
	isn't pulled in here instead. Mapped by physical position, not
	letter (Nintendo's A/B/X/Y layout is rotated versus Xbox's): bottom
	face -> A, right face -> B, left face -> X, top face -> Y. ZL/ZR
	(real triggers) -> the Xbox triggers (grenade/fire); L/R (shoulder)
	-> BLACK/WHITE (grenade switch/flashlight) - Switch has four of
	these where the Vita only has two, so unlike vita_pad.c neither
	needs to borrow the D-pad. Stick clicks -> the Xbox thumb buttons
	(crouch/zoom) directly, since these sticks (unlike the Vita's) are
	actually clickable - no crouch-toggle workaround needed either. */
	if (buttons & (1ULL<<1)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A] = 255;  /* B: bottom */
	if (buttons & (1ULL<<0)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B] = 255;  /* A: right */
	if (buttons & (1ULL<<3)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_X] = 255;  /* Y: left */
	if (buttons & (1ULL<<2)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_Y] = 255;  /* X: top */
	if (buttons & (1ULL<<6)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255; /* L */
	if (buttons & (1ULL<<7)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] = 255; /* R */
	if (buttons & (1ULL<<8)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;  /* ZL */
	if (buttons & (1ULL<<9)) state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255; /* ZR */
	if (buttons & (1ULL<<10)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_START;      /* Plus */
	if (buttons & (1ULL<<11)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;       /* Minus */
	if (buttons & (1ULL<<4)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;  /* StickL click */
	if (buttons & (1ULL<<5)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB; /* StickR click */
	if (buttons & (1ULL<<12)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (buttons & (1ULL<<13)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (buttons & (1ULL<<14)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (buttons & (1ULL<<15)) state->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	/* untested on hardware which way libnx's stick y actually points -
	kept as a direct passthrough (matching Xbox's own sThumbLY: positive
	is forward/up) until confirmed; a flipped look/move axis is the only
	possible symptom if this guess is backwards, trivial to fix in
	exactly one place */
	state->Gamepad.sThumbLX = clamp_axis(lx);
	state->Gamepad.sThumbLY = clamp_axis(ly);
	state->Gamepad.sThumbRX = clamp_axis(rx);
	state->Gamepad.sThumbRY = clamp_axis(ry);
	return ERROR_SUCCESS;
}

unsigned long __stdcall XInputSetState(void *, struct _XINPUT_FEEDBACK *)
{
	return 0;
}

void __stdcall XInitDevices(unsigned long, struct _XDEVICE_PREALLOC_TYPE *)
{

}

int __stdcall XGetDeviceChanges(struct _XPP_DEVICE_TYPE *device_type, unsigned long *insertions,
	unsigned long *removals)
{
	static int gamepad_reported;

	*insertions = 0;
	*removals = 0;
	/* reported once, the first time anything asks, and never removed -
	no hot-unplug handling (host_pad_connected exists for a future,
	real version of this if that ever matters; input_xbox.c only ever
	acts on this once per real insertion/removal either way) */
	if (device_type == XDEVICE_TYPE_GAMEPAD && !gamepad_reported)
	{
		gamepad_reported = 1;
		*insertions = 1; /* bit 0: port 0 */
		return 1;
	}
	return 0;
}

/* no debug keyboard: the queue is always empty. 0 (ERROR_SUCCESS) here
hung input_initialize - input_update_keyboard_devices drains the queue
with while (XInputDebugGetKeystroke(...) == ERROR_SUCCESS) */
unsigned long __stdcall XInputDebugGetKeystroke(struct _XINPUT_DEBUG_KEYSTROKE *)
{
	return 38; /* ERROR_HANDLE_EOF, as port/linux/src/xinput_sdl.c */
}

unsigned long __stdcall XInputDebugInitKeyboardQueue(struct _XINPUT_DEBUG_KEYQUEUE_PARAMETERS *)
{
	return 0;
}

/* source/cache/physical_memory_map.c's physical_memory_allocate() - the
SECOND real call main() makes, right after fuck_code_in_the_eye() -
calls this for the game's core data (game state, tag cache, texture
cache, sound cache) and match_assert()s the result is non-NULL. A
plain "return 0" here isn't a safe placeholder like most of this
file's stubs are (this file's own header comment) - it's an immediate
crash on startup, before any game or rendering code ever runs. Real
implementation instead, matching port/linux/src/xbox_memory.c's exact
wrapper (including its parameter swap: XPhysicalAlloc's own
(size, physical_address, alignment, protect) vs
platform_contiguous_alloc's (size, alignment, physical_address,
protect)) over switch_contiguous_memory.c's allocator - written in
Milestone 9 but never actually connected to anything until now. */
void *__stdcall XPhysicalAlloc(unsigned long size, unsigned long physical_address, unsigned long alignment,
	unsigned long protect)
{
	return platform_contiguous_alloc(size, alignment,
		physical_address < PLATFORM_CONTIGUOUS_SIZE ? physical_address : PLATFORM_ANY_PHYSICAL_ADDRESS, protect);
}

void __stdcall XPhysicalFree(void *address)
{
	platform_contiguous_free(address);
}

void __stdcall XPhysicalProtect(void *address, unsigned long size, unsigned long protect)
{
	(void)address;
	(void)size;
	(void)protect;
}

/* XPhysicalProtect above never changes protection on Switch, so every
page really is read/write - physical_memory_verify asserts exactly that */
unsigned long __stdcall XQueryMemoryProtect(void *)
{
	return 0x04; /* PAGE_READWRITE */
}

/* source/shell/shell_xbox.c's main() calls fuck_code_in_the_eye() (an
anti-tamper check) unconditionally, first thing, before anything else -
it walks modules/sections in a "while (Dm...(...) != XBDM_ENDOFLIST)"
loop. Returning plain 0 ("success", meaning "here's a module") instead
of XBDM_ENDOFLIST would make that loop spin forever on startup, since
0 != XBDM_ENDOFLIST is always true and nothing here ever writes to the
*module/*section output the loop then goes on to read. "No modules to
walk" is also the honestly correct answer - there's no real Xbox debug
monitor module list behind this. */
HRESULT __stdcall DmWalkLoadedModules(PDM_WALK_MODULES *walk, PDMN_MODLOAD module)
{
	(void)walk;
	(void)module;
	return XBDM_ENDOFLIST;
}

HRESULT __stdcall DmWalkModuleSections(PDM_WALK_MODSECT *walk, const char *module_name, PDMN_SECTIONLOAD section)
{
	(void)walk;
	(void)module_name;
	(void)section;
	return XBDM_ENDOFLIST;
}

HRESULT __stdcall DmCloseModuleSections(PDM_WALK_MODSECT walk)
{
	return 0;
}
