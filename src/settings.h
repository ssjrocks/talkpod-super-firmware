#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* Settings persistence: a log of 32-byte records in one sector of the firmware's own flash region
   (FLASH_SETTINGS); the newest valid record wins, and the sector is erased only when it is full. */
#define SETTINGS_MAX 26                 /* payload bytes per record */

int settings_load(void *dst, uint32_t len);         /* bytes loaded (an older record can be shorter) */
int settings_save(const void *src, uint32_t len);   /* 1 on success */

#endif
