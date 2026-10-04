/*
GUEST_TEXT_DEMO.C

A "does the real GLES3 pipeline actually work" smoke test, one step up
from flashing solid colors: draws "NX-MOD/HALOCE-NX" on screen as a
vector-stroke font (GL_LINES - no texture/bitmap-font pipeline needed,
so no risk of an unreadable blurry glyph; each letter is a handful of
straight line segments I can reason about directly, like an old
calculator or vector-display font).

Exercises the real pieces for the first time: platform_video_initialize
(host_video.c's EGL sequence), gl_functions_load (resolves every
halo_gl* pointer through guest_gl_get_proc_address - the generated
guest/host GL bridge, tools/android_gl_stubs.py), real shader compile/
link (glCreateShader/glShaderSource/glCompileShader/glLinkProgram - the
same calls ~/switch/nxvk's smoke test proved on this hardware), and
platform_video_swap. NOT yet wired into the game's own rendering in any
way - this is its own standalone call from guest_main.c's entry, purely
to get something real on screen before tackling the actual game
bootstrap (PORTING.md's Milestone 9 "next step").
*/

#include <stddef.h>

/* gl.h is self-sufficient (no platform.h dependency - it doesn't use
BOOL/DWORD/platform_log, just the GLES3 types/functions themselves).
No GL_FUNCTIONS_DEFINE here - gl_functions.c (SWITCH_D3D8_FILES) is the
one translation unit that actually defines the halo_glXxx storage;
this file just wants the extern declarations and the glXxx->halo_glXxx
renaming, same as every other caller. */
#include "gl.h"

extern void host_log(const char *text);
extern int platform_video_initialize(unsigned long width, unsigned long height);
extern void platform_video_drawable_size(int *width, int *height);
extern void platform_video_swap(void);
extern void platform_pump_events(void);
extern int gl_functions_load(void);

/* ---------- the vector font: each glyph is a handful of line segments
in a [0,1]x[0,1] local box (y up). Only the letters "NX-MOD/HALOCE"
need exist. */

struct segment { float x0, y0, x1, y1; };

static int glyph_segments(char c, struct segment *out, int max)
{
#define SEG(a, b, cx, d) do { if (count < max) out[count++] = (struct segment){(a), (b), (cx), (d)}; } while (0)
	int count = 0;

	switch (c)
	{
	case 'N':
		SEG(0, 0, 0, 1);
		SEG(0, 1, 1, 0);
		SEG(1, 0, 1, 1);
		break;
	case 'X':
		SEG(0, 0, 1, 1);
		SEG(0, 1, 1, 0);
		break;
	case '-':
		SEG(0.15f, 0.5f, 0.85f, 0.5f);
		break;
	case 'M':
		SEG(0, 0, 0, 1);
		SEG(0, 1, 0.5f, 0.35f);
		SEG(0.5f, 0.35f, 1, 1);
		SEG(1, 1, 1, 0);
		break;
	case 'O':
		SEG(0, 0, 0, 1);
		SEG(0, 1, 1, 1);
		SEG(1, 1, 1, 0);
		SEG(1, 0, 0, 0);
		break;
	case 'D':
		SEG(0, 0, 0, 1);
		SEG(0, 1, 0.7f, 0.8f);
		SEG(0.7f, 0.8f, 0.7f, 0.2f);
		SEG(0.7f, 0.2f, 0, 0);
		break;
	case '/':
		SEG(0, 0, 1, 1);
		break;
	case 'H':
		SEG(0, 0, 0, 1);
		SEG(1, 0, 1, 1);
		SEG(0, 0.5f, 1, 0.5f);
		break;
	case 'A':
		SEG(0, 0, 0.5f, 1);
		SEG(0.5f, 1, 1, 0);
		SEG(0.2f, 0.4f, 0.8f, 0.4f);
		break;
	case 'L':
		SEG(0, 1, 0, 0);
		SEG(0, 0, 1, 0);
		break;
	case 'C':
		SEG(1, 1, 0.2f, 1);
		SEG(0.2f, 1, 0, 0.8f);
		SEG(0, 0.8f, 0, 0.2f);
		SEG(0, 0.2f, 0.2f, 0);
		SEG(0.2f, 0, 1, 0);
		break;
	case 'E':
		SEG(0, 0, 0, 1);
		SEG(0, 1, 1, 1);
		SEG(0, 0.5f, 0.8f, 0.5f);
		SEG(0, 0, 1, 0);
		break;
	default:
		break;
	}
	return count;
#undef SEG
}

static const char kMessage[] = "NX-MOD/HALOCE-NX";

