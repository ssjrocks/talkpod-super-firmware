#ifndef SPIFLASH_H
#define SPIFLASH_H

#include <stdint.h>

/* W25Q16-compatible (2 MB) external flash. The stock channels, settings, per-radio calibration,
   fonts and voice prompts live here (docs/stock-firmware.md), so writing and erasing are refused
   outside FLASH_OWN_START..FLASH_OWN_END, the region this firmware keeps its own data in. */
/* Inside 0x192000-0x1EFFFF, which the 2 MB backup of 2026-10-30 shows erased after the radio's
   use with stock firmware (stock data at 0x190000-0x191FFF and 0x1F0000-0x1F1FFF is left alone).
   0x1A0000-0x1DFFFF: boot splash; 0x1E0000: settings, then messages. */
#define FLASH_WRITES    1
#define FLASH_OWN_START 0x1A0000U
#define FLASH_OWN_END   0x1F0000U
#define FLASH_SETTINGS  0x1E0000U
#define FLASH_SECTOR    4096U

void     spiflash_init(void);
uint32_t spiflash_jedec_id(void);              /* W25Q16: 0xEF4015; this radio: 0x5E6015 */
void     spiflash_read(uint32_t addr, void *dst, uint32_t len);
int      spiflash_erase(uint32_t addr);        /* one 4 KB sector; 0 if refused or timed out */
int      spiflash_write(uint32_t addr, const void *src, uint32_t len);   /* into erased flash */

#endif
