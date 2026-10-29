#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

/* Matrix keys in scan order (row * 4 + column + 1), then the side keys and PTT. */
enum {
    KEY_NONE = 0,
    KEY_MENU, KEY_UP, KEY_DOWN, KEY_BACK,
    KEY_1, KEY_2, KEY_3, KEY_AB,
    KEY_4, KEY_5, KEY_6, KEY_0,
    KEY_7, KEY_8, KEY_9, KEY_FM,
    KEY_TOP, KEY_SIDE1, KEY_SIDE2,
    KEY_PTT,
    KEY_COUNT
};

enum {
    KEV_PRESS,          /* debounced press */
    KEV_LONG,           /* still held after KEY_LONG_MS */
    KEV_REPEAT,         /* every KEY_REPEAT_MS after KEV_LONG (not for PTT) */
    KEV_RELEASE,
};

typedef struct {
    uint8_t key, type;
} key_event_t;

void keypad_init(void);
void keypad_scan(void);                 /* call every KEY_SCAN_MS */
int  keypad_get(key_event_t *ev);       /* 1 and fills *ev if an event is waiting */
int  ptt_pressed(void);
const char *key_name(uint8_t key);

#define KEY_SCAN_MS     10
#define KEY_LONG_MS     600
#define KEY_REPEAT_MS   120

#endif
