/*
HOST_VIDEO.C

The host side of the GLES3 renderer bridge (PORTING.md's "unstub video"
milestone): the window/context functions the guest's platform_video_...
and platform_pump_events call directly, and the six host_gl_ helpers
port/linux/src/xgpu.h's HALO_ANDROID/HALO_SWITCH branch needs beyond the
generated hostgl_<name> entry-point table (host_gl_resolve.c).

EGL sequence is the one ~/switch/nxvk/switch/smoke/gl_egl_tri.c already
proved on real hardware this session (PORTING.md): eglGetDisplay ->
eglInitialize -> eglBindAPI -> eglChooseConfig -> eglCreateWindowSurface
on nwindowGetDefault() -> eglCreateContext -> eglMakeCurrent. Guest and
host share one process's address space (unlike Android's guest/host
split across real process boundaries) - every pointer the guest passes
here is already a valid address this code can read or write directly,
no marshaling needed.

NOT yet run on hardware - see PORTING.md's own caveat on every stub this
session. consoleExit happens lazily, the first time platform_video_initialize
is actually called (from inside the guest's entry, already running on
this thread by the time that happens) rather than unconditionally in
main(), so the debug console text still shows up for host_main.c's own
startup/error logging first.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include "host_loading_text.h"

void platform_video_drawable_size(int *width, int *height);

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLSurface s_surface = EGL_NO_SURFACE;
static EGLContext s_context = EGL_NO_CONTEXT;

int platform_video_initialize(unsigned long width, unsigned long height)
{
	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
		EGL_NONE,
	};
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
		EGL_NONE,
	};
	EGLConfig config;
	EGLint config_count = 0;

	(void)width;
	(void)height;
	/* consoleInit (host_main.c's main()) claimed the default window for
	text; the guest's own rendering needs it from here on */
	consoleExit(NULL);

	s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (s_display == EGL_NO_DISPLAY)
		return 0;
	if (!eglInitialize(s_display, NULL, NULL))
		return 0;
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		return 0;
	if (!eglChooseConfig(s_display, config_attribs, &config, 1, &config_count) || config_count < 1)
		return 0;
	s_surface = eglCreateWindowSurface(s_display, config, nwindowGetDefault(), NULL);
	if (s_surface == EGL_NO_SURFACE)
		return 0;
	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, context_attribs);
	if (s_context == EGL_NO_CONTEXT)
		return 0;
	if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context))
		return 0;
	{
		int w = 0, h = 0;

		/* a black frame with the loading text, up until the game presents */
		platform_video_drawable_size(&w, &h);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		host_loading_text_draw(w, h);
		eglSwapBuffers(s_display, s_surface);
	}
	return 1;
}

extern void logf_both(const char *fmt, ...);

void platform_video_drawable_size(int *width, int *height)
{
	EGLint w = 0, h = 0;
	int ok = 0;

	if (s_display != EGL_NO_DISPLAY)
	{
		/* also never checked before this - if either query silently
		failed, w/h stay 0, which the guest then feeds straight into
		glViewport(0,0,0,0) (every draw clipped to a zero-area viewport
		- no GL error, nothing visible) and a height/width aspect ratio
		of 0/0 = NaN (every vertex's x becomes NaN - also no GL error,
		also nothing visible). Either one alone reproduces "no text"
		exactly, with every other diagnostic staying clean. */
		ok = eglQuerySurface(s_display, s_surface, EGL_WIDTH, &w) &&
			eglQuerySurface(s_display, s_surface, EGL_HEIGHT, &h);
	}
	{
		/* asked every frame (halo_screen_commit): log changes only */
		static EGLint last_w = -1, last_h = -1;
		static int last_ok = -1;

		if (w != last_w || h != last_h || ok != last_ok)
		{
			logf_both("platform_video_drawable_size: %dx%d (query %s)\n", (int)w, (int)h, ok ? "ok" : "FAILED");
			last_w = w;
			last_h = h;
			last_ok = ok;
		}
	}
	if (!ok || w <= 0 || h <= 0)
	{
		w = 1280;
		h = 720;
	}
	if (width)
		*width = w;
	if (height)
		*height = h;
}

