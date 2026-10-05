/*
HOST_SHADER_CACHE.C

A shader program cache on the SD card. Compiling a shader takes 10-40 ms
here and linking 40-70 ms, so every new effect's first appearance was a
60-110 ms hitch (measured: host_gl_resolve.c's old timing wrappers).

The guest's shader calls (glShaderSource, glCompileShader, glLinkProgram
and the queries around them) come through these wrappers. A compile is
deferred: the guest's own GL_COMPILE_STATUS check is answered "compiled"
until something needs the real result. At link, the program's key (both
sources, the attribute bindings and the driver's version) names a file
in CACHE_DIRECTORY; if it is there its binary is loaded with
glProgramBinary and nothing is compiled or linked. Otherwise the pending
shaders are compiled, the program linked, and its binary saved.

Every Switch runs the same GPU and this NRO's statically linked Mesa, so
a binary made on one console is valid on every other: the directory can
be shipped as a prebuilt pack.

If the driver offers no binary formats, everything is compiled and
linked as before.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>
#include <GLES3/gl32.h>

extern void logf_both(const char *fmt, ...);

#define CACHE_DIRECTORY "sdmc:/haloce-nx/shader_cache"
#define MAXIMUM_TRACKED 8192

struct tracked_shader
{
	char *source;
	unsigned long long hash;
	int pending;
};

struct tracked_program
{
	GLuint shaders[2];
	unsigned long long binding_hash;
};

static struct tracked_shader s_shaders[MAXIMUM_TRACKED];
static struct tracked_program s_programs[MAXIMUM_TRACKED];
static int s_enabled = -1; /* -1 until the first link asks the driver */
static unsigned long long s_driver_hash;
static unsigned s_hits, s_misses;
static double s_compile_ms;

static unsigned long long hash_bytes(unsigned long long hash, const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0; index < size; index++)
	{
		hash ^= bytes[index];
		hash *= 1099511628211ULL;
	}
	return hash;
}

static double milliseconds_since(u64 start)
{
	return (double)armTicksToNs(armGetSystemTick() - start) / 1000000.0;
}

static int cache_enabled(void)
{
	if (s_enabled < 0)
	{
		GLint formats = 0;
		const char *version = (const char *)glGetString(GL_VERSION);
		const char *renderer = (const char *)glGetString(GL_RENDERER);

		glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
		s_driver_hash = 1469598103934665603ULL;
		if (version)
			s_driver_hash = hash_bytes(s_driver_hash, version, strlen(version));
		if (renderer)
			s_driver_hash = hash_bytes(s_driver_hash, renderer, strlen(renderer));
		s_enabled = formats > 0;
		if (s_enabled)
			mkdir(CACHE_DIRECTORY, 0777);
		logf_both("shader cache: %s (%d binary formats; %s / %s)\n", s_enabled ? "on" : "off - no binary formats",
			(int)formats, renderer ? renderer : "?", version ? version : "?");
	}
	return s_enabled;
}

static void compile_now(GLuint shader)
{
	u64 start;

	if (shader >= MAXIMUM_TRACKED || !s_shaders[shader].pending)
		return;
	s_shaders[shader].pending = 0;
	start = armGetSystemTick();
	glCompileShader(shader);
	s_compile_ms += milliseconds_since(start);
}

/* glShaderSource's generated guest wrapper (tools/android_gl_stubs.py)
widens each string pointer into a fixed array of 64-bit slots */
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
	if (shader < MAXIMUM_TRACKED)
	{
		struct tracked_shader *tracked = &s_shaders[shader];
		char *at;

		free(tracked->source);
		tracked->source = malloc(total + 1);
		tracked->pending = 0;
		if (!tracked->source)
			return;
		at = tracked->source;
		for (index = 0; index < count; index++)
		{
			size_t length = lengths && lengths[index] >= 0 ? (size_t)lengths[index] : strlen(pointers[index]);

			memcpy(at, pointers[index], length);
			at += length;
		}
		*at = 0;
		tracked->hash = hash_bytes(1469598103934665603ULL, tracked->source, total);
	}
}

void hostgl_glCompileShader(GLuint shader)
{
	if (cache_enabled() && shader < MAXIMUM_TRACKED && s_shaders[shader].source)
	{
		s_shaders[shader].pending = 1;
		return;
	}
	{
		u64 start = armGetSystemTick();

		glCompileShader(shader);
		s_compile_ms += milliseconds_since(start);
	}
}

