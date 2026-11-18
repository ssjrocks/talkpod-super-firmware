#ifndef UI_H
#define UI_H

#include "keypad.h"

void ui_init(void);
void ui_key(const key_event_t *ev);
void ui_tick(void);                     /* every 10 ms: radio, timers, screen updates */
void ui_refresh(void);                  /* redraw everything (after the shell changed things) */
void ui_wake(void);                     /* the display on, as a real key would turn it on */
int  ui_type(const char *text);         /* the open typing screen's text; -1 if none is open */

#endif
