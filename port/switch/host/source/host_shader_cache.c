/*
HOST_SHADER_CACHE.C

The shader pack: programs compiled ahead of time on a core of their own.

Compiling a shader takes 10-40 ms here and linking 40-70 ms, so every new
effect's first appearance was a 60-110 ms hitch. This driver (Mesa 20.1,
nouveau) offers no program binaries, so compiled programs cannot be saved.
Their sources can: every program the game links is appended to
PACK_PATH (both GLSL sources and the attribute bindings), and at startup
a worker thread with a GL context of its own, sharing objects with the
game's, compiles and links every program in the pack on core 2, which
nothing else uses. When the game links one of them it gets the worker's
program at once instead of compiling. The pack holds sources, not
binaries, so it does not depend on the driver and can be shipped.

The game's program names are the host's (PROGRAM 1, 2, ...): the game
creates a program, attaches and binds, then links, and only at the link
is it known which real program it is. Every call that takes a program
name - glCreateProgram, glAttachShader, glBindAttribLocation,
glLinkProgram, glGetProgramiv, glGetProgramInfoLog, glUseProgram and
glGetUniformLocation - comes through here and is mapped. Compiles are
deferred: the game's own GL_COMPILE_STATUS check is answered "compiled",
and a shader is compiled only if its program is not in the pack.

nouveau still turns each program into machine code at its first draw, in
the game's context: the pack removes the compile and link, not that.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>

extern void logf_both(const char *fmt, ...);

#define PACK_PATH "sdmc:/haloce-nx/shader_pack.bin"
#define PACK_MAGIC 0x314b5053 /* "SPK1" */
#define MAXIMUM_TRACKED 8192
#define MAXIMUM_BINDINGS 16
#define MAXIMUM_PACKED 4096
#define PACK_TABLE_SIZE 8192 /* a power of two, at least twice MAXIMUM_PACKED */
#define WORKER_CORE 2
#define WORKER_PRIORITY 0x2D /* just below the game's threads */
#define WORKER_STACK_SIZE 0x200000 /* the GLSL compiler recurses deeply */

struct tracked_shader
{
	char *source;
	unsigned long long hash;
	int pending;
};

struct binding
{
	GLuint index;
	char *name;
};

struct tracked_program
{
	GLuint real;
	GLuint vertex_shader, fragment_shader;
	int binding_count;
	struct binding bindings[MAXIMUM_BINDINGS];
};

enum
{
	_packed_queued,
	_packed_busy, /* being compiled, by the worker or the game's thread */
	_packed_ready,
	_packed_failed,
	_packed_taken /* compiled by the game's thread for its own program */
};

struct packed_program
{
	unsigned long long key;
	char *vertex_source, *fragment_source;
	int binding_count;
	struct binding bindings[MAXIMUM_BINDINGS];
	volatile int state;
	GLuint real;
	/* the game program using it: one only, since the game keeps per-program
	uniform values (d3d8_gl.c) that a shared real program would mix */
	GLuint claimed_by;
};

static struct tracked_shader s_shaders[MAXIMUM_TRACKED];
static struct tracked_program s_programs[MAXIMUM_TRACKED];
static GLuint s_program_count;
static struct packed_program s_packed[MAXIMUM_PACKED];
static int s_packed_count;
static int s_packed_at_startup;
static short s_pack_table[PACK_TABLE_SIZE]; /* index + 1 into s_packed; 0 empty */
static unsigned s_from_pack, s_waited, s_built;
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

static unsigned long long program_key(const char *vertex_source, const char *fragment_source,
	const struct binding *bindings, int binding_count)
{
	unsigned long long key = 1469598103934665603ULL;
	int index;

	key = hash_bytes(key, vertex_source, strlen(vertex_source) + 1);
	key = hash_bytes(key, fragment_source, strlen(fragment_source) + 1);
	for (index = 0; index < binding_count; index++)
	{
		key = hash_bytes(key, &bindings[index].index, sizeof(bindings[index].index));
		key = hash_bytes(key, bindings[index].name, strlen(bindings[index].name) + 1);
	}
	return key;
}

/* ---------- the pack table (the game's thread inserts; the worker only
reads entries it was given at startup) */

