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

/* host_threads.c */
void host_pin_current_thread(int core);
void host_sleep_ns(long long ns);

/* host_bik.c: the disc's Bink movies through FFmpeg, for the guest's mjx.c */
long host_bik_open(unsigned int path_address, unsigned int info_address);
long host_bik_decode(long handle, unsigned int index, unsigned int layout_address);
long host_bik_read_audio(long handle, unsigned int offset, unsigned int destination_address, unsigned int length);
void host_bik_close(long handle);

/* host_mjx.c: a .mjx movie picture's decode, for the guest's mjx.c */
long host_mjx_decode(unsigned int jpeg_address, unsigned int length, unsigned int layout_address);
void host_loading_text_console(void); /* clears the console */

#endif
