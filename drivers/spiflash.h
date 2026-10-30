#ifndef SPIFLASH_H
#define SPIFLASH_H

#include <stdint.h>

/* W25Q16 (2 MB) external flash, read-only: the stock channels, settings and per-radio calibration
   live here (docs/stock-firmware.md), so this driver has no write or erase commands at all. */
void     spiflash_init(void);
uint32_t spiflash_jedec_id(void);              /* W25Q16: 0xEF4015 */
void     spiflash_read(uint32_t addr, void *dst, uint32_t len);

#endif
