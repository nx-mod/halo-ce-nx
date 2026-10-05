/*
 * HOST_SHADER_STATS.C
 *
 * The two GL entry points this host still hand-writes, plus the compile
 * counters host_fps_draw prints.
 *
 * hostgl_glShaderSource is an ABI adapter, not part of the (now disabled)
 * shader pack: guest_gl.c truncates each string pointer to 32 bits before
 * widening it into a 64-bit slot, so the host has to unpack the array
 * itself before real GLES3 can read it. The other three forward unchanged
 * and only bump a counter, so the game's GL behaviour is identical.
 */

#include <stddef.h>
#include <string.h>
#include <GLES3/gl32.h>

#include "host_shader_stats.h"

volatile unsigned long g_shader_compiles;
volatile unsigned long g_shader_compiles_done;
volatile unsigned long g_shader_programs;

void hostgl_glShaderSource(GLuint shader, GLsizei count, const unsigned long long *strings, const GLint *lengths)
{
	const char *pointers[16];
	GLsizei index;

	if (count > 16)
		count = 16;
	for (index = 0; index < count; index++)
		pointers[index] = (const char *)(unsigned long)strings[index];
	glShaderSource(shader, count, pointers, lengths);
}

void hostgl_glCompileShader(GLuint shader)
{
	g_shader_compiles++;
	glCompileShader(shader);
}

void hostgl_glLinkProgram(GLuint program)
{
	g_shader_programs++;
	glLinkProgram(program);
}

void hostgl_glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	/* the game asking for the status is the compile finishing, as far as we
	are concerned: it is the only signal we get, and it asks exactly once per
	compile, so the two counters cannot drift apart once the driver is idle */
	if (pname == GL_COMPILE_STATUS)
		g_shader_compiles_done++;
	glGetShaderiv(shader, pname, params);
}