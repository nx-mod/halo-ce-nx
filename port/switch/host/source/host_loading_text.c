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
#include <string.h>
#include <switch.h>
#include <GLES3/gl32.h>

#include "host_loading_text.h"
#include "host_shader_stats.h"

/* the same frame counter the heartbeat thread reports */
extern volatile unsigned long g_host_swap_count;

#define LOADING_TEXT "GITHUB | NX-MOD | HALOCE-NX"
/* the main loop presents every frame; the startup frames come one at a
time with seconds between them */
#define LOADING_TEXT_FRAMES 30 /* a fallback: main.c calls host_loading_text_stop */
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
	case 'F': SEG(0, 0, 0, 1); SEG(0, 1, 1, 1); SEG(0, 0.5f, 0.8f, 0.5f); break;
	case 'P':
		SEG(0, 0, 0, 1); SEG(0, 1, 0.9f, 1); SEG(0.9f, 1, 1, 0.85f); SEG(1, 0.85f, 1, 0.6f);
		SEG(1, 0.6f, 0.85f, 0.45f); SEG(0.85f, 0.45f, 0, 0.45f);
		break;
	/* seven-segment digits: the set of segments is named once, so the numbers
	stay consistent with each other and no glyph needs more than seven */
	case '0': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.85f, 0.75f, 0.85f, 1);
		SEG(0.2f, 0.5f, 0.8f, 0.5f); SEG(0.15f, 0, 0.15f, 0.25f); SEG(0.85f, 0, 0.85f, 0.25f);
		SEG(0.2f, 0, 0.8f, 0);
		break;
	case '1': SEG(0.85f, 0.75f, 0.85f, 1); SEG(0.85f, 0, 0.85f, 0.25f); break;
	case '2': SEG(0.2f, 1, 0.8f, 1); SEG(0.85f, 0.75f, 0.85f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.15f, 0, 0.15f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
	case '3': SEG(0.2f, 1, 0.8f, 1); SEG(0.85f, 0.75f, 0.85f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.85f, 0, 0.85f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
	case '4': SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.85f, 0.75f, 0.85f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.85f, 0, 0.85f, 0.25f);
		break;
	case '5': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.85f, 0, 0.85f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
	case '6': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.15f, 0, 0.15f, 0.25f); SEG(0.85f, 0, 0.85f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
	case '7': SEG(0.2f, 1, 0.8f, 1); SEG(0.85f, 0.75f, 0.85f, 1); SEG(0.85f, 0, 0.85f, 0.25f); break;
	case '8': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.85f, 0.75f, 0.85f, 1);
		SEG(0.2f, 0.5f, 0.8f, 0.5f); SEG(0.15f, 0, 0.15f, 0.25f); SEG(0.85f, 0, 0.85f, 0.25f);
		SEG(0.2f, 0, 0.8f, 0);
		break;
	case '9': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.85f, 0.75f, 0.85f, 1);
		SEG(0.2f, 0.5f, 0.8f, 0.5f); SEG(0.85f, 0, 0.85f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
	case '.': SEG(0.35f, 0, 0.65f, 0.05f); break;
	case 'S': SEG(0.2f, 1, 0.8f, 1); SEG(0.15f, 0.75f, 0.15f, 1); SEG(0.2f, 0.5f, 0.8f, 0.5f);
		SEG(0.85f, 0, 0.85f, 0.25f); SEG(0.2f, 0, 0.8f, 0);
		break;
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
target. The font is measured off the glyph height, so one call serves both
the startup text (20 px, centered, at 2/3 down) and the frame counter (14 px,
pinned to a corner). */
#define OVERLAY_TEXT_MAXIMUM 32
static char s_text[OVERLAY_TEXT_MAXIMUM];
static int s_vertex_count;
static int s_built_width, s_built_height;
static float s_built_glyph, s_built_left, s_built_bottom;

static int build_vertices(const char *text, int width, int height, float left, float bottom,
	float glyph_height)
{
	float vertices[OVERLAY_TEXT_MAXIMUM * MAXIMUM_SEGMENTS_PER_GLYPH * 4];
	const float glyph_width = glyph_height * 0.65f, advance = glyph_height * 0.9f;
	const int length = (int)strlen(text);
	int count = 0;
	int i;

	for (i = 0; i < length && i < OVERLAY_TEXT_MAXIMUM - 1; i++)
	{
		struct segment segments[MAXIMUM_SEGMENTS_PER_GLYPH];
		int n = glyph_segments(text[i], segments);
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
	return count > 0;
}

/* re-uploads only when the string or the placement changed; the frame counter
changes its text about once a second, which is not worth a rebuild each frame.
Placement has to be part of the test: the startup text and the frame counter
share this one buffer, and they are different sizes in different places, so
comparing the string alone would draw the counter at the startup text's size. */
static int set_text(const char *text, int width, int height, float left, float bottom, float glyph_height)
{
	if (!strcmp(s_text, text) && width == s_built_width && height == s_built_height &&
		glyph_height == s_built_glyph && left == s_built_left && bottom == s_built_bottom)
	{
		return s_vertex_count > 0;
	}
	snprintf(s_text, sizeof(s_text), "%s", text);
	s_built_width = width;
	s_built_height = height;
	s_built_glyph = glyph_height;
	s_built_left = left;
	s_built_bottom = bottom;
	return build_vertices(s_text, width, height, left, bottom, glyph_height);
}

/* draws whatever set_text last uploaded, over the game's own rendering */
static void draw_overlay(int width, int height)
{
	GLint program, vao, array_buffer, framebuffer, viewport[4];
	GLboolean depth, blend, scissor, cull, stencil, color_mask[4];
	int pass;

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

static int ensure_program(void)
{
	if (!s_program && !build_program())
	{
		s_failed = 1;
		return 0;
	}
	return 1;
}

void host_loading_text_stop(void)
{
	s_frames_left = 0;
}

/* centered on a width x height target, for glyph_height tall glyphs */
static float centered_left(const char *text, int width, float glyph_height)
{
	const float advance = glyph_height * 0.9f, glyph_width = glyph_height * 0.65f;
	const int length = (int)strlen(text);

	return ((float)width - advance * (float)length + (advance - glyph_width)) * 0.5f;
}

void host_loading_text_draw(int width, int height)
{
	const float glyph_height = 20.0f;
	float left = centered_left(LOADING_TEXT, width, glyph_height);
	float bottom = (float)height / 3.0f - glyph_height * 0.5f; /* y up: 1/3 from the bottom */

	if (s_frames_left <= 0 || s_failed || width <= 0 || height <= 0)
		return;
	s_frames_left--;
	if (!ensure_program())
		return;
	if (!set_text(LOADING_TEXT, width, height, left, bottom, glyph_height))
	{
		s_failed = 1;
		return;
	}
	draw_overlay(width, height);
}

/* frames per second, from the same counter the heartbeat thread reports, so
the two can be checked against each other. Twice a second is enough to read
and is barely more often than the string actually changes. Centered at the top
of the screen, where nothing else is drawn. */
extern void logf_both(const char *fmt, ...);

void host_fps_draw(int width, int height)
{
	static unsigned long last_count;
	static u64 last_tick;
	static int s_samples, s_draws;
	static unsigned long s_last_reported;
	u64 now = armGetSystemTick();
	float elapsed;
	unsigned long count = g_host_swap_count;
	const float glyph_height = 14.0f;
	char text[OVERLAY_TEXT_MAXIMUM];
	int fps;

	if (s_failed || width <= 0 || height <= 0)
		return;
	elapsed = (float)armTicksToNs(now - last_tick) / 1000000000.0f;
	/* Once a second is enough to sample: the frame rate is averaged over the
	interval and the shader counters only climb. Everything below that guard
	used to be skipped too, which drew the overlay on one frame in thirty and
	left it off the other twenty-nine - a 1 Hz blink at 30 fps. The overlay is
	painted on every frame; only the numbers behind it are sampled. */
	if (elapsed < 1.0f)
	{
		draw_overlay(width, height);
		return;
	}
	if (!ensure_program())
		return;
	fps = (int)((float)(count - last_count) / elapsed);
	if (fps < 0)
		fps = 0;
	last_count = count;
	last_tick = now;
	/* shader work next to the frame rate: compiles finished / compiles issued.
	Both only climb. The game issues a compile and immediately asks for its
	status, so these normally match - a gap means the driver has work
	outstanding, which is the stall. See tools/switch_gl_resolve.py for why
	there is no cache to hide it. */
	snprintf(text, sizeof(text), "%d FPS  %lu/%lu", fps, g_shader_compiles_done, g_shader_compiles);
	/* The counter was reported as flashing on and off too fast to read, which
	has two very different causes: the string churning, or the draw not
	happening. These lines tell them apart. Only the first few, so a working
	overlay stays quiet. */
	if (s_samples < 12)
	{
		s_samples++;
		logf_both("fps overlay: \"%s\" from %lu swaps over %.2fs, %d vertices\n", text,
			count - s_last_reported, elapsed, s_vertex_count);
		s_last_reported = count;
	}
	/* 10 px below the top edge */
	if (!set_text(text, width, height, centered_left(text, width, glyph_height), (float)height - glyph_height - 10.0f,
		glyph_height))
	{
		s_failed = 1;
		logf_both("fps overlay: nothing to draw for \"%s\"\n", text);
		return;
	}
	if (!(++s_draws % 60) && s_samples >= 12)
		logf_both("fps overlay: %d draws, %u swap calls, \"%s\"\n", s_draws, (unsigned)count, text);
	draw_overlay(width, height);
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
