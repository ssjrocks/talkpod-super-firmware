#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* Settings persistence: a log of 32-byte records per area, each area one sector from FLASH_SETTINGS
   on (0x4000-0x7FFF, inside the program-mode window, so the PC's Read / Write carry them); the newest
   valid record wins, and the sector is erased only when it is full. */
enum { SET_MAIN, SET_APRS, SET_PAGER, SET_SCRAMBLE };
#define SETTINGS_MAX 26                 /* payload bytes per record */

int settings_load(int area, void *dst, uint32_t len);         /* bytes loaded (an older record can be shorter) */
int settings_save(int area, const void *src, uint32_t len);   /* 1 on success */

#endif
