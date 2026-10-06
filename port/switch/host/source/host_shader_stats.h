#ifndef HOST_SHADER_STATS_H
#define HOST_SHADER_STATS_H

/*
 * Counts the shader work the game does, so the frame counter can show it.
 *
 * This is not a cache and holds no state of its own: every function here
 * forwards to real GLES3 and only bumps a counter. The three counters are
 * what host_fps_draw prints next to the frame rate, because a compile is
 * the one thing on this driver that visibly stalls a frame and there is
 * no way to see it from the log while it is happening.
 *
 * The guest cannot tell us itself. d3d8_gl.c's program_get() knows when it
 * links, but the host has no channel to be told, and reporting it would
 * mean rebuilding guest.elf; intercepting the two GL entry points is
 * cheaper and needs no guest change.
 */

/* compiles the game has issued, ever */
extern volatile unsigned long g_shader_compiles;
/* the ones it has since asked about, i.e. that the driver has finished. The
game asks via glGetShaderiv(GL_COMPILE_STATUS) exactly once per compile, so
started/finished converge to n/n when the driver is idle and show n/m while
it still has work outstanding */
extern volatile unsigned long g_shader_compiles_done;

/* programs linked whose compile and link took more than a cache hit does:
compiled fresh, the first-time stutter (host_shader_stats.c) */
extern volatile unsigned long g_shader_misses;
extern volatile unsigned long g_shader_programs;

/* logs the shader work since its last call, at most once a second */
void host_shader_stats_report(void);
/* programs linked, ever: the closest thing to a "total", since the game only
discovers a program exists when it first needs it */
extern volatile unsigned long g_shader_programs;

#endif