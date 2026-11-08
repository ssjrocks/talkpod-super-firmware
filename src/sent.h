#ifndef SENT_H
#define SENT_H

#include <stdint.h>

/* The texts sent from the typing screens, newest first, kept in the external flash (src/sent.c) */
#define SENT_TEXT 144                   /* the longest a screen sends (Morse) */
enum { SENT_MSG, SENT_APRS, SENT_PAGER, SENT_MORSE };

void sent_add(const char *text, int kind);      /* the same text as the newest isn't kept twice */
int  sent_count(void);
int  sent_get(int i, char *text, int *kind);    /* i = 0 the newest; text holds SENT_TEXT + 1; 0 if none */

#endif
