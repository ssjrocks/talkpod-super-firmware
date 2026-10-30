/* Serial debug shell, one command per line:
 *   r RR          read BK4819 register RR (hex)
 *   w RR VVVV     write it (hex); a REG_30 value with the PA gain bit (3) is refused: no transmit
 *   x ADDR LEN    hex dump of the external flash (hex address, decimal length up to 256)
 *   d             dump the main BK4819 registers and GPIO outputs
 *   f HZ          tune the selected slot (as VFO) to HZ
 *   s L           squelch level L (0-9)
 *   t HZ          continuous local tone (no RF) at HZ, 0 = stop; dual watch pauses meanwhile
 *   b             1 kHz beep
 *   m HZ N        retune to HZ (N: 1 = narrow) and log squelch bit, RSSI, noise every 5 ms for 200 ms
 *   v LO HI ST N  survey LO..HI Hz in ST Hz steps: per step, 80 ms after the retune, print kHz,
 *                 squelch bit, noise (REG_65), RSSI dBm, glitch (REG_63); any byte stops it
 * Bytes arrive inside progmode_poll(), which also runs within delays, so they are only buffered
 * there and a finished line is run from the main loop by shell_poll().
 */
#include "shell.h"
#include "bk4819.h"
#include "board.h"
#include "debug.h"
#include "radio.h"
#include "spiflash.h"
#include "system.h"
#include "ui.h"
#include "watch.h"

static char line[32];
static volatile uint8_t line_len, line_ready;

void shell_input(char c)
{
    if (line_ready)
        return;
    if (c == '\r' || c == '\n') {
        if (line_len) {
            line[line_len] = 0;
            line_ready = 1;
        }
    } else if (line_len < sizeof line - 1) {
        line[line_len++] = c;
    }
}

static uint32_t parse_num(const char **p, int base)
{
    uint32_t v = 0;
    while (**p == ' ')
        (*p)++;
    for (;; (*p)++) {
        char c = **p;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
        if (d >= base)
            return v;
        v = v * (uint32_t)base + (uint32_t)d;
    }
}

