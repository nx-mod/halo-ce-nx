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

void *__stdcall XPhysicalAlloc(unsigned long, unsigned long, unsigned long, unsigned long)
{
	return 0;
}

void __stdcall XPhysicalFree(void *)
{
	
}

void __stdcall XPhysicalProtect(void *, unsigned long, unsigned long)
{
	
}

unsigned long __stdcall XQueryMemoryProtect(void *)
{
	return 0;
}

HRESULT __stdcall DmWalkLoadedModules(PDM_WALK_MODULES *walk, PDMN_MODLOAD module)
{
	return 0;
}

HRESULT __stdcall DmWalkModuleSections(PDM_WALK_MODSECT *walk, const char *module_name, PDMN_SECTIONLOAD section)
{
	return 0;
}

HRESULT __stdcall DmCloseModuleSections(PDM_WALK_MODSECT walk)
{
	return 0;
}
