#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>

enum { SCAN_OFF, SCAN_VFO, SCAN_CH };
enum { SCAN_EV_NONE, SCAN_EV_STEP, SCAN_EV_HOLD, SCAN_EV_RESUME };

/* Frequency scan across lo..hi (10 Hz units) in steps, AM where channel_is_am(); channel scan over
   the channels with the scan flag. Stops on a signal, resumes as the menu's Scan resume says. */
void scan_start_vfo(uint32_t lo, uint32_t hi, uint32_t step, int narrow);
void scan_start_ch(int from, int dir);
void scan_stop(void);
void scan_skip(int dir);                    /* move on now, in direction dir */
void scan_lockout(void);                    /* never stop on this frequency again; move on */
void scan_lockout_clear(void);
int  scan_lockouts(void);
int  scan_mode(void);
int  scan_holding(void);
int  scan_channel(void);                    /* channel scan: the channel tuned now */
int  scan_poll(void);                       /* every 10 ms; a SCAN_EV_* */

int  channel_is_am(uint32_t freq_10hz);     /* airband 108-137 MHz, as the stock firmware, and 27 MHz CB */

#endif