static struct packed_program *pack_find(unsigned long long key)
{
	unsigned long slot = (unsigned long)key & (PACK_TABLE_SIZE - 1);

	while (s_pack_table[slot])
	{
		struct packed_program *packed = &s_packed[s_pack_table[slot] - 1];

		if (packed->key == key)
			return packed;
		slot = (slot + 1) & (PACK_TABLE_SIZE - 1);
	}
	return NULL;
}

static struct packed_program *pack_insert(unsigned long long key)
{
	unsigned long slot = (unsigned long)key & (PACK_TABLE_SIZE - 1);
	struct packed_program *packed;

	if (s_packed_count >= MAXIMUM_PACKED)
		return NULL;
	while (s_pack_table[slot])
		slot = (slot + 1) & (PACK_TABLE_SIZE - 1);
	packed = &s_packed[s_packed_count++];
	memset(packed, 0, sizeof(*packed));
	packed->key = key;
	s_pack_table[slot] = (short)s_packed_count;
	return packed;
}

/* ---------- the pack file */

static int read_u32(FILE *file, unsigned *value)
{
	return fread(value, sizeof(*value), 1, file) == 1;
}

static char *read_string(FILE *file)
{
	unsigned length;
	char *string;

	if (!read_u32(file, &length) || length > 0x100000 || !(string = malloc(length + 1)))
		return NULL;
	if (fread(string, 1, length, file) != length)
	{
		free(string);
		return NULL;
	}
	string[length] = 0;
	return string;
}

static void pack_load(void)
{
	FILE *file = fopen(PACK_PATH, "rb");
	unsigned magic;

	if (!file)
		return;
	if (!read_u32(file, &magic) || magic != PACK_MAGIC)
	{
		fclose(file);
		logf_both("shader pack: %s is not a pack; ignored\n", PACK_PATH);
		return;
	}
	for (;;)
	{
		struct binding bindings[MAXIMUM_BINDINGS];
		char *vertex_source = read_string(file), *fragment_source = NULL;
		unsigned binding_count = 0, index;
		int complete;
		struct packed_program *packed;

		if (!vertex_source)
			break;
		fragment_source = read_string(file);
		complete = fragment_source && read_u32(file, &binding_count) && binding_count <= MAXIMUM_BINDINGS;
		for (index = 0; complete && index < binding_count; index++)
		{
			complete = read_u32(file, &bindings[index].index) && (bindings[index].name = read_string(file));
			if (!complete)
				binding_count = index;
		}
		if (!complete)
		{
			/* a record cut short (the game closed mid-write): the rest is lost */
			free(vertex_source);
			free(fragment_source);
			for (index = 0; index < binding_count; index++)
				free(bindings[index].name);
			break;
		}
		{
			unsigned long long key = program_key(vertex_source, fragment_source, bindings, (int)binding_count);

			if (pack_find(key) || !(packed = pack_insert(key)))
			{
				free(vertex_source);
				free(fragment_source);
				for (index = 0; index < binding_count; index++)
					free(bindings[index].name);
				continue;
			}
		}
		packed->vertex_source = vertex_source;
		packed->fragment_source = fragment_source;
		packed->binding_count = (int)binding_count;
		memcpy(packed->bindings, bindings, sizeof(bindings[0]) * binding_count);
		packed->state = _packed_queued;
	}
	fclose(file);
}

static void write_string(FILE *file, const char *string)
{
	unsigned length = (unsigned)strlen(string);

	fwrite(&length, sizeof(length), 1, file);
	fwrite(string, 1, length, file);
}

static void pack_append(const struct packed_program *packed)
{
	FILE *file = fopen(PACK_PATH, "ab");
	unsigned count = (unsigned)packed->binding_count;
	int index;

	if (!file)
		return;
	if (!ftell(file))
	{
		unsigned magic = PACK_MAGIC;

		fwrite(&magic, sizeof(magic), 1, file);
	}
	write_string(file, packed->vertex_source);
	write_string(file, packed->fragment_source);
	fwrite(&count, sizeof(count), 1, file);
	for (index = 0; index < packed->binding_count; index++)
	{
		fwrite(&packed->bindings[index].index, sizeof(packed->bindings[index].index), 1, file);
		write_string(file, packed->bindings[index].name);
	}
	fclose(file);
}

