/*
GL_FUNCTIONS.C

Run-time resolution of the OpenGL entry points listed in gl.h.
*/

#include "platform.h"
#define GL_FUNCTIONS_DEFINE
#include "gl.h"

#if defined(HALO_ANDROID) || defined(HALO_SWITCH)
/* Android: guest_sdl.c fakes SDL_GL_GetProcAddress by forwarding to this
(a real guest-side function, not a host import - see
tools/android_gl_stubs.py's generated guest_gl.c). Switch has no SDL at
all (PORTING.md: libnx native audio/input, no SDL dependency) and the
exact same generator produces the exact same function, so this calls it
directly instead of going through a fake SDL shim neither platform
actually needs here. */
typedef void (*guest_gl_function)(void);
guest_gl_function guest_gl_get_proc_address(const char *name);
#define HALO_GL_GET_PROC_ADDRESS(name) guest_gl_get_proc_address(name)
#else
#include <SDL3/SDL.h>
#define HALO_GL_GET_PROC_ADDRESS(name) SDL_GL_GetProcAddress(name)
#endif

#define GL_DEFINE_FUNCTION(name) __typeof__(&name) halo_##name;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)

int gl_functions_load(void)
{
	int success = TRUE;

#define GL_LOAD_FUNCTION(name) \
	halo_##name = (__typeof__(halo_##name))HALO_GL_GET_PROC_ADDRESS(#name); \
	if (!halo_##name) \
	{ \
		platform_log("OpenGL function %s is unavailable", #name); \
		success = FALSE; \
	}
	GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
	return success;
}
