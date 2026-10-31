#include <string.h>
#include "settings.h"
#include "spiflash.h"

/* record: u16 magic, u8 length, u8 0, payload (zero-padded to 26 bytes), u16 CRC-16/XMODEM of the
   first 30 bytes. A record cut short by power loss fails the CRC and is skipped. */
#define BASE(a)  (FLASH_SETTINGS + (uint32_t)(a) * FLASH_SECTOR)
#define OLD(a)   (FLASH_SETTINGS_OLD + (uint32_t)(a) * FLASH_SECTOR)
#define REC     32
#define RECORDS (FLASH_SECTOR / REC)
#define MAGIC   0xA36C

static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++)
            crc = (uint16_t)(crc & 0x8000 ? crc << 1 ^ 0x1021 : crc << 1);
    }
    return crc;
}

static int erased(const uint8_t *r)
{
    for (int i = 0; i < REC; i++)
        if (r[i] != 0xFF)
            return 0;
    return 1;
}

static int load(uint32_t base, void *dst, uint32_t len)
{
    uint8_t r[REC];
    int found = 0;
    for (uint32_t i = 0; i < RECORDS; i++) {
        spiflash_read(base + i * REC, r, REC);
        if (erased(r))
            break;
        /* a record from an older firmware is shorter: it fills the start, the rest keeps defaults */
        if ((r[0] | r[1] << 8) == MAGIC && r[2] && r[2] <= len &&
            crc16(r, REC - 2) == (r[30] | r[31] << 8)) {
            memcpy(dst, r + 4, r[2]);
            found = r[2];
        }
    }
    return found;
}

/* Firmware before 1.13 kept the logs at FLASH_SETTINGS_OLD. While the window has no record, the old
   one counts, and is copied into the window once: the PC must read what the radio uses, or its next
   Write would put defaults in their place. */
int settings_load(int area, void *dst, uint32_t len)
{
    if (len > SETTINGS_MAX)
        return 0;
    int n = load(BASE(area), dst, len);
    if (!n && (n = load(OLD(area), dst, len)) != 0)
        settings_save(area, dst, (uint32_t)n);
    return n;
}

int settings_save(int area, const void *src, uint32_t len)
{
    uint8_t r[REC];
    uint32_t i;
    if (len > SETTINGS_MAX)
        return 0;
    for (i = 0; i < RECORDS; i++) {
        spiflash_read(BASE(area) + i * REC, r, REC);
        if (erased(r))
            break;
    }
    if (i == RECORDS) {
        if (!spiflash_erase(BASE(area)))
            return 0;
        i = 0;
    }
    memset(r, 0, sizeof r);
    r[0] = MAGIC & 0xFF;
    r[1] = MAGIC >> 8;
    r[2] = (uint8_t)len;
    memcpy(r + 4, src, len);
    uint16_t crc = crc16(r, REC - 2);
    r[30] = (uint8_t)crc;
    r[31] = (uint8_t)(crc >> 8);
    return spiflash_write(BASE(area) + i * REC, r, REC);
}