static const char *kVertexSource =
	"#version 300 es\n"
	"layout(location = 0) in vec2 aPos;\n"
	"uniform float uScaleX;\n"
	"void main() { gl_Position = vec4(aPos.x * uScaleX, aPos.y, 0.0, 1.0); }\n";

static const char *kFragmentSource =
	"#version 300 es\n"
	"precision mediump float;\n"
	"out vec4 FragColor;\n"
	"void main() { FragColor = vec4(0.1, 1.0, 0.75, 1.0); }\n";

static GLuint compile(GLenum stage, const char *source)
{
	GLuint shader = halo_glCreateShader(stage);
	GLint ok = 0;

	halo_glShaderSource(shader, 1, &source, NULL);
	halo_glCompileShader(shader);
	halo_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[512];
		GLint length = 0;

		halo_glGetShaderInfoLog(shader, sizeof(log), &length, log);
		host_log("guest_text_demo: shader compile failed:");
		host_log(log);
		return 0;
	}
	return shader;
}

void guest_text_demo(void)
{
	int width = 1280, height = 720;
	float vertices[2 * 6 * (int)(sizeof(kMessage) - 1)]; /* up to 6 segments/glyph, 2 points/segment, 2 floats/point */
	int vertex_count = 0;
	float glyph_width = 0.075f, glyph_height = 0.5f, advance = 0.10f;
	float total_width = advance * (float)(sizeof(kMessage) - 1);
	float start_x = -total_width * 0.5f;
	GLuint vertex_shader, fragment_shader, program, vao, vbo;
	GLint scale_location;
	int i, frame;

	host_log("guest_text_demo: starting");
	if (!platform_video_initialize((unsigned long)width, (unsigned long)height))
	{
		host_log("guest_text_demo: platform_video_initialize failed");
		return;
	}
	platform_video_drawable_size(&width, &height);
	host_log("guest_text_demo: video initialized");

	if (!gl_functions_load())
		host_log("guest_text_demo: some GL functions were unavailable (continuing anyway)");

	/* build the line-segment vertex buffer for the whole message */
	for (i = 0; kMessage[i]; i++)
	{
		struct segment segments[6];
		int count = glyph_segments(kMessage[i], segments, 6);
		float origin_x = start_x + (float)i * advance;
		int s;

		for (s = 0; s < count; s++)
		{
			vertices[vertex_count++] = origin_x + segments[s].x0 * glyph_width;
			vertices[vertex_count++] = -0.5f * glyph_height + segments[s].y0 * glyph_height;
			vertices[vertex_count++] = origin_x + segments[s].x1 * glyph_width;
			vertices[vertex_count++] = -0.5f * glyph_height + segments[s].y1 * glyph_height;
		}
	}
	host_log("guest_text_demo: built vertex buffer");

	vertex_shader = compile(GL_VERTEX_SHADER, kVertexSource);
	fragment_shader = compile(GL_FRAGMENT_SHADER, kFragmentSource);
	if (!vertex_shader || !fragment_shader)
		return;

	program = halo_glCreateProgram();
	halo_glAttachShader(program, vertex_shader);
	halo_glAttachShader(program, fragment_shader);
	halo_glLinkProgram(program);
	{
		GLint linked = 0;

		halo_glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			char log[512];
			GLint length = 0;

			halo_glGetProgramInfoLog(program, sizeof(log), &length, log);
			host_log("guest_text_demo: program link failed:");
			host_log(log);
			return;
		}
	}
	host_log("guest_text_demo: shader program linked");

	halo_glGenVertexArrays(1, &vao);
	halo_glBindVertexArray(vao);
	halo_glGenBuffers(1, &vbo);
	halo_glBindBuffer(GL_ARRAY_BUFFER, vbo);
	halo_glBufferData(GL_ARRAY_BUFFER, vertex_count * (int)sizeof(float), vertices, GL_STATIC_DRAW);
	halo_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
	halo_glEnableVertexAttribArray(0);

	halo_glUseProgram(program);
	scale_location = halo_glGetUniformLocation(program, "uScaleX");
	halo_glUniform1f(scale_location, (float)height / (float)width);
	halo_glLineWidth(3.0f);
	halo_glViewport(0, 0, width, height);

	host_log("guest_text_demo: entering present loop");
	for (frame = 0; frame < 3600; frame++)
	{
		platform_pump_events();
		halo_glClearColor(0.03f, 0.05f, 0.08f, 1.0f);
		halo_glClear(GL_COLOR_BUFFER_BIT);
		halo_glDrawArrays(GL_LINES, 0, vertex_count / 2);
		platform_video_swap();
	}
	host_log("guest_text_demo: done (3600 frames presented)");
}
