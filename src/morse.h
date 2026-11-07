#ifndef MORSE_H
#define MORSE_H

#include <stdint.h>

/* Morse code sent as a tone on the FM transmitter (MCW), src/morse.c */
#define MORSE_WPM     30                /* about a professional operator's speed */
#define MORSE_HZ      700
#define MORSE_TEXT    87                /* characters: three text messages' worth */

int  morse_start(const char *text, uint32_t freq, int narrow, int low_power, int tone, uint16_t tone_value,
                 int wpm);              /* 1 sending, -1 TX not allowed there, 0 nothing to send */
void morse_poll(void);                  /* as often as possible: keys the tone on time, ends the TX */
void morse_stop(void);                  /* now: no Roger, no tail */
int  morse_active(void);
int  morse_pos(void);                   /* the character being sent */
uint32_t morse_ms(const char *text, int wpm);   /* how long sending it takes, key-up to key-down */
uint8_t morse_code(char c);             /* elements after the leading 1, MSB first: 1 dah, 0 dit; 0 none */

#endif
