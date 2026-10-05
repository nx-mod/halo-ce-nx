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

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

static GLuint create_passthrough_program(void);

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
	int compiled; /* a real glCompileShader has been made against this object */
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
	int passthrough; /* the name is the driver's own, not one of ours */
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

	while (__atomic_load_n(&s_pack_table[slot], __ATOMIC_ACQUIRE))
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
	/* the game links from more than one thread, so the slot has to be ours
	alone - two threads taking the same index would clobber each other's
	entry, and the loser's shaders would be silently used for its program */
	int index = __atomic_add_fetch(&s_packed_count, 1, __ATOMIC_ACQ_REL);

	if (index >= MAXIMUM_PACKED)
	{
		__atomic_store_n(&s_packed_count, MAXIMUM_PACKED, __ATOMIC_RELAXED);
		return NULL;
	}
	packed = &s_packed[index];
	memset(packed, 0, sizeof(*packed));
	packed->key = key;
	while (__atomic_load_n(&s_pack_table[slot], __ATOMIC_RELAXED))
		slot = (slot + 1) & (PACK_TABLE_SIZE - 1);
	__atomic_store_n(&s_pack_table[slot], (short)(index + 1), __ATOMIC_RELEASE);
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
	GLint compiled = GL_FALSE;

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled != GL_TRUE)
	{
		char log[1024];

		log[0] = 0;
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		logf_both("shader pack: a packed %s shader does not compile here: %s\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		return 0;
	}
	return shader;
}

static GLuint build_packed(const struct packed_program *packed)
{
	GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, packed->vertex_source);
	GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, packed->fragment_source);
	GLuint program;
	GLint linked = 0;
	int index;

	if (!vertex_shader || !fragment_shader)
	{
		if (vertex_shader)
			glDeleteShader(vertex_shader);
		if (fragment_shader)
			glDeleteShader(fragment_shader);
		return 0;
	}
	program = glCreateProgram();
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

/* Whether a program built in the worker's context can be used in the game's.
 * This has to be settled before anything is handed over, because it cannot be
 * settled per program: the driver's program names are a single counter shared
 * between the two contexts (the numbers in the old log stepped by three and ran
 * past the worker's whole output), so a name from here is also the name of
 * whatever the game builds next. Probing with glGetProgramiv therefore only
 * ever proves that *something* with that number is linked - often an
 * unrelated program the game just built, which is how the menu ended up
 * rendering other shaders' geometry. So the probe asks about a uniform that
 * only exists in the probe program, which a same-numbered stranger cannot
 * answer for.
 */
#define PROBE_UNIFORM "shaman_share_probe"
#define PROBE_WAIT_MS 5000
static const char *const PROBE_VERTEX =
	"#version 300 es\nvoid main() { gl_Position = vec4(0.0); }\n";
static const char *const PROBE_FRAGMENT =
	"#version 300 es\nprecision mediump float;\nuniform float " PROBE_UNIFORM ";\n"
	"out vec4 colour;\nvoid main() { colour = vec4(" PROBE_UNIFORM "); }\n";
static EGLDisplay s_worker_display;
static EGLContext s_worker_context;
static GLuint s_probe_program;
static Thread s_worker;
static volatile int s_probe_result; /* 0 untried, 1 built, 2 shared, -1 unusable */

