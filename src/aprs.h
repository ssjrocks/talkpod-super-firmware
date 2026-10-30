#ifndef APRS_H
#define APRS_H

#include <stdint.h>

/* APRS on AFSK 1200 (see aprs.c). */
typedef struct __attribute__((packed)) {
    char call[10];                      /* "VK2ABC-9"; empty until set */
    uint8_t path;                       /* 0 none, 1 WIDE1-1, 2 WIDE2-1, 3 WIDE1-1,WIDE2-1 */
    char text[15];                      /* status beacon */
} aprs_cfg_t;

typedef struct {
    char from[10];                      /* the sender, or for one of ours the addressee */
    char text[48];                      /* the packet, summed up */
    char id[6];                         /* message number (ours, for its ack) */
    uint8_t flags;
} aprs_pkt_t;

enum { APRS_OUT = 1, APRS_TO_ME = 2, APRS_ACKED = 4, APRS_MSG = 8 };
enum { APRS_EV_NEW = 1, APRS_EV_ACKED = 2, APRS_EV_ACK_DUE = 4, APRS_EV_RX = 8 };   /* aprs_poll() */

extern aprs_cfg_t aprs_cfg;
extern const char *const aprs_path_names[4];

void aprs_init(void);                   /* loads aprs_cfg */
void aprs_save(void);
void aprs_listen(int on);               /* the receiver's modem: APRS, or back to UV-K5 messages */
int  aprs_poll(void);                   /* every 10 ms while listening and not transmitting */
int  aprs_receiving(void);
int  aprs_beacon(uint32_t freq, int narrow, int low_power);
int  aprs_message(const char *to, const char *text, uint32_t freq, int narrow, int low_power);
int  aprs_send_ack(int low_power);      /* after APRS_EV_ACK_DUE, where the message came in */
                                        /* the senders: 1 sent, 0 failed, -1 not allowed, -2 no call */
int  aprs_count(void);
const aprs_pkt_t *aprs_get(int i);      /* 0 = newest */
int  aprs_unread(void);
void aprs_read_all(void);

#endif