extern void logf_both(const char *fmt, ...);

void platform_video_swap(void)
{
	static int failure_count;

	if (s_display == EGL_NO_DISPLAY)
		return;
	/* never checked before this - a silently failing swap (e.g.
	EGL_BAD_SURFACE: "swapchain out of date", the exact failure
	~/switch/nxvk's smoke test itself watches for) would mean every
	glClear/glDrawArrays call keeps succeeding into the backbuffer while
	nothing ever actually reaches the screen - indistinguishable from a
	real rendering bug by description alone ("no text"), but a totally
	different fix. Logged, not fatal: only the first few, so a
	persistent failure doesn't spam host.log for the rest of the run. */
	{
		int w = 0, h = 0;

		platform_video_drawable_size(&w, &h);
		host_loading_text_draw(w, h);
	}
	if (!eglSwapBuffers(s_display, s_surface) && failure_count < 5)
	{
		failure_count++;
		logf_both("platform_video_swap: eglSwapBuffers failed, eglGetError=0x%x", eglGetError());
	}
}

/* main thread only (the guest's own comment in sdl_platform.h) - this
host has exactly one thread, the one running the guest's entry point,
so that's trivially satisfied. appletMainLoop's job (detect HOME menu
requests, keep the applet alive) has no real guest-side equivalent to
hand control back to mid-frame yet, so exiting the whole process on
"the applet wants to close" is the simplest correct behavior for now
- the alternative (silently ignoring it) hangs the console instead. */
void platform_pump_events(void)
{
	if (!appletMainLoop())
	{
		if (s_display != EGL_NO_DISPLAY)
			eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		exit(0);
	}
}

/* ---------- xgpu.h's six host_gl_* helpers */

void host_gl_get_string(unsigned int name, int index, char *buffer, unsigned int size)
{
	const unsigned char *text;

	if (!size)
		return;
	text = index >= 0 ? glGetStringi(name, (GLuint)index) : glGetString(name);
	if (!text)
	{
		buffer[0] = '\0';
		return;
	}
	snprintf(buffer, size, "%s", (const char *)text);
}

int host_gl_has_extension(const char *name)
{
	GLint count = 0, i;

	glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	for (i = 0; i < count; i++)
	{
		const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);

		if (extension && !strcmp(extension, name))
			return 1;
	}
	return 0;
}

unsigned int host_gl_read_buffer_word(unsigned int buffer, unsigned int offset)
{
	/* GLES3 has no glGetBufferSubData (desktop-GL only) - map the one
	word needed instead */
	unsigned int value = 0;
	void *mapped;

	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	mapped = glMapBufferRange(GL_ARRAY_BUFFER, (GLintptr)offset, sizeof(value), GL_MAP_READ_BIT);
	if (mapped)
	{
		memcpy(&value, mapped, sizeof(value));
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
	return value;
}

void host_gl_buffer_write(unsigned int target, unsigned int offset, unsigned int size, const void *data)
{
	glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
}

/* a small ring of fences, one per in-flight frame slot - fence_frame
marks "the GPU work queued so far is slot N's", wait_frame blocks until
the GPU finishes whatever was last fenced for that same slot (so the
CPU never gets more than a few frames ahead of the GPU) */
#define FRAME_RING 4
static GLsync s_frame_fences[FRAME_RING];

void host_gl_fence_frame(unsigned int slot)
{
	GLsync *fence = &s_frame_fences[slot % FRAME_RING];

	if (*fence)
		glDeleteSync(*fence);
	*fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void host_gl_wait_frame(unsigned int slot)
{
	GLsync fence = s_frame_fences[slot % FRAME_RING];

	if (fence)
		glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, (GLuint64)-1);
}
