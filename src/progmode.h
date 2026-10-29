#ifndef PROGMODE_H
#define PROGMODE_H

/* Poll the serial port for the programming handshake; resets into the bootloader on UPDATE. */
void progmode_poll(void);

#endif
