/*
SWITCH_XINPUT_NULL.C

The "headless boot" input/save-game/debug-monitor backend: controller
input (real backend: libnx hid, not written yet - PORTING.md), the
Xbox save-game API (real backend: a host file-I/O bridge, same
deferred category as posix_stat/fstat), and the Xbox debug monitor
(xbdm) calls the game probes for at startup. See switch_d3d8_null.c's
header comment for the generation method and caveats.

XInitDevices/XGetDeviceChanges report zero devices; every XInputOpen
caller checks for a null handle already (so does the Xbox port for a
disconnected controller) - a key difference from switch_d3d8_null.c's
Create* stubs, where "report failure" isn't always safe: here it's the
designed, already-handled path.
*/

#include "platform.h"

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

void *__stdcall XInputOpen(struct _XPP_DEVICE_TYPE *, unsigned long, unsigned long, struct _XINPUT_POLLING_PARAMETERS *)
{
	return 0;
}

void __stdcall XInputClose(void *)
{
	
}

unsigned long __stdcall XInputGetState(void *, struct _XINPUT_STATE *)
{
	return 0;
}

unsigned long __stdcall XInputSetState(void *, struct _XINPUT_FEEDBACK *)
{
	return 0;
}

void __stdcall XInitDevices(unsigned long, struct _XDEVICE_PREALLOC_TYPE *)
{
	
}

int __stdcall XGetDeviceChanges(struct _XPP_DEVICE_TYPE *, unsigned long *, unsigned long *)
{
	return 0;
}

unsigned long __stdcall XInputDebugGetKeystroke(struct _XINPUT_DEBUG_KEYSTROKE *)
{
	return 0;
}

unsigned long __stdcall XInputDebugInitKeyboardQueue(struct _XINPUT_DEBUG_KEYQUEUE_PARAMETERS *)
{
	return 0;
}

unsigned long __stdcall XCreateSaveGame(const char *, const unsigned short *, unsigned long, unsigned long, char *, unsigned int)
{
	return 0;
}

unsigned long __stdcall XDeleteSaveGame(const char *, const unsigned short *)
{
	return 0;
}

void *__stdcall XFindFirstSaveGame(const char *, struct _XGAME_FIND_DATA *)
{
	return 0;
}

int __stdcall XFindNextSaveGame(void *, struct _XGAME_FIND_DATA *)
{
	return 0;
}

int __stdcall XFindClose(void *)
{
	return 0;
}

void *__stdcall XCalculateSignatureBegin(unsigned long)
{
	return 0;
}

unsigned long __stdcall XCalculateSignatureUpdate(void *, const unsigned char *, unsigned long)
{
	return 0;
}

unsigned long __stdcall XCalculateSignatureEnd(void *, struct _XCALCSIG_SIGNATURE *)
{
	return 0;
}

int __stdcall XSetNicknameW(const unsigned short *, int)
{
	return 0;
}

void *__stdcall XFindFirstNicknameW(int, unsigned short *, unsigned int)
{
	return 0;
}

unsigned long __stdcall XGetLanguage(void)
{
	return 0;
}

unsigned long __stdcall XGetLaunchInfo(unsigned long *, struct _LAUNCH_DATA *)
{
	return 0;
}

unsigned long __stdcall XLaunchNewImageA(const char *, struct _LAUNCH_DATA *)
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

unsigned long __stdcall XQueryMemoryProtect(void *)
{
	return 0;
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
