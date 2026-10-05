/*
HOST_LOADING_TEXT.C

"GITHUB | NX-MOD | HALOCE-NX", centered horizontally two thirds of the
way down, during startup. The game presents a frame when it creates its
device and then nothing until its main loop runs (several seconds of
shell_initialize), so the last presented frame is what stays on screen:
host_video.c draws this over each frame just before the swap, until the
main loop has presented steadily for a while.

A vector-stroke font (GL_LINES), like guest_text_demo.c's: no texture or
glyph atlas to get wrong. All GL state it touches is saved and restored,
since it draws in the middle of the game's own rendering.
*/

#include <stdio.h>
#include <switch.h>
#include <GLES3/gl32.h>

#include "host_loading_text.h"

#define LOADING_TEXT "GITHUB | NX-MOD | HALOCE-NX"
/* the main loop presents every frame; the startup frames come one at a
time with seconds between them */
#define LOADING_TEXT_FRAMES 30
#define MAXIMUM_SEGMENTS_PER_GLYPH 10

struct segment { float x0, y0, x1, y1; };

static int glyph_segments(char c, struct segment *out)
{
#define SEG(a, b, cx, d) do { if (count < MAXIMUM_SEGMENTS_PER_GLYPH) out[count++] = (struct segment){(a), (b), (cx), (d)}; } while (0)
	int count = 0;

	switch (c)
	{
	case 'A': SEG(0, 0, 0.5f, 1); SEG(0.5f, 1, 1, 0); SEG(0.2f, 0.4f, 0.8f, 0.4f); break;
	case 'B':
		SEG(0, 0, 0, 1); SEG(0, 1, 0.75f, 1); SEG(0.75f, 1, 0.9f, 0.85f); SEG(0.9f, 0.85f, 0.9f, 0.62f);
		SEG(0.9f, 0.62f, 0.75f, 0.5f); SEG(0, 0.5f, 0.8f, 0.5f); SEG(0.8f, 0.5f, 1, 0.35f);
		SEG(1, 0.35f, 1, 0.15f); SEG(1, 0.15f, 0.8f, 0); SEG(0.8f, 0, 0, 0);
		break;
	case 'C': SEG(1, 1, 0.2f, 1); SEG(0.2f, 1, 0, 0.8f); SEG(0, 0.8f, 0, 0.2f); SEG(0, 0.2f, 0.2f, 0); SEG(0.2f, 0, 1, 0); break;
	case 'D': SEG(0, 0, 0, 1); SEG(0, 1, 0.7f, 1); SEG(0.7f, 1, 1, 0.7f); SEG(1, 0.7f, 1, 0.3f); SEG(1, 0.3f, 0.7f, 0); SEG(0.7f, 0, 0, 0); break;
	case 'E': SEG(0, 0, 0, 1); SEG(0, 1, 1, 1); SEG(0, 0.5f, 0.8f, 0.5f); SEG(0, 0, 1, 0); break;
	case 'G': SEG(1, 1, 0, 1); SEG(0, 1, 0, 0); SEG(0, 0, 1, 0); SEG(1, 0, 1, 0.45f); SEG(1, 0.45f, 0.5f, 0.45f); break;
	case 'H': SEG(0, 0, 0, 1); SEG(1, 0, 1, 1); SEG(0, 0.5f, 1, 0.5f); break;
	case 'I': SEG(0.5f, 0, 0.5f, 1); SEG(0.2f, 1, 0.8f, 1); SEG(0.2f, 0, 0.8f, 0); break;
	case 'L': SEG(0, 1, 0, 0); SEG(0, 0, 1, 0); break;
	case 'M': SEG(0, 0, 0, 1); SEG(0, 1, 0.5f, 0.35f); SEG(0.5f, 0.35f, 1, 1); SEG(1, 1, 1, 0); break;
	case 'N': SEG(0, 0, 0, 1); SEG(0, 1, 1, 0); SEG(1, 0, 1, 1); break;
	case 'O': SEG(0, 0, 0, 1); SEG(0, 1, 1, 1); SEG(1, 1, 1, 0); SEG(1, 0, 0, 0); break;
	case 'T': SEG(0, 1, 1, 1); SEG(0.5f, 1, 0.5f, 0); break;
	case 'U': SEG(0, 1, 0, 0); SEG(0, 0, 1, 0); SEG(1, 0, 1, 1); break;
	case 'X': SEG(0, 0, 1, 1); SEG(0, 1, 1, 0); break;
	case '-': SEG(0.15f, 0.5f, 0.85f, 0.5f); break;
	case '|': SEG(0.5f, -0.2f, 0.5f, 1.2f); break;
	default: break;
	}
	return count;
#undef SEG
}

static const char *kVertexSource =
	"#version 300 es\n"
	"layout(location = 0) in vec2 aPos;\n"
	"uniform vec2 uOffset;\n"
	"void main() { gl_Position = vec4(aPos + uOffset, 0.0, 1.0); }\n";