static void worker_main(void *argument)
{
	u64 start = armGetSystemTick();
	int index, built = 0, failed = 0;

	(void)argument;
	if (!eglMakeCurrent(s_worker_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_worker_context))
	{
		__atomic_store_n(&s_probe_result, -1, __ATOMIC_RELEASE);
		logf_both("shader pack: the worker's context failed (0x%x); the game compiles as it goes\n", eglGetError());
		return;
	}

	/* one program first: does anything built here survive in the game's context? */
	{
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, PROBE_VERTEX);
		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, PROBE_FRAGMENT);
		GLuint program = 0;
		GLint linked = GL_FALSE;

		if (vertex_shader && fragment_shader)
		{
			program = glCreateProgram();
			glAttachShader(program, vertex_shader);
			glAttachShader(program, fragment_shader);
			glLinkProgram(program);
			glGetProgramiv(program, GL_LINK_STATUS, &linked);
		}
		if (vertex_shader)
			glDeleteShader(vertex_shader);
		if (fragment_shader)
			glDeleteShader(fragment_shader);
		if (!program || linked != GL_TRUE)
		{
			__atomic_store_n(&s_probe_result, -1, __ATOMIC_RELEASE);
			logf_both("shader pack: the probe program did not build (0x%x); the game compiles as it goes\n",
				glGetError());
			return;
		}
		glFinish(); /* the game's context has to be able to see it before we ask */
		s_probe_program = program;
		/* 1 = the probe is built and waiting for the game's verdict */
		__atomic_store_n(&s_probe_result, 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&s_probe_result, __ATOMIC_ACQUIRE) == 1)
			svcSleepThread(1000);
		if (__atomic_load_n(&s_probe_result, __ATOMIC_ACQUIRE) != 2)
			return; /* (the game's side already logged why) */
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
	{
		logf_both("shader pack: no worker thread (0x%x); the game compiles as it goes\n", result);
		return;
	}

	/* the game's context is current here, so this is the one chance to find out
	whether a program the worker built is reachable from it. Ask about the
	probe's own uniform: if the name belongs to a stranger, that stranger has no
	such uniform and we get -1 back. */
	{
		u64 start = armGetSystemTick();
		GLint location = -1;

		while (__atomic_load_n(&s_probe_result, __ATOMIC_ACQUIRE) == 0 &&
			milliseconds_since(start) < PROBE_WAIT_MS)
		{
			svcSleepThread(1000);
		}
		if (__atomic_load_n(&s_probe_result, __ATOMIC_ACQUIRE) == 1)
		{
			while (glGetError() != GL_NO_ERROR)
				;
			location = glGetUniformLocation(s_probe_program, PROBE_UNIFORM);
			while (glGetError() != GL_NO_ERROR)
				;
			/* (no glDeleteProgram: on a driver that does not share, that number
			is one of the game's own programs, and deleting it would be the very
			bug the probe exists to catch) */
		}
		if (location >= 0)
		{
			__atomic_store_n(&s_probe_result, 2, __ATOMIC_RELEASE);
			logf_both("shader pack: programs are shared; precompiling %d\n", s_packed_at_startup);
		}
		else
		{
			__atomic_store_n(&s_probe_result, -1, __ATOMIC_RELEASE);
			logf_both("shader pack: this driver does not share programs between contexts, so the"
				" %d in %s cannot be used; the game compiles as it goes\n",
				s_packed_at_startup, PACK_PATH);
			/* nothing will ever claim these, so retire them now: the game's
			link path skips a failed entry outright and builds in place,
			rather than queueing for a worker that is not coming */
			for (int index = 0; index < s_packed_at_startup; index++)
				__atomic_store_n(&s_packed[index].state, _packed_failed, __ATOMIC_RELEASE);
		}
	}
}

/* ---------- the game's calls */

/* a deferred shader is compiled here instead: at its program's link, or when
someone asks it a question the deferral cannot answer. `forced` also compiles a
shader whose deferral was cleared without a compile following - linking a shader
object that was never compiled fails with an empty log, which reads as a shader
bug when it is a bookkeeping one. */
static void compile_now(GLuint shader, int forced)
{
	u64 start;

	if (shader >= MAXIMUM_TRACKED || s_shaders[shader].compiled)
		return;
	if (!s_shaders[shader].pending && !forced)
		return;
	s_shaders[shader].pending = 0;
	s_shaders[shader].compiled = 1;
	start = armGetSystemTick();
	glCompileShader(shader);
	s_compile_ms += milliseconds_since(start);
}

static GLuint real_program(GLuint program)
{
	return program && program < MAXIMUM_TRACKED ? s_programs[program].real : 0;
}

/* ---------- taking a program the worker built

The worker links in a context of its own, so its program objects only reach
the game through the share list - and that is the one thing here that cannot
be taken on trust: the name may name nothing at all in the game's context, in
which case every call the game makes with it raises GL_INVALID_VALUE and the
menu renders nothing. So every handed-over name is asked about here, in the
game's own context, and anything that does not answer is dropped for the game
to rebuild. Asking costs a query and an error drain, not a compile.

The drain is deliberate: a name that has gone away leaves an error behind, and
the game reads glGetError per frame, so it must not be allowed to see this one.
 */

static GLuint adopt(GLuint candidate)
{
	GLint linked = GL_FALSE;

	if (!candidate)
		return 0;
	while (glGetError() != GL_NO_ERROR)
		;
	glGetProgramiv(candidate, GL_LINK_STATUS, &linked);
	while (glGetError() != GL_NO_ERROR)
		;
	if (linked != GL_TRUE)
	{
		logf_both("shader pack: program %u did not survive into the game's context; rebuilt\n", candidate);
		return 0;
	}
	return candidate;
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
		tracked->compiled = 0;
	}
}

void hostgl_glCompileShader(GLuint shader)
{
	if (shader < MAXIMUM_TRACKED && s_shaders[shader].source && !s_shaders[shader].compiled)
	{
		s_shaders[shader].pending = 1;
		return;
	}
	{
		u64 start = armGetSystemTick();

		glCompileShader(shader);
		if (shader < MAXIMUM_TRACKED)
			s_shaders[shader].compiled = 1;
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
		compile_now(shader, FALSE);
	}
	glGetShaderiv(shader, pname, params);
}

