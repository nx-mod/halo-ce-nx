/*
 * HOST_SHADER_STATS.C
 *
 * The GL entry points this host hand-writes: what the game's shader work
 * costs, and a record of every program it links.
 *
 * hostgl_glShaderSource is also an ABI adapter: guest_gl.c truncates each
 * string pointer to 32 bits before widening it into a 64-bit slot, so the
 * host has to unpack the array itself before real GLES3 can read it.
 *
 * Costs: compile, link and status calls are timed, and a program whose
 * work took more than HIT_MS is a miss - the driver really compiled it.
 * Under nxvk (Zink, Mesa's shader disk cache) a program met before comes
 * back from sdmc:/haloce-nx/mesa_shader_cache in about a millisecond; a
 * miss is tens, and is the first-time stutter. host_fps_draw shows linked
 * programs and misses.
 *
 * The record: every program linked, once - its two shaders' sources and
 * where its attributes were bound - appended to RECORD_PATH. The sources
 * are text the renderer generates, the same on every console, so the file
 * describes everything the game has drawn with: what a warm-up can compile
 * into the cache before the game needs it, on this console or any other.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <GLES3/gl32.h>

#include "host_shader_stats.h"

extern void logf_both(const char *fmt, ...);

#define RECORD_PATH "sdmc:/haloce-nx/shader_programs.bin"
#define RECORD_MAGIC 0x31475053 /* "SPG1" */
#define HIT_MS 4.0
#define MAXIMUM_SHADERS 8192
#define MAXIMUM_RECORDED 8192
#define MAXIMUM_ATTRIBUTES 16

volatile unsigned long g_shader_compiles;
volatile unsigned long g_shader_compiles_done;
volatile unsigned long g_shader_programs;
volatile unsigned long g_shader_misses;

/* shader work since the last report, in ticks */
static u64 s_pending_ticks;
static unsigned long s_pending_programs, s_pending_misses;
static u64 s_last_report;

/* per shader name: its source's hash and text (for the record), and the
compile ticks not yet charged to a program */
static struct
{
	unsigned long long hash;
	char *source;
	u64 ticks;
} s_shaders[MAXIMUM_SHADERS];

static unsigned long long s_recorded[MAXIMUM_RECORDED];
static int s_recorded_count = -1; /* -1 until the file is read */

static unsigned long long hash_bytes(unsigned long long hash, const void *data, size_t size)
{
	const unsigned char *bytes = data;

	while (size--)
	{
		hash ^= *bytes++;
		hash *= 1099511628211ULL;
	}
	return hash;
}

static double ticks_ms(u64 ticks)
{
	return (double)armTicksToNs(ticks) / 1000000.0;
}

/* ---------- the record */

static int recorded(unsigned long long key)
{
	int index;

	for (index = 0; index < s_recorded_count; index++)
	{
		if (s_recorded[index] == key)
			return 1;
	}
	return 0;
}

static int read_u32(FILE *file, unsigned *value)
{
	return fread(value, sizeof(*value), 1, file) == 1;
}

static int skip_string(FILE *file)
{
	unsigned length;

	return read_u32(file, &length) && length < 0x100000 && fseek(file, (long)length, SEEK_CUR) == 0;
}

/* each record's key is stored ahead of it, so this reads only the keys */
static void record_load(void)
{
	FILE *file = fopen(RECORD_PATH, "rb");
	unsigned magic;

	s_recorded_count = 0;
	if (!file)
		return;
	if (read_u32(file, &magic) && magic == RECORD_MAGIC)
	{
		unsigned long long key;
		unsigned attributes, index;

		while (s_recorded_count < MAXIMUM_RECORDED && fread(&key, sizeof(key), 1, file) == 1)
		{
			if (!skip_string(file) || !skip_string(file) || !read_u32(file, &attributes) ||
				attributes > MAXIMUM_ATTRIBUTES)
			{
				break;
			}
			for (index = 0; index < attributes; index++)
			{
				unsigned location;

				if (!read_u32(file, &location) || !skip_string(file))
					break;
			}
			if (index < attributes)
				break;
			s_recorded[s_recorded_count++] = key;
		}
	}
	fclose(file);
	logf_both("shader record: %d programs in %s\n", s_recorded_count, RECORD_PATH);
}

static void write_string(FILE *file, const char *text)
{
	unsigned length = (unsigned)strlen(text);

	fwrite(&length, sizeof(length), 1, file);
	fwrite(text, 1, length, file);
}

