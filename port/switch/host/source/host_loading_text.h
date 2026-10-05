#ifndef HOST_LOADING_TEXT_H
#define HOST_LOADING_TEXT_H

void host_loading_text_draw(int width, int height);
/* the guest's main loop has started (main.c): no more loading text */
void host_loading_text_stop(void);
/* centered at the top of the screen, every frame; independent of the
loading text above, which cancels itself */
void host_fps_draw(int width, int height);

/* the overlay's settings (config.toml's overlay.*, by way of
host_video_configure): HOST_OVERLAY_* bits */
#define HOST_OVERLAY_ENABLED 1
#define HOST_OVERLAY_BOTTOM 2
#define HOST_OVERLAY_FRAME_TIME 4
#define HOST_OVERLAY_SHADERS 8
extern int g_host_overlay_flags;

/* host_video.c: config.toml's display and overlay settings and
debug.gl_debug, from the guest before it opens the window */
void host_video_configure(int frame_rate, int vsync, int overlay_flags, int gl_debug);
void host_loading_text_console(void); /* clears the console */

#endif