static void put_dec(int v)
{
    char t[12];
    int n = 0;
    if (v < 0) {
        debug_putc('-');
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        debug_putc(t[--n]);
}

static void hex2(uint8_t b)
{
    debug_putc("0123456789ABCDEF"[b >> 4]);
    debug_putc("0123456789ABCDEF"[b & 15]);
}

static void print_reg(uint8_t reg)
{
    debug_puts("REG");
    hex2(reg);
    debug_puts(" = ");
    debug_hex16(bk4819_read(reg));
    debug_puts("\r\n");
}

static void restore(void)
{
    watch_update(watch_selected());
    ui_refresh();
}

static void wait_ms(uint32_t ms)
{
    uint32_t t0 = millis();
    while (millis() - t0 < ms)
        fwdgt_feed();
}

static void measure(const char *p)
{
    uint32_t hz = parse_num(&p, 10), nb = parse_num(&p, 10);
    static uint16_t log[40][3];
    if (hz < 18000000 || hz > 660000000)
        return;
    uint32_t t0 = millis();
    radio_tune(hz / 10, 0, nb != 0);
    for (int i = 0; i < 40; i++) {
        while (millis() - t0 < (uint32_t)(i + 1) * 5)
            fwdgt_feed();
        log[i][0] = bk4819_read(0x0C);
        log[i][1] = bk4819_read(0x67);
        log[i][2] = bk4819_read(0x65);
    }
    debug_puts("ms sq rssi noise; thresholds 78=");
    debug_hex16(bk4819_read(0x78));
    debug_puts(" 4F=");
    debug_hex16(bk4819_read(0x4F));
    debug_puts("\r\n");
    for (int i = 0; i < 40; i++) {
        put_dec((i + 1) * 5);
        debug_puts(log[i][0] & 2 ? " 1 " : " 0 ");
        put_dec((log[i][1] & 0x1FF) / 2 - 160);
        debug_putc(' ');
        put_dec(log[i][2] & 0x7F);
        debug_puts("\r\n");
    }
    restore();
}

static void survey(const char *p)
{
    uint32_t lo = parse_num(&p, 10) / 10, hi = parse_num(&p, 10) / 10, st = parse_num(&p, 10) / 10;
    uint32_t nb = parse_num(&p, 10);
    if (lo < 1800000 || hi > 66000000 || lo > hi || st == 0)
        return;
    debug_puts("kHz sq noise rssi glitch\r\n");
    for (uint32_t f = lo; f <= hi; f += st) {
        radio_tune(f, 0, nb != 0);
        wait_ms(80);
        if (debug_getc() >= 0) {            /* any byte stops it: the shell isn't polled meanwhile */
            debug_puts("survey stopped\r\n");
            break;
        }
        uint16_t sq = bk4819_read(0x0C), noise = bk4819_read(0x65), rssi = bk4819_read(0x67);
        uint16_t glitch = bk4819_read(0x63);
        put_dec((int)(f / 100));
        debug_puts(sq & 2 ? " 1 " : " 0 ");
        put_dec(noise & 0x7F);
        debug_putc(' ');
        put_dec((rssi & 0x1FF) / 2 - 160);
        debug_putc(' ');
        put_dec(glitch & 0xFF);
        debug_puts("\r\n");
    }
    debug_puts("survey done\r\n");
    restore();
}

void shell_poll(void)
{
    if (!line_ready)
        return;
    const char *p = line + 1;
    switch (line[0]) {
    case 'r':
        print_reg((uint8_t)parse_num(&p, 16));
        break;
    case 'w': {
        uint8_t reg = (uint8_t)parse_num(&p, 16);
        uint16_t val = (uint16_t)parse_num(&p, 16);
        if (reg == 0x30 && (val & 0x0008)) {
            debug_puts("refused: PA gain bit\r\n");
            break;
        }
        bk4819_write(reg, val);
        print_reg(reg);
        break;
    }
    case 'x': {
        uint32_t addr = parse_num(&p, 16), len = parse_num(&p, 10);
        uint8_t buf[16];
        if (len > 256)
            len = 256;
        for (uint32_t off = 0; off < len; off += 16) {
            uint32_t n = len - off < 16 ? len - off : 16;
            spiflash_read(addr + off, buf, n);
            debug_hex32(addr + off);
            debug_putc(':');
            for (uint32_t i = 0; i < n; i++) {
                debug_putc(' ');
                hex2(buf[i]);
            }
            debug_puts("\r\n");
        }
        break;
    }
    case 'd': {
        static const uint8_t regs[] = {0x0C, 0x30, 0x33, 0x37, 0x43, 0x47, 0x48, 0x4F, 0x65, 0x67,
                                       0x70, 0x78, 0x7E};
        for (unsigned i = 0; i < sizeof regs; i++)
            print_reg(regs[i]);
        debug_puts("GPIOA out ");
        debug_hex32(GPIO_OCTL(GPIOA));
        debug_puts(" GPIOB out ");
        debug_hex32(GPIO_OCTL(GPIOB));
        debug_puts(" GPIOC out ");
        debug_hex32(GPIO_OCTL(GPIOC));
        debug_puts("\r\n");
        break;
    }
    case 'f': {
        uint32_t hz = parse_num(&p, 10);
        slot_t *s = watch_slot(watch_selected());
        if (hz < 18000000 || hz > 1300000000) {
            debug_puts("18-1300 MHz\r\n");
            break;
        }
        s->is_channel = 0;
        s->vfo = hz / 10;
        restore();
        debug_puts("ok\r\n");
        break;
    }
    case 's':
        radio_set_squelch((int)parse_num(&p, 10) % 10);
        restore();
        debug_puts("ok\r\n");
        break;
    case 't': {
        uint32_t hz = parse_num(&p, 10);
        if (hz) {
            watch_pause(1);
            board_speaker(1);
            bk4819_beep(1, (uint16_t)hz);
            bk4819_write(0x48, 0xB3FF);     /* louder than the stock beep */
        } else {
            bk4819_beep(0, 0);
            watch_pause(0);
            restore();
        }
        debug_puts("ok\r\n");
        break;
    }
    case 'b':
        watch_pause(1);
        board_speaker(1);
        bk4819_beep(1, 1000);
        wait_ms(300);
        bk4819_beep(0, 0);
        watch_pause(0);
        restore();
        break;
    case 'm':
        measure(p);
        break;
    case 'v':
        survey(p);
        break;
    default:
        debug_puts("r RR | w RR VVVV | x ADDR LEN | d | f HZ | s L | t HZ | b | m HZ N | v LO HI ST N\r\n");
    }
    line_len = 0;
    line_ready = 0;
}
