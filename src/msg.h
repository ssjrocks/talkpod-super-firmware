#ifndef MSG_H
#define MSG_H

#include <stdint.h>

/* Text messages in the UV-K5 messenger's format (see msg.c). */
#define MSG_TEXT 29                     /* characters a message can have */

typedef struct {
    char text[31];                      /* up to 30 received, MSG_TEXT sent */
    uint8_t flags;
} msg_t;

enum { MSG_IN = 1, MSG_OUT = 2, MSG_DELIVERED = 4, MSG_SECRET = 8 };
enum { MSG_EV_NEW = 1, MSG_EV_DELIVERED = 2, MSG_EV_RECEIPT_DUE = 4, MSG_EV_RX = 8,
       MSG_EV_ROLL = 16 };                      /* msg_poll(); MSG_EV_ROLL: msg_roll_burst() */

void msg_init(int modem, int receipts);         /* BK_FSK_1200 / 700 / 450 */
void msg_set_modem(int modem);
int  msg_modem(void);
void msg_set_receipts(int on);                  /* answer messages with a delivery receipt */
int  msg_receipts(void);
int  msg_poll(void);                            /* every 10 ms while not transmitting: MSG_EV_* bits */
int  msg_receiving(void);                       /* a packet is arriving: keep the receiver here */
const uint8_t *msg_roll_burst(uint32_t *end_ms);   /* the rolling scrambler's burst, when its data ended */
int  msg_send(const char *text, uint32_t freq, int narrow, int low_power);
                                                /* 1 sent, 0 failed, -1 not allowed on this frequency */
int  msg_send_receipt(int low_power);           /* after MSG_EV_RECEIPT_DUE: on the frequency it came in on */
int  msg_count(void);
const msg_t *msg_get(int i);                    /* 0 = newest */
int  msg_unread(void);
void msg_read_all(void);

#endif