static const char *kFragmentSource =
	"#version 300 es\n"
	"precision mediump float;\n"
	"out vec4 FragColor;\n"
	"void main() { FragColor = vec4(0.85, 0.85, 0.85, 1.0); }\n";

static int s_frames_left = LOADING_TEXT_FRAMES;
static int s_failed;
static GLuint s_program, s_vao, s_vbo;
static GLint s_offset_location = -1;
static int s_vertex_count;
static int s_built_width, s_built_height;

static GLuint compile(GLenum stage, const char *source)
{
	GLuint shader = glCreateShader(stage);
	GLint ok = 0;

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static int build_program(void)
{
	GLuint vertex = compile(GL_VERTEX_SHADER, kVertexSource);
	GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentSource);
	GLint ok = 0;

	if (!vertex || !fragment)
		return 0;
	s_program = glCreateProgram();
	glAttachShader(s_program, vertex);
	glAttachShader(s_program, fragment);
	glLinkProgram(s_program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
	if (!ok)
		return 0;
	s_offset_location = glGetUniformLocation(s_program, "uOffset");
	glGenVertexArrays(1, &s_vao);
	glGenBuffers(1, &s_vbo);
	return 1;
}

/* the whole string as line segments in clip space for a width x height
target: glyphs 20 px tall (the smoke test's were ~180), centered, at 2/3 down */
static void build_vertices(int width, int height)
{
	static float vertices[sizeof(LOADING_TEXT) * MAXIMUM_SEGMENTS_PER_GLYPH * 4];
	const float glyph_height = 20.0f, glyph_width = 13.0f, advance = 18.0f;
	const int length = (int)sizeof(LOADING_TEXT) - 1;
	float left = ((float)width - advance * (float)length + (advance - glyph_width)) * 0.5f;
	float bottom = (float)height / 3.0f - glyph_height * 0.5f; /* y up: 1/3 from the bottom */
	int count = 0;
	int i;

	for (i = 0; i < length; i++)
	{
		struct segment segments[MAXIMUM_SEGMENTS_PER_GLYPH];
		int n = glyph_segments(LOADING_TEXT[i], segments);
		float x = left + advance * (float)i;
		int s;

		for (s = 0; s < n; s++)
		{
			vertices[count++] = (x + segments[s].x0 * glyph_width) / (float)width * 2.0f - 1.0f;
			vertices[count++] = (bottom + segments[s].y0 * glyph_height) / (float)height * 2.0f - 1.0f;
			vertices[count++] = (x + segments[s].x1 * glyph_width) / (float)width * 2.0f - 1.0f;
			vertices[count++] = (bottom + segments[s].y1 * glyph_height) / (float)height * 2.0f - 1.0f;
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(float)), vertices, GL_STATIC_DRAW);
	glBindVertexArray(s_vao);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);
	s_vertex_count = count / 2;
	s_built_width = width;
	s_built_height = height;
}

void host_loading_text_draw(int width, int height)
{
	GLint program, vao, array_buffer, framebuffer, viewport[4];
	GLboolean depth, blend, scissor, cull, stencil, color_mask[4];
	int pass;

	if (s_frames_left <= 0 || s_failed || width <= 0 || height <= 0)
		return;
	s_frames_left--;

	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
	depth = glIsEnabled(GL_DEPTH_TEST);
	blend = glIsEnabled(GL_BLEND);
	scissor = glIsEnabled(GL_SCISSOR_TEST);
	cull = glIsEnabled(GL_CULL_FACE);
	stencil = glIsEnabled(GL_STENCIL_TEST);

	if (!s_program && !build_program())
	{
		s_failed = 1;
		glUseProgram((GLuint)program);
		return;
	}
	if (width != s_built_width || height != s_built_height)
		build_vertices(width, height);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(s_program);
	glBindVertexArray(s_vao);
	/* two 1-pixel offsets: slightly bolder strokes without glLineWidth */
	for (pass = 0; pass < 2; pass++)
	{
		glUniform2f(s_offset_location, (float)(pass & 1) * 2.0f / (float)width,
			(float)(pass >> 1) * 2.0f / (float)height);
		glDrawArrays(GL_LINES, 0, s_vertex_count);
	}

	glBindVertexArray((GLuint)vao);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)array_buffer);
	glUseProgram((GLuint)program);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)framebuffer);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND);
	if (scissor) glEnable(GL_SCISSOR_TEST);
	if (cull) glEnable(GL_CULL_FACE);
	if (stencil) glEnable(GL_STENCIL_TEST);
}

/* a blank console until the GL window takes over: the text is shown once,
by host_loading_text_draw, not also as console text first */
void host_loading_text_console(void)
{
	printf("\x1b[2J");
	/* stdout is buffered: without the flush the screen kept whatever
	stderr had already shown */
	fflush(stdout);
	consoleUpdate(NULL);
}
