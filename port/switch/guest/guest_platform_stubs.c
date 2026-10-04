/*
GUEST_PLATFORM_STUBS.C

The small platform-layer entry points that don't need a real host import
yet - pure guest-side logic, or just enough to let the game keep going
without crashing while there's no real backend. Each one is a deliberate
placeholder for the "headless boot" milestone (PORTING.md): get the game
ticking before there's real file I/O, config, or input.

Not here: posix_stat/posix_fstat/posix_make_directory/posix_truncate
(port/linux/src/posix.h) - same category as D3D8/DirectSound/XInput,
genuinely needs a real host-side implementation (posix.h's own comment:
Android compiles posix_*.c straight into its 64-bit host and calls it
from the ILP32 guest - the same shape we'd need here, with new
host_* imports). Left undefined on purpose until that exists.
*/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern void host_log(const char *text);

void platform_log(const char *format, ...)
{
	char buffer[512];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	host_log(buffer);
}

void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title ? title : "", message ? message : "");
}

/* identity mapping for now - no real Xbox-path -> SD-card-path scheme yet
(there's no real file I/O to target regardless, see the file comment) */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	if (host_path_size == 0)
		return;
	strncpy(host_path, xbox_path ? xbox_path : "", host_path_size - 1);
	host_path[host_path_size - 1] = '\0';
}

/* port/linux/src/port_config.c's cvar system, not ported yet - every
setting reports its "off"/zero default rather than reading real config. */
int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

long config_integer(const char *name)
{
	(void)name;
	return 0;
}

/* source/rasterizer/xbox/rasterizer_xbox_decals.c - the real GLES3
backend (switch_d3d8_null.c's successor) doesn't have an equivalent for
this contiguous-stream-attribute trick */
void halo_d3d_stream_attribute(long reg, long stream)
{
	(void)reg;
	(void)stream;
}

/* frames between the 30 Hz ticks at the display's refresh rate -
port/linux/include/halo_linux_source_fixups.h; the real backend reads
display.interpolation itself once config_boolean is real */
int halo_interpolation_enabled(void)
{
	return 0;
}

/* source/game/player_control.c - no real gamepad-as-mouse backend yet
(PORTING.md: input is libnx hid, not written yet either) */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	if (yaw) *yaw = 0.0f;
	if (pitch) *pitch = 0.0f;
	return 0;
}

/* source/main.c and friends: a generation counter bumped whenever a
display/audio/input setting changes, so callers can re-read it once.
config_boolean/real/string/integer above never report a change, so
this never needs to move either. */
volatile unsigned long halo_settings_generation;

/* real host-synced high-res timing isn't wired up yet (no host_* import
for it) - a monotonically-meaningless 0 until then. Every caller treats
this as "time since some epoch", so returning a constant just means
"no time has passed", not a crash - fine for a build that doesn't render
or tick gameplay against real deltas yet either. */
unsigned long long vita_host_time_us(void)
{
	return 0;
}

/* xinput_sdl.c's test-input debug hook (SDL path, unused on Switch -
PORTING.md: input goes through libnx's hid directly, not SDL) */
void test_input_hold_action(int hold)
{
	(void)hold;
}

/* musl's own src/time/__tz.c is excluded (build_musl.sh: real tzdata
file parsing, deferred along with real file I/O) - but localtime_r.c,
mktime.c and strftime.c call its two internal entry points
unconditionally, not just when a real zoneinfo file exists. UTC with no
DST is a genuinely correct answer, not a placeholder lie - just the
simplest one, until there's a real timezone database to read. */
void __secs_to_zone(long long t, int local, int *isdst, long *offset, long *oppoff, const char **zonename)
{
	(void)t;
	(void)local;
	*isdst = 0;
	*offset = 0;
	if (oppoff)
		*oppoff = 0;
	*zonename = "UTC";
}

const char *__tm_to_tzname(const struct tm *tm)
{
	(void)tm;
	return "UTC";
}