/* ---------- compiling a packed program (either thread) */

static GLuint compile_shader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	return shader;
}

static GLuint build_packed(const struct packed_program *packed)
{
	GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, packed->vertex_source);
	GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, packed->fragment_source);
	GLuint program = glCreateProgram();
	GLint linked = 0;
	int index;

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	for (index = 0; index < packed->binding_count; index++)
		glBindAttribLocation(program, packed->bindings[index].index, packed->bindings[index].name);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	/* (flagged for deletion; they go with the program) */
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);
	if (linked != GL_TRUE)
	{
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

/* ---------- the worker */

static EGLDisplay s_worker_display;
static EGLContext s_worker_context;
static Thread s_worker;

static void worker_main(void *argument)
{
	u64 start = armGetSystemTick();
	int index, built = 0, failed = 0;

	(void)argument;
	if (!eglMakeCurrent(s_worker_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_worker_context))
	{
		logf_both("shader pack: the worker's context failed (0x%x); the game compiles as it goes\n", eglGetError());
		return;
	}
	for (index = 0; index < s_packed_at_startup; index++)
	{
		struct packed_program *packed = &s_packed[index];
		int expected = _packed_queued;
		GLuint program;

		/* (the game's thread may have taken it first) */
		if (!__atomic_compare_exchange_n(&packed->state, &expected, _packed_busy, 0, __ATOMIC_ACQUIRE,
			__ATOMIC_RELAXED))
		{
			continue;
		}
		program = build_packed(packed);
		/* finished before the game's context may use it */
		glFinish();
		packed->real = program;
		__atomic_store_n(&packed->state, program ? _packed_ready : _packed_failed, __ATOMIC_RELEASE);
		if (program)
			built++;
		else
			failed++;
	}
	logf_both("shader pack: worker compiled %d programs in %.0f ms on core %d (%d failed)\n", built,
		milliseconds_since(start), WORKER_CORE, failed);
	eglMakeCurrent(s_worker_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglReleaseThread();
}

/* host_video.c, once the game's context is current */
void host_shader_pack_start(EGLDisplay display, EGLConfig config, EGLContext game_context)
{
	static const EGLint context_attributes[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
		EGL_NONE,
	};
	const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
	Result result;

	pack_load();
	s_packed_at_startup = s_packed_count;
	logf_both("shader pack: %d programs in %s\n", s_packed_count, PACK_PATH);
	if (!s_packed_count)
		return;
	if (!extensions || !strstr(extensions, "EGL_KHR_surfaceless_context"))
	{
		logf_both("shader pack: no surfaceless contexts; the game compiles as it goes\n");
		return;
	}
	s_worker_display = display;
	s_worker_context = eglCreateContext(display, config, game_context, context_attributes);
	if (s_worker_context == EGL_NO_CONTEXT)
	{
		logf_both("shader pack: no shared context (0x%x); the game compiles as it goes\n", eglGetError());
		return;
	}
	result = threadCreate(&s_worker, worker_main, NULL, NULL, WORKER_STACK_SIZE, WORKER_PRIORITY, WORKER_CORE);
	if (R_SUCCEEDED(result))
		result = threadStart(&s_worker);
	if (R_FAILED(result))
		logf_both("shader pack: no worker thread (0x%x); the game compiles as it goes\n", result);
}

/* ---------- the game's calls */

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

static GLuint real_program(GLuint program)
{
	return program && program < MAXIMUM_TRACKED ? s_programs[program].real : 0;
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
	if (shader < MAXIMUM_TRACKED && s_shaders[shader].source)
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
	struct tracked_program *tracked;

	if (s_program_count + 1 >= MAXIMUM_TRACKED)
		return 0;
	tracked = &s_programs[++s_program_count];
	memset(tracked, 0, sizeof(*tracked));
	return s_program_count;
}

void hostgl_glAttachShader(GLuint program, GLuint shader)
{
	GLint type = 0;

	if (!program || program >= MAXIMUM_TRACKED)
		return;
	glGetShaderiv(shader, GL_SHADER_TYPE, &type);
	if (type == GL_VERTEX_SHADER)
		s_programs[program].vertex_shader = shader;
	else
		s_programs[program].fragment_shader = shader;
}

void hostgl_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	struct tracked_program *tracked;

	if (!program || program >= MAXIMUM_TRACKED)
		return;
	tracked = &s_programs[program];
	if (tracked->binding_count < MAXIMUM_BINDINGS)
	{
		tracked->bindings[tracked->binding_count].index = index;
		tracked->bindings[tracked->binding_count].name = strdup(name);
		tracked->binding_count++;
	}
}

/* compiled and linked here, on the game's thread, as before the pack */
static GLuint build_here(struct tracked_program *tracked)
{
	u64 start = armGetSystemTick();
	GLuint program = glCreateProgram();
	int index;

	compile_now(tracked->vertex_shader);
	compile_now(tracked->fragment_shader);
	glAttachShader(program, tracked->vertex_shader);
	glAttachShader(program, tracked->fragment_shader);
	for (index = 0; index < tracked->binding_count; index++)
		glBindAttribLocation(program, tracked->bindings[index].index, tracked->bindings[index].name);
	glLinkProgram(program);
	s_compile_ms += milliseconds_since(start);
	return program;
}

static void log_progress(void)
{
	unsigned total = s_from_pack + s_built;

	if (total <= 3 || total % 50 == 0)
		logf_both("shader pack: %u programs from the pack (%u waited for), %u compiled in game (%.0f ms)\n",
			s_from_pack, s_waited, s_built, s_compile_ms);
}

void hostgl_glLinkProgram(GLuint program)
{
	struct tracked_program *tracked;
	struct packed_program *packed;
	const char *vertex_source, *fragment_source;
	unsigned long long key;

	if (!program || program >= MAXIMUM_TRACKED)
		return;
	tracked = &s_programs[program];
	vertex_source = tracked->vertex_shader < MAXIMUM_TRACKED ? s_shaders[tracked->vertex_shader].source : NULL;
	fragment_source = tracked->fragment_shader < MAXIMUM_TRACKED ? s_shaders[tracked->fragment_shader].source : NULL;
	if (!vertex_source || !fragment_source)
	{
		tracked->real = build_here(tracked);
		return;
	}
	key = program_key(vertex_source, fragment_source, tracked->bindings, tracked->binding_count);
	packed = pack_find(key);
	if (packed && !packed->claimed_by)
	{
		int expected = _packed_queued;

		/* not started by the worker: compiled here instead of waited for */
		if (__atomic_compare_exchange_n(&packed->state, &expected, _packed_busy, 0, __ATOMIC_ACQUIRE,
			__ATOMIC_RELAXED))
		{
			tracked->real = build_here(tracked);
			packed->claimed_by = program;
			s_built++;
			__atomic_store_n(&packed->state, _packed_taken, __ATOMIC_RELEASE);
			log_progress();
			return;
		}
		if (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_busy)
		{
			s_waited++;
			while (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_busy)
				svcSleepThread(500000);
		}
		if (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_ready)
		{
			tracked->real = packed->real;
			packed->claimed_by = program;
			s_from_pack++;
			log_progress();
			return;
		}
	}
	tracked->real = build_here(tracked);
	s_built++;
	if (!packed && (packed = pack_insert(key)))
	{
		/* new: into the pack, for the next start */
		packed->vertex_source = strdup(vertex_source);
		packed->fragment_source = strdup(fragment_source);
		packed->binding_count = tracked->binding_count;
		for (int index = 0; index < tracked->binding_count; index++)
		{
			packed->bindings[index].index = tracked->bindings[index].index;
			packed->bindings[index].name = strdup(tracked->bindings[index].name);
		}
		packed->claimed_by = program;
		packed->state = _packed_taken; /* (the worker only takes the entries it started with) */
		if (packed->vertex_source && packed->fragment_source)
			pack_append(packed);
	}
	log_progress();
}

void hostgl_glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
	glGetProgramiv(real_program(program), pname, params);
}

void hostgl_glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *log)
{
	glGetProgramInfoLog(real_program(program), size, length, log);
}

void hostgl_glUseProgram(GLuint program)
{
	glUseProgram(real_program(program));
}

GLint hostgl_glGetUniformLocation(GLuint program, const GLchar *name)
{
	return glGetUniformLocation(real_program(program), name);
}
