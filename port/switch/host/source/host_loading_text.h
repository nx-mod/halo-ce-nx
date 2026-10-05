#ifndef HOST_LOADING_TEXT_H
#define HOST_LOADING_TEXT_H

void host_loading_text_draw(int width, int height);
/* the guest's main loop has started (main.c): no more loading text */
void host_loading_text_stop(void);
void host_loading_text_console(void); /* clears the console */

#endif