void hostgl_glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log)
{
	compile_now(shader, FALSE);
	glGetShaderInfoLog(shader, size, length, log);
}

/* The game builds programs from more than one thread, so the name is ours to
hand out atomically: two threads sharing one name would share one record, and
whichever linked last would overwrite the other's shaders. The driver used to
give us that atomicity for free, by naming the programs itself. */
GLuint hostgl_glCreateProgram(void)
{
	struct tracked_program *tracked;
	GLuint name = __atomic_add_fetch(&s_program_count, 1, __ATOMIC_ACQ_REL);

	if (name >= MAXIMUM_TRACKED)
	{
		__atomic_store_n(&s_program_count, MAXIMUM_TRACKED - 1, __ATOMIC_RELAXED);
		logf_both("shader pack: out of program names; the game compiles as it goes\n");
		return create_passthrough_program();
	}
	tracked = &s_programs[name];
	memset(tracked, 0, sizeof(*tracked));
	return name;
}

/* the table is full, or the game asked for more programs than we can name:
hand the driver's own name back and let every program call pass straight
through, which is what this file did before the pack existed */
static GLuint create_passthrough_program(void)
{
	GLuint program = glCreateProgram();

	if (!program || program >= MAXIMUM_TRACKED)
		return program;
	memset(&s_programs[program], 0, sizeof(s_programs[program]));
	s_programs[program].real = program;
	s_programs[program].passthrough = TRUE;
	return program;
}

void hostgl_glAttachShader(GLuint program, GLuint shader)
{
	GLint type = 0;

	if (!program || program >= MAXIMUM_TRACKED)
	{
		glAttachShader(program, shader);
		return;
	}
	if (s_programs[program].passthrough)
	{
		glAttachShader(program, shader);
		return;
	}
	/* the shader is only compiled when its program is built, so asking its
	type here is the one thing that may still force a compile early */
	compile_now(shader, FALSE);
	glGetShaderiv(shader, GL_SHADER_TYPE, &type);
	if (type == GL_VERTEX_SHADER)
		s_programs[program].vertex_shader = shader;
	else
		s_programs[program].fragment_shader = shader;
}

void hostgl_glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	struct tracked_program *tracked;

	if (!program || program >= MAXIMUM_TRACKED || s_programs[program].passthrough)
	{
		glBindAttribLocation(program, index, name);
		return;
	}
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

	compile_now(tracked->vertex_shader, TRUE);
	compile_now(tracked->fragment_shader, TRUE);
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
	unsigned from_pack = __atomic_load_n(&s_from_pack, __ATOMIC_RELAXED);
	unsigned built = __atomic_load_n(&s_built, __ATOMIC_RELAXED);
	unsigned total = from_pack + built;

	if (total <= 3 || total % 50 == 0)
		logf_both("shader pack: %u programs from the pack (%u waited for), %u compiled in game (%.0f ms)\n",
			from_pack, __atomic_load_n(&s_waited, __ATOMIC_RELAXED), built, s_compile_ms);
}

void hostgl_glLinkProgram(GLuint program)
{
	struct tracked_program *tracked;
	struct packed_program *packed;
	const char *vertex_source, *fragment_source;
	unsigned long long key;

	if (!program || program >= MAXIMUM_TRACKED)
	{
		glLinkProgram(program);
		return;
	}
	if (s_programs[program].passthrough)
	{
		glLinkProgram(program);
		return;
	}
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
			__atomic_add_fetch(&s_built, 1, __ATOMIC_RELAXED);
			__atomic_store_n(&packed->state, tracked->real ? _packed_taken : _packed_failed, __ATOMIC_RELEASE);
			log_progress();
			return;
		}
		if (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_busy)
		{
			__atomic_add_fetch(&s_waited, 1, __ATOMIC_RELAXED);
			while (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_busy)
				svcSleepThread(500000);
		}
		if (__atomic_load_n(&packed->state, __ATOMIC_ACQUIRE) == _packed_ready)
		{
			/* (the worker's context has to have shared its programs with ours;
			adopt() finds out whether it did) */
			tracked->real = adopt(packed->real);
			if (tracked->real)
			{
				packed->claimed_by = program;
				__atomic_add_fetch(&s_from_pack, 1, __ATOMIC_RELAXED);
				log_progress();
				return;
			}
			logf_both("shader pack: program %u from the pack is not usable here; the game compiles as it goes\n",
				program);
			tracked->real = build_here(tracked);
			__atomic_add_fetch(&s_built, 1, __ATOMIC_RELAXED);
			log_progress();
			return;
		}
	}
	tracked->real = build_here(tracked);
	__atomic_add_fetch(&s_built, 1, __ATOMIC_RELAXED);
	if (!tracked->real)
		logf_both("shader pack: could not build program %u (%u, %u)\n", program, tracked->vertex_shader,
			tracked->fragment_shader);
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