/* program's two shaders and its attribute bindings, appended if new */
static void record_program(GLuint program)
{
	GLuint attached[2];
	GLsizei attached_count = 0;
	const char *vertex = NULL, *fragment = NULL;
	char names[MAXIMUM_ATTRIBUTES][64];
	GLint locations[MAXIMUM_ATTRIBUTES];
	GLint attribute_count = 0;
	unsigned long long key = 1469598103934665603ULL;
	int index, kept = 0;
	FILE *file;

	if (s_recorded_count < 0)
		record_load();
	glGetAttachedShaders(program, 2, &attached_count, attached);
	for (index = 0; index < attached_count; index++)
	{
		GLint type = 0;

		if (attached[index] >= MAXIMUM_SHADERS || !s_shaders[attached[index]].source)
			return;
		glGetShaderiv(attached[index], GL_SHADER_TYPE, &type);
		if (type == GL_VERTEX_SHADER)
			vertex = s_shaders[attached[index]].source;
		else if (type == GL_FRAGMENT_SHADER)
			fragment = s_shaders[attached[index]].source;
	}
	if (!vertex || !fragment)
		return;
	/* where the game bound each attribute (glBindAttribLocation before the
	link): the program's active attributes and their locations now */
	glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attribute_count);
	for (index = 0; index < attribute_count && kept < MAXIMUM_ATTRIBUTES; index++)
	{
		GLint size;
		GLenum type;

		glGetActiveAttrib(program, (GLuint)index, sizeof(names[kept]), NULL, &size, &type, names[kept]);
		locations[kept] = glGetAttribLocation(program, names[kept]);
		if (locations[kept] >= 0)
			kept++;
	}
	key = hash_bytes(key, vertex, strlen(vertex) + 1);
	key = hash_bytes(key, fragment, strlen(fragment) + 1);
	for (index = 0; index < kept; index++)
	{
		key = hash_bytes(key, &locations[index], sizeof(locations[index]));
		key = hash_bytes(key, names[index], strlen(names[index]) + 1);
	}
	if (recorded(key) || s_recorded_count >= MAXIMUM_RECORDED)
		return;
	file = fopen(RECORD_PATH, "ab");
	if (!file)
		return;
	if (!ftell(file))
	{
		unsigned magic = RECORD_MAGIC;

		fwrite(&magic, sizeof(magic), 1, file);
	}
	fwrite(&key, sizeof(key), 1, file);
	write_string(file, vertex);
	write_string(file, fragment);
	{
		unsigned count = (unsigned)kept;

		fwrite(&count, sizeof(count), 1, file);
	}
	for (index = 0; index < kept; index++)
	{
		unsigned location = (unsigned)locations[index];

		fwrite(&location, sizeof(location), 1, file);
		write_string(file, names[index]);
	}
	fclose(file);
	s_recorded[s_recorded_count++] = key;
}

/* ---------- the game's calls */

void hostgl_glShaderSource(GLuint shader, GLsizei count, const unsigned long long *strings, const GLint *lengths)
{
	const char *pointers[16];
	size_t total = 0;
	GLsizei index;

	if (count > 16)
		count = 16;
	for (index = 0; index < count; index++)
	{
		pointers[index] = (const char *)(unsigned long)strings[index];
		total += lengths && lengths[index] >= 0 ? (size_t)lengths[index] : strlen(pointers[index]);
	}
	glShaderSource(shader, count, pointers, lengths);
	if (shader < MAXIMUM_SHADERS)
	{
		char *text = malloc(total + 1), *at = text;

		if (!text)
			return;
		for (index = 0; index < count; index++)
		{
			size_t length = lengths && lengths[index] >= 0 ? (size_t)lengths[index] : strlen(pointers[index]);

			memcpy(at, pointers[index], length);
			at += length;
		}
		*at = 0;
		free(s_shaders[shader].source);
		s_shaders[shader].source = text;
		s_shaders[shader].hash = hash_bytes(1469598103934665603ULL, text, total);
		s_shaders[shader].ticks = 0;
	}
}

void hostgl_glCompileShader(GLuint shader)
{
	u64 start = armGetSystemTick();

	g_shader_compiles++;
	glCompileShader(shader);
	if (shader < MAXIMUM_SHADERS)
		s_shaders[shader].ticks += armGetSystemTick() - start;
}

void hostgl_glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	u64 start = armGetSystemTick();

	/* the game asking for the status is the compile finishing, as far as we
	are concerned: it asks exactly once per compile */
	if (pname == GL_COMPILE_STATUS)
		g_shader_compiles_done++;
	glGetShaderiv(shader, pname, params);
	/* (a driver that compiles lazily does it here) */
	if (pname == GL_COMPILE_STATUS && shader < MAXIMUM_SHADERS)
		s_shaders[shader].ticks += armGetSystemTick() - start;
}

void hostgl_glLinkProgram(GLuint program)
{
	u64 start = armGetSystemTick(), ticks;
	GLuint attached[2];
	GLsizei attached_count = 0;
	int index;

	g_shader_programs++;
	glLinkProgram(program);
	ticks = armGetSystemTick() - start;
	/* the program's whole cost: its shaders' compiles and its link */
	glGetAttachedShaders(program, 2, &attached_count, attached);
	for (index = 0; index < attached_count; index++)
	{
		if (attached[index] < MAXIMUM_SHADERS)
		{
			ticks += s_shaders[attached[index]].ticks;
			s_shaders[attached[index]].ticks = 0;
		}
	}
	s_pending_ticks += ticks;
	s_pending_programs++;
	if (ticks_ms(ticks) > HIT_MS)
	{
		g_shader_misses++;
		s_pending_misses++;
	}
	record_program(program);
}

/* host_fps_draw, about once a second: the shader work since the last call */
void host_shader_stats_report(void)
{
	u64 now = armGetSystemTick();

	if (s_pending_programs && ticks_ms(now - s_last_report) >= 1000.0)
	{
		logf_both("shaders: %lu linked, %lu compiled fresh, %.1f ms (%lu linked, %lu fresh in all)\n",
			s_pending_programs, s_pending_misses, ticks_ms(s_pending_ticks), g_shader_programs, g_shader_misses);
		s_pending_programs = s_pending_misses = 0;
		s_pending_ticks = 0;
		s_last_report = now;
	}
}
