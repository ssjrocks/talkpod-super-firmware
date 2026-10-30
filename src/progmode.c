/* The stock firmware's serial programming protocol, so CHIRP and Talkpod Manager can identify the
 * radio, read and write its channels and settings, and restart it into the bootloader.
 *
 *   PC -> "PROGRAMJC37U"           radio -> 0x06
 *   PC -> 'F'                      radio -> 8-byte identification
 *   PC -> 'R' addr(2, BE) len(1)   radio -> 'R' addr len, then len bytes of flash from addr
 *   PC -> 'W' addr(2, BE) len(1), then len bytes    radio -> 0x06
 *   PC -> 'E'                      (leave programming mode)
 *   PC -> "UPDATE"                 radio -> 0x06, then reset: the bootloader listens for an INTO_BOOT
 *                                  frame at every start, so talkpod-firmware can flash with no buttons.
 *
 * 'R' / 'W' address the external flash directly, 0 .. FLASH_WINDOW_END, as stock does: channels at
 * 0x0000, VFO data 0x8000, settings 0x9000, DTMF 0xA000, ANI codes and names 0xB000 (the layout of
 * CHIRP's mml_jc8810 driver). Writes collect in a 4 KB sector buffer; the sector is erased and
 * programmed once, when a write moves to another sector, on 'E', or when the PC goes quiet.
 */
#include <stdint.h>
#include <string.h>
#include "debug.h"
#include "progmode.h"
#include "spiflash.h"
#include "system.h"

static const char MAGIC[] = "PROGRAMJC37U";
/* Not a stock fingerprint, so CHIRP / Talkpod Manager won't mistake us for stock firmware. The
   last two digits are the protocol level: 01 identification only, 02 'R' / 'W' as well. */
static const uint8_t IDENT[8] = {'A', '3', '6', 'C', 'F', 'W', '0', '2'};
#define ACK 0x06
#define NAK 0x15
#define NO_SECTOR 0xFFFFFFFFU

/* Bytes that aren't programming traffic also go to a line-based debug shell, if one is linked in. */
void shell_input(char c) __attribute__((weak));
/* Called after the PC changed the window (channels, settings), to show the new data. */
void progmode_changed(void) __attribute__((weak));

static char window[12];
static uint8_t in_prog, busy, changed;
static uint8_t upd_len;
static uint32_t last_rx;
static uint8_t cmd[4 + 255];
static uint16_t have, need;

static uint8_t sector[FLASH_SECTOR];
static uint32_t sector_addr = NO_SECTOR;
static uint8_t dirty;

static int flush(void)
{
    int ok = 1;
    if (dirty) {
        ok = spiflash_erase(sector_addr) && spiflash_write(sector_addr, sector, FLASH_SECTOR);
        dirty = 0;
        changed = 1;
    }
    sector_addr = NO_SECTOR;
    return ok;
}

static int window_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr >= FLASH_WINDOW_END || len > FLASH_WINDOW_END - addr)
        return 0;
    while (len) {
        uint32_t s = addr & ~(FLASH_SECTOR - 1), off = addr - s;
        uint32_t n = FLASH_SECTOR - off < len ? FLASH_SECTOR - off : len;
        if (s != sector_addr) {
            if (!flush())
                return 0;
            spiflash_read(s, sector, FLASH_SECTOR);
            sector_addr = s;
        }
        if (memcmp(sector + off, data, n)) {
            memcpy(sector + off, data, n);
            dirty = 1;
        }
        addr += n;
        data += n;
        len -= n;
    }
    return 1;
}

static void window_read(uint32_t addr, uint8_t *out, uint32_t len)
{
    if (addr >= FLASH_WINDOW_END)
        len = 0;
    else if (len > FLASH_WINDOW_END - addr)
        len = FLASH_WINDOW_END - addr;
    spiflash_read(addr, out, len);
    if (sector_addr != NO_SECTOR)               /* written, not programmed yet */
        for (uint32_t i = 0; i < len; i++)
            if (((addr + i) & ~(FLASH_SECTOR - 1)) == sector_addr)
                out[i] = sector[addr + i - sector_addr];
}

static void run(void)
{
    uint32_t addr = (uint32_t)cmd[1] << 8 | cmd[2], len = cmd[3];
    if (cmd[0] == 'R') {
        uint8_t data[255];
        memset(data, 0xFF, len);
        window_read(addr, data, len);
        debug_write(cmd, 4);
        debug_write(data, len);
    } else {
        debug_putc(window_write(addr, cmd + 4, len) ? ACK : NAK);
    }
}

/* Debug logging stays muted from the handshake until the PC sends 'E' or goes quiet for 1 s. */
static void leave(void)
{
    in_prog = 0;
    upd_len = 0;
    need = 0;
    flush();
    debug_mute(0);
    if (changed && progmode_changed)
        progmode_changed();
    changed = 0;
}

void progmode_poll(void)
{
    int c;
    if (busy)
        return;                                 /* called again from a delay inside a command */
    busy = 1;
    while ((c = debug_getc()) >= 0) {
        last_rx = millis();
        if (in_prog) {
            if (need) {                         /* inside an 'R' / 'W' command */
                cmd[have++] = (uint8_t)c;
                if (have == 4 && cmd[0] == 'W')
                    need = (uint16_t)(4 + cmd[3]);
                if (have == need) {
                    need = 0;
                    run();
                }
                continue;
            }
            if (upd_len) {                      /* collecting "UPDATE" */
                static const char UPD[] = "UPDATE";
                if (c == UPD[upd_len]) {
                    if (++upd_len == 6) {
                        flush();
                        debug_putc(ACK);
                        system_reset();
                    }
                    continue;
                }
                upd_len = 0;
            }
            if (c == 'R' || c == 'W') {
                cmd[0] = (uint8_t)c;
                have = 1;
                need = 4;
                continue;
            }
            if (c == 'F') {
                debug_write(IDENT, sizeof IDENT);
                continue;
            }
            if (c == 'E') {
                leave();
                continue;
            }
            if (c == 'U') {
                upd_len = 1;
                continue;
            }
        }
        if (shell_input && !in_prog)
            shell_input((char)c);
        memmove(window, window + 1, sizeof window - 1);
        window[sizeof window - 1] = (char)c;
        if (memcmp(window, MAGIC, sizeof window) == 0) {
            if (shell_input)
                shell_input(0x18);              /* CAN: drop the handshake from the shell's line */
            debug_mute(1);
            debug_putc(ACK);
            in_prog = 1;
            upd_len = 0;
            need = 0;
        }
    }
    if (in_prog && millis() - last_rx > 1000)
        leave();
    busy = 0;
}
