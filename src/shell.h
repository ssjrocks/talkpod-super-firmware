#ifndef SHELL_H
#define SHELL_H

void shell_input(char c);       /* called by progmode_poll() for every received byte */
void shell_poll(void);          /* runs a finished command line; call from the main loop */

#endif
