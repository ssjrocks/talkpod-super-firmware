/* The stock firmware's settings and VFO data, stored exactly as stock stores them, so both firmwares
 * and the PC share them (measured from stock V1.22: 0x8005188 load, 0x8005320 save, 0x8005270 /
 * 0x800546c the VFOs, 0x8007eb4 program mode).
 *
 * Each is a wear-levelling log in one 4 KB sector: a bitmap at the start, then records from +0x10.
 * The first set bit of the bitmap (bit 0 of byte 0 first) is the newest record's slot. Saving
 * writes the next slot, then clears the bit of the old one; when the sector is full it is erased and
 * slot 0 is written. A record ends in the CRC-16/XMODEM of the rest (little-endian). An invalid
 * record means defaults.
 *
 * Program mode doesn't show the log. Reading the 64 bytes at 0x9000 gives the settings and at 0x8000
 * the VFOs, and writing them adds a record. Every other address is plain flash.
 */
#include <string.h>
#include "stock.h"
#include "spiflash.h"

typedef struct {
    uint32_t base;
    uint8_t size, map_bytes, last_read, last_write;   /* last_write: full from this slot on */
} log_t;

static const log_t LOG_SET = {STOCK_SET, 0x60, 6, 0x29, 0x29};
static const log_t LOG_VFO = {STOCK_VFO, 0x42, 8, 0x3C, 0x3C};
#define RECORD_MAX 0x60

/* stock 0x8009060 */
static const uint8_t DEFAULTS[0x31] = {
    0x03, 0x01, 0x00, 0x03, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x05, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x05, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1D, 0x0A, 0x07, 0x00,
    0x00,
};

stock_t stock;
static stock_t stored;                  /* what the newest record holds */

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

static int newest_slot(const log_t *l)
{
    uint8_t map[8];
    spiflash_read(l->base, map, l->map_bytes);
    for (int i = 0; i < l->map_bytes; i++)
        for (int b = 0; b < 8; b++)
            if (map[i] >> b & 1)
                return i * 8 + b;
    return 0xFF;
}

static uint32_t slot_addr(const log_t *l, int slot)
{
    return l->base + 0x10 + (uint32_t)slot * l->size;
}

static int record_read(const log_t *l, uint8_t *r)
{
    int slot = newest_slot(l);
    if (slot > l->last_read)
        return 0;
    spiflash_read(slot_addr(l, slot), r, l->size);
    return crc16(r, l->size - 2u) == (r[l->size - 2] | r[l->size - 1] << 8);
}

static int record_write(const log_t *l, const uint8_t *block)
{
    uint8_t r[RECORD_MAX], old[RECORD_MAX];
    memset(r, 0, sizeof r);
    memcpy(r, block, STOCK_BLOCK);
    uint16_t crc = crc16(r, l->size - 2u);
    r[l->size - 2] = (uint8_t)crc;
    r[l->size - 1] = (uint8_t)(crc >> 8);
    if (record_read(l, old) && memcmp(old, r, l->size) == 0)
        return 1;
    int slot = newest_slot(l);
    if (slot >= l->last_write) {
        return spiflash_erase(l->base) && spiflash_write(slot_addr(l, 0), r, l->size);
    }
    if (!spiflash_write(slot_addr(l, slot + 1), r, l->size))
        return 0;
    uint8_t m;
    spiflash_read(l->base + (uint32_t)slot / 8, &m, 1);
    m &= (uint8_t)~(1u << (slot & 7));
    return spiflash_write(l->base + (uint32_t)slot / 8, &m, 1);
}

void stock_load(void)
{
    uint8_t r[RECORD_MAX];
    if (record_read(&LOG_SET, r)) {
        memcpy(&stock, r, sizeof stock);
    } else {
        memset(&stock, 0, sizeof stock);
        memcpy(&stock, DEFAULTS, sizeof DEFAULTS);
    }
    stored = stock;
}

int stock_save(void)
{
    if (memcmp(&stock, &stored, sizeof stock) == 0)
        return 1;
    stored = stock;
    return record_write(&LOG_SET, (const uint8_t *)&stock);
}

void stock_block_read(uint32_t base, uint8_t *out)
{
    uint8_t r[RECORD_MAX];
    if (base == STOCK_SET) {
        memcpy(out, &stock, STOCK_BLOCK);
    } else if (record_read(&LOG_VFO, r)) {
        memcpy(out, r, STOCK_BLOCK);
    } else {
        memset(out, 0, STOCK_BLOCK);
    }
}

int stock_block_write(uint32_t base, const uint8_t *data)
{
    if (base == STOCK_VFO)
        return record_write(&LOG_VFO, data);
    memcpy(&stock, data, sizeof stock);
    stored = stock;
    return record_write(&LOG_SET, data);
}