void hostgl_glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	/* a deferred compile counts as a successful one until something needs
	the real answer; a shader that really fails shows at link */
	if (shader < MAXIMUM_TRACKED && s_shaders[shader].pending)
	{
		if (pname == GL_COMPILE_STATUS)
		{
			*params = GL_TRUE;
			return;
		}
		if (pname == GL_INFO_LOG_LENGTH)
		{
			*params = 0;
			return;
		}
		compile_now(shader);
	}
	glGetShaderiv(shader, pname, params);
}

void hostgl_glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log)
{
	compile_now(shader);
	glGetShaderInfoLog(shader, size, length, log);
}

GLuint hostgl_glCreateProgram(void)
{
	GLuint program = glCreateProgram();

	/* names are reused after a delete: start this one's record clean */
	if (program < MAXIMUM_TRACKED)
		memset(&s_programs[program], 0, sizeof(s_programs[program]));
	return program;
}

void hostgl_glAttachShader(GLuint program, GLuint shader)
{
	if (program < MAXIMUM_TRACKED)
	{
		struct tracked_program *tracked = &s_programs[program];

		if (!tracked->shaders[0])
			tracked->shaders[0] = shader;
		else
			tracked->shaders[1] = shader;
	}
	glAttachShader(program, shader);
}

void hostgl_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	if (program < MAXIMUM_TRACKED)
	{
		s_programs[program].binding_hash = hash_bytes(s_programs[program].binding_hash + index, name, strlen(name));
	}
	glBindAttribLocation(program, index, name);
}

static int program_key(GLuint program, unsigned long long *key)
{
	struct tracked_program *tracked;
	unsigned long long hash = s_driver_hash;
	int index;

	if (program >= MAXIMUM_TRACKED)
		return 0;
	tracked = &s_programs[program];
	for (index = 0; index < 2; index++)
	{
		GLuint shader = tracked->shaders[index];

		if (!shader || shader >= MAXIMUM_TRACKED || !s_shaders[shader].source)
			return 0;
		hash = hash_bytes(hash, &s_shaders[shader].hash, sizeof(s_shaders[shader].hash));
	}
	hash = hash_bytes(hash, &tracked->binding_hash, sizeof(tracked->binding_hash));
	*key = hash;
	return 1;
}

static int load_binary(GLuint program, const char *path)
{
	FILE *file = fopen(path, "rb");
	GLenum format;
	long size;
	void *data;
	GLint linked = 0;

	if (!file)
		return 0;
	if (fread(&format, sizeof(format), 1, file) != 1 || fseek(file, 0, SEEK_END) ||
		(size = ftell(file) - (long)sizeof(format)) <= 0 || fseek(file, (long)sizeof(format), SEEK_SET))
	{
		fclose(file);
		return 0;
	}
	data = malloc((size_t)size);
	if (!data || fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		free(data);
		fclose(file);
		return 0;
	}
	fclose(file);
	glProgramBinary(program, format, data, (GLsizei)size);
	free(data);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	return linked == GL_TRUE;
}

static void save_binary(GLuint program, const char *path)
{
	GLint size = 0;
	GLenum format = 0;
	void *data;
	FILE *file;

	glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &size);
	if (size <= 0 || !(data = malloc((size_t)size)))
		return;
	glGetProgramBinary(program, size, &size, &format, data);
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(&format, sizeof(format), 1, file);
		fwrite(data, 1, (size_t)size, file);
		fclose(file);
	}
	free(data);
}

void hostgl_glLinkProgram(GLuint program)
{
	unsigned long long key;
	char path[96];
	u64 start = armGetSystemTick();

	if (cache_enabled() && program_key(program, &key))
	{
		snprintf(path, sizeof(path), CACHE_DIRECTORY "/%016llx.bin", key);
		if (load_binary(program, path))
		{
			s_hits++;
			if (s_hits <= 3 || s_hits % 50 == 0)
				logf_both("shader cache: %u loaded, %u built (%.0f ms compiling and linking so far)\n", s_hits,
					s_misses, s_compile_ms);
			return;
		}
		s_misses++;
		compile_now(s_programs[program].shaders[0]);
		compile_now(s_programs[program].shaders[1]);
		glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		glLinkProgram(program);
		s_compile_ms += milliseconds_since(start);
		{
			GLint linked = 0;

			glGetProgramiv(program, GL_LINK_STATUS, &linked);
			if (linked == GL_TRUE)
				save_binary(program, path);
		}
		if (s_misses <= 3 || s_misses % 50 == 0)
			logf_both("shader cache: %u loaded, %u built (%.0f ms compiling and linking so far)\n", s_hits, s_misses,
				s_compile_ms);
		return;
	}
	if (program < MAXIMUM_TRACKED)
	{
		compile_now(s_programs[program].shaders[0]);
		compile_now(s_programs[program].shaders[1]);
	}
	glLinkProgram(program);
	s_compile_ms += milliseconds_since(start);
}
