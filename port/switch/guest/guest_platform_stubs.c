/*
GUEST_PLATFORM_STUBS.C

The small platform-layer entry points that don't need a real host import
yet - pure guest-side logic, or just enough to let the game keep going
without crashing while there's no real backend. Each one is a deliberate
placeholder for the "headless boot" milestone (PORTING.md): get the game
ticking before there's real file I/O, config, or input.

Not here: platform_translate_path (port/linux/src/xbox_files.c defines
it for real now, part of SWITCH_PLATFORM_FILES - the file I/O milestone,
PORTING.md) and every posix_* function (port/linux/src/posix.h) - those
are real host imports now too (port/switch/host/source/host_posix_files.c,
host_posix_io.c), not guest-local stubs.
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

/* config_boolean/integer/real/string are port/linux/src/port_config.c's,
reading sdmc:/haloce-nx/config.toml (it was stubs answering defaults
here, which left display.interpolation and display.vsync reading false) */
int config_boolean(const char *name);

/* d3d8_gl.c's screen_mode_choose: the display the game fills. From it the
game picks its layout width (852 for 16:9 at its fixed 480 lines) and the
scale its screen targets are drawn at (1.5x for 1280x720) - without this
it drew a 640x480 picture pillarboxed in the middle of the screen. */
int platform_screen_mode(long *width, long *height)
{
	extern void platform_video_drawable_size(int *width, int *height);
	int w = 0, h = 0;

	platform_video_drawable_size(&w, &h);
	*width = w;
	*height = h;
	return w > 0 && h > 0;
}

/* source/rasterizer/xbox/rasterizer_xbox_decals.c - the real GLES3
backend (switch_d3d8_null.c's successor) doesn't have an equivalent for
this contiguous-stream-attribute trick */
void halo_d3d_stream_attribute(long reg, long stream)
{
	(void)reg;
	(void)stream;
}

/* frames between the 30 Hz ticks at the display's refresh rate
(port/linux/game/render_interpolation.c), as sdl_platform.c answers it on
the other ports. This answered 0 while config_boolean was a stub: every
frame between two ticks was the same picture drawn again. */
int halo_interpolation_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = config_boolean("display.interpolation");
	return enabled;
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

/* (halo_settings_generation is port_config.c's now) */

/* real now (not a constant 0): AArch64's own CNTPCT_EL0/CNTFRQ_EL0
system counter, the same ordinary EL0 (no host import needed) register
pair switch_win32_null.c's QueryPerformanceCounter/Frequency use -
every caller here only ever subtracts two of these (elapsed time, not
wall-clock date), so a monotonic-since-boot counter is exactly as
correct an answer as a real epoch-relative clock would be for any of
them (profiling - source/game/game.c, source/objects/objects.c,
source/render/*; a cache lock's 3-second stall warning -
source/memory/lruv_cache.c). Every microsecond-conversion division
below is safe without a zero-frequency guard: CNTFRQ_EL0 is a fixed,
real hardware constant on this CPU, never actually zero. */
unsigned long long vita_host_time_us(void)
{
	unsigned long long tick, frequency;

	__asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (tick));
	__asm__("mrs %0, cntfrq_el0" : "=r" (frequency));
	/* one division, by the real (always nonzero) hardware frequency
	directly - not by a scaled-down intermediate that a frequency
	under 1 MHz (not a real possibility on this hardware, but not
	worth assuming either) could turn into a zero divisor. tick*1e6
	only overflows 64 bits past ~11 continuous days of uptime. */
	return frequency ? (tick * 1000000ULL) / frequency : 0;
}

/* main.c's frame cap without interpolation (a frame per 30 Hz tick, not the
same picture again), which it only applies when this exists */
void vita_host_sleep_us(unsigned long microseconds)
{
	struct timespec duration;

	duration.tv_sec = (time_t)(microseconds / 1000000UL);
	duration.tv_nsec = (long)(microseconds % 1000000UL) * 1000L;
	nanosleep(&duration, NULL);
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
