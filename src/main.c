#include "board.h"
#include "system.h"
#include "debug.h"
#include "progmode.h"
#include "lcd.h"
#include "gfx.h"
#include "keypad.h"
#include "bk4819.h"
#include "spiflash.h"
#include "radio.h"
#include "scan.h"
#include "channels.h"

/* talkpod-firmware checks these markers before flashing */
const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.84";

#define C_BG     RGB(0, 0, 0)
#define C_BAR    RGB(0, 40, 110)
#define C_TEXT   RGB(255, 255, 255)
#define C_DIM    RGB(140, 140, 140)
#define C_FREQ   RGB(255, 210, 0)
#define C_OPEN   RGB(0, 200, 60)
#define C_SHUT   RGB(50, 50, 50)
#define C_RSSI   RGB(0, 160, 255)
#define C_BUSY   RGB(200, 120, 0)

/* Receive test bench: VFO and channels, squelch, search, scan. The real UI comes later. */
static uint32_t vfo = 47690000;             /* 10 Hz units: UHF CB channel 11 */
static const uint32_t steps[] = {250, 500, 625, 1250, 2500, 10000, 100000};
static const char *const step_names[] = {"2.5k", "5k", "6.25k", "12.5k", "25k", "100k", "1M"};
static uint8_t step = 3, am, narrow, ch_mode, searching, tone_on;
static uint8_t range;                       /* VFO scan: 0 = +-1 MHz, 1 = +-5 MHz, 2 = whole band */
static const char *const range_names[] = {" R1M", " R5M", " RBand"};
static int channel = 1;
static char ch_name[13];

static void dec(char *out, int v)
{
    char tmp[11];
    int n = 0;
    if (v < 0) {
        *out++ = '-';
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *out++ = tmp[--n];
    *out = 0;
}

static char *append(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

static void field(int x, int y, int width, const char *s, uint16_t fg)
{
    int end = gfx_text(x, y, &font_6x10, s, fg, C_BG);
    if (end < x + width)
        lcd_fill(end, y, x + width - end, font_6x10.h, C_BG);
}

static void draw_freq(void)
{
    char s[10];
    uint32_t f = radio_freq(), mhz = f / 100000, frac = f % 100000;
    s[0] = (char)('0' + mhz / 100 % 10);
    s[1] = (char)('0' + mhz / 10 % 10);
    s[2] = (char)('0' + mhz % 10);
    s[3] = '.';
    for (int i = 8; i >= 4; i--, frac /= 10)
        s[i] = (char)('0' + frac % 10);
    s[9] = 0;
    gfx_text(19, 30, &font_10x20, mhz < 100 ? s + 1 : s, C_FREQ, C_BG);
    if (mhz < 100)
        lcd_fill(99, 30, 10, 20, C_BG);
}

static void draw_header(void)
{
    char s[12], n[8];
    lcd_fill(0, 0, LCD_W, 13, C_BAR);
    gfx_text(4, 2, &font_6x10, "CFW v0.84", C_TEXT, C_BAR);
    if (ch_mode) {
        dec(n, scan_mode() == SCAN_CH ? scan_channel() : channel);
        append(append(s, "CH "), n);
    } else {
        append(s, "VFO");
    }
    gfx_text(LCD_W - 4 - gfx_text_width(&font_6x10, s), 2, &font_6x10, s, C_TEXT, C_BAR);
    field(4, 16, 120, ch_mode ? ch_name : "", C_TEXT);
}

static void draw_settings(void)
{
    char s[24], n[4];
    char *p = append(s, am ? "AM " : "FM ");
    p = append(p, narrow ? "N " : "W ");
    p = append(p, step_names[step]);
    p = append(p, " SQ");
    dec(n, radio_squelch());
    p = append(p, n);
    append(p, range_names[range]);
    field(4, 54, 124, s, C_TEXT);
}

/* the thresholds the chip is using, in dBm (raw / 2 - 160) */
static void draw_thresholds(void)
{
    char s[24], n[8];
    uint16_t r78 = bk4819_read(0x78);
    char *p = append(s, "open ");
    dec(n, (r78 >> 8) / 2 - 160);
    p = append(append(p, n), " close ");
    dec(n, (r78 & 0xFF) / 2 - 160);
    append(p, n);
    field(4, 90, 120, radio_squelch() ? s : "squelch off", C_DIM);
}

static void draw_status(void)
{
    const char *t;
    uint16_t c;
    if (searching) {
        t = "SEARCH";
        c = C_BUSY;
    } else if (scan_mode() != SCAN_OFF && !scan_holding()) {
        t = scan_lockouts() ? "SCAN (L)" : "SCAN";
        c = C_BUSY;
    } else if (radio_open()) {
        t = radio_monitor() ? "MONITOR" : scan_holding() ? "SCAN HOLD" : "OPEN";
        c = C_OPEN;
    } else {
        t = "closed";
        c = C_SHUT;
    }
    lcd_fill(4, 106, 64, 16, c);
    gfx_text(8, 109, &font_6x10, t, C_TEXT, c);
}

static void draw_rssi(int dbm)
{
    char s[12];
    dec(s, dbm);
    field(40, 66, 30, s, C_TEXT);
    int w = (dbm + 130) * 84 / 80;          /* -130..-50 dBm over 84 px */
    if (w < 0) w = 0;
    if (w > 84) w = 84;
    lcd_fill(40, 80, w, 6, C_RSSI);
    lcd_fill(40 + w, 80, 84 - w, 6, C_SHUT);
}

static void redraw(void)
{
    draw_header();
    draw_freq();
    draw_settings();
    draw_thresholds();
    draw_status();
}

static void tune_vfo(void)
{
    radio_tune(vfo, am, narrow);
    redraw();
}

static void tune_channel(void)
{
    channel_t ch;
    if (!channel_read(channel, &ch)) {
        int n = channel_next(channel, 1, 0);
        if (!n || !channel_read(n, &ch)) {
            ch_mode = 0;                    /* no channels at all */
            tune_vfo();
            return;
        }
        channel = n;
    }
    for (int i = 0; i < 13; i++)
        ch_name[i] = ch.name[i];
    am = (uint8_t)channel_is_am(ch.rx);
    narrow = ch.narrow;
    radio_tune(ch.rx, am, narrow);
    redraw();
}

static void retune(void)
{
    if (ch_mode)
        tune_channel();
    else
        tune_vfo();
}

/* VFO scan limits: the band in the radio's band table (flash 0xF230: enable, low MHz, high MHz in
   BCD) that holds the frequency, airband 108-137 MHz, or 5 MHz either side; then narrowed to the
   chosen range around f. */
static void band_limits(uint32_t f, uint32_t *lo, uint32_t *hi);

static void scan_limits(uint32_t f, uint32_t *lo, uint32_t *hi)
{
    static const uint32_t half[] = {100000, 500000};    /* 1 MHz, 5 MHz in 10 Hz units */
    band_limits(f, lo, hi);
    if (range < 2) {
        if (f > *lo + half[range])
            *lo = f - half[range];
        if (f + half[range] < *hi)
            *hi = f + half[range];
    }
}

static void band_limits(uint32_t f, uint32_t *lo, uint32_t *hi)
{
    uint8_t t[20];
    spiflash_read(0xF230, t, sizeof t);
    for (int i = 0; i < 20; i += 5) {
        if (t[i] != 1)
            continue;
        uint32_t a = (uint32_t)((t[i + 1] >> 4) * 1000 + (t[i + 1] & 15) * 100 + (t[i + 2] >> 4) * 10 + (t[i + 2] & 15));
        uint32_t b = (uint32_t)((t[i + 3] >> 4) * 1000 + (t[i + 3] & 15) * 100 + (t[i + 4] >> 4) * 10 + (t[i + 4] & 15));
        if (f >= a * 100000 && f <= b * 100000) {
            *lo = a * 100000;
            *hi = b * 100000;
            return;
        }
    }
    if (f >= 10800000 && f < 13700000) {
        *lo = 10800000;
        *hi = 13700000;
        return;
    }
    *lo = f > 2300000 ? f - 500000 : 1800000;
    *hi = f + 500000;
}

static void stop_activity(void)
{
    if (searching) {
        searching = 0;
        radio_search_stop();
    }
    if (scan_mode() != SCAN_OFF) {
        scan_stop();
        if (ch_mode)
            channel = scan_channel();
        else
            vfo = radio_freq();
    }
    radio_set_monitor(0);
    retune();
}

static void beep(void)
{
    board_speaker(1);
    bk4819_beep(1, 1000);
    delay_ms(300);
    bk4819_beep(0, 0);
    retune();
}

/* Serial debug shell, one command per line:
 *   r RR          read BK4819 register RR (hex)
 *   w RR VVVV     write it (hex); a REG_30 value with the PA gain bit (3) is refused: no transmit
 *   p N 0|1|z     set PA pin N (8, 11, 12, 13, 14 or 15) low / high / floating input
 *   t HZ          continuous local tone (no RF) at HZ, 0 = stop and receive again
 *   f HZ          receive on HZ (VFO)
 *   d             dump the main BK4819 registers
 *   b             1 kHz beep
 *   x ADDR LEN    hex dump of the external flash (hex address, decimal length up to 256)
 *   s L           squelch level L (0-9)
 *   m HZ N        retune to HZ (N: 1 = narrow) and log squelch bit, RSSI, noise every 5 ms for 200 ms
 *   v LO HI ST N  survey LO..HI Hz in ST Hz steps: per step, 80 ms after the retune, print kHz,
 *                 squelch bit, noise (REG_65), RSSI dBm, glitch (REG_63); any byte stops it
 * Bytes arrive inside progmode_poll(), which also runs within delays, so they are only buffered
 * there and a finished line is run from the main loop. */
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

static void print_reg(uint8_t reg)
{
    debug_puts("REG");
    debug_putc("0123456789ABCDEF"[reg >> 4]);
    debug_putc("0123456789ABCDEF"[reg & 15]);
    debug_puts(" = ");
    debug_hex16(bk4819_read(reg));
    debug_puts("\r\n");
}

static void shell_poll(void)
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
    case 'p': {
        uint32_t pin = parse_num(&p, 10);
        while (*p == ' ')
            p++;
        int floating = *p == 'z';
        uint32_t on = parse_num(&p, 10);
        static const uint8_t allowed[] = {8, 11, 12, 13, 14, 15};
        int ok = 0;
        for (unsigned i = 0; i < sizeof allowed; i++)
            ok |= pin == allowed[i];
        if (!ok) {
            debug_puts("pins: 8 11 12 13 14 15\r\n");
            break;
        }
        if (floating) {
            gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, 1U << pin);
        } else {
            if (on)
                gpio_bit_set(GPIOA, 1U << pin);
            else
                gpio_bit_reset(GPIOA, 1U << pin);
            gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, 1U << pin);
        }
        debug_puts("ok\r\n");
        break;
    }
    case 't': {
        uint32_t hz = parse_num(&p, 10);
        tone_on = hz != 0;                          /* squelch handling pauses meanwhile */
        if (hz) {
            board_speaker(1);
            bk4819_beep(1, (uint16_t)hz);
            bk4819_write(0x48, 0xB3FF);             /* louder than the stock beep */
        } else {
            bk4819_beep(0, 0);
            retune();
        }
        debug_puts("ok\r\n");
        break;
    }
    case 'f': {
        uint32_t hz = parse_num(&p, 10);
        if (hz < 18000000 || hz > 660000000) {
            debug_puts("18-660 MHz\r\n");
            break;
        }
        stop_activity();
        ch_mode = 0;
        vfo = hz / 10;
        tune_vfo();
        debug_puts("ok\r\n");
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
    case 'b':
        beep();
        break;
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
                debug_putc("0123456789ABCDEF"[buf[i] >> 4]);
                debug_putc("0123456789ABCDEF"[buf[i] & 15]);
            }
            debug_puts("\r\n");
        }
        break;
    }
    case 's':
        radio_set_squelch((int)parse_num(&p, 10) % 10);
        redraw();
        debug_puts("ok\r\n");
        break;
    case 'v': {
        uint32_t lo = parse_num(&p, 10) / 10, hi = parse_num(&p, 10) / 10, st = parse_num(&p, 10) / 10;
        uint32_t nb = parse_num(&p, 10);
        if (lo < 1800000 || hi > 66000000 || lo > hi || st == 0)
            break;
        stop_activity();
        debug_puts("kHz sq noise rssi glitch\r\n");
        for (uint32_t f = lo; f <= hi; f += st) {
            char n[12];
            radio_tune(f, 0, nb != 0);
            uint32_t t0 = millis();
            while (millis() - t0 < 80)
                fwdgt_feed();
            if (debug_getc() >= 0) {        /* any byte stops it: the shell isn't polled meanwhile */
                debug_puts("survey stopped\r\n");
                break;
            }
            uint16_t sq = bk4819_read(0x0C), noise = bk4819_read(0x65), rssi = bk4819_read(0x67);
            uint16_t glitch = bk4819_read(0x63);
            dec(n, (int)(f / 100));
            debug_puts(n);
            debug_puts(sq & 2 ? " 1 " : " 0 ");
            dec(n, noise & 0x7F);
            debug_puts(n);
            debug_puts(" ");
            dec(n, (rssi & 0x1FF) / 2 - 160);
            debug_puts(n);
            debug_puts(" ");
            dec(n, glitch & 0xFF);
            debug_puts(n);
            debug_puts("\r\n");
        }
        debug_puts("survey done\r\n");
        retune();
        break;
    }
    case 'm': {
        uint32_t hz = parse_num(&p, 10), nb = parse_num(&p, 10);
        if (hz < 18000000 || hz > 660000000)
            break;
        stop_activity();
        ch_mode = 0;
        vfo = hz / 10;
        narrow = nb != 0;
        uint32_t t0 = millis();
        radio_tune(vfo, am, narrow);
        static uint16_t log[40][3];
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
            char n[8];
            dec(n, (i + 1) * 5);
            debug_puts(n);
            debug_puts(log[i][0] & 2 ? " 1 " : " 0 ");
            dec(n, (log[i][1] & 0x1FF) / 2 - 160);
            debug_puts(n);
            debug_puts(" ");
            dec(n, log[i][2] & 0x7F);
            debug_puts(n);
            debug_puts("\r\n");
        }
        redraw();
        break;
    }
    default:
        debug_puts("r RR | w RR VVVV | p PIN 0/1/z | f HZ | m HZ | t HZ | d | b | x ADDR LEN\r\n");
    }
    line_len = 0;
    line_ready = 0;
}

static void on_key(const key_event_t *ev)
{
    if (ev->type == KEV_PRESS) {
        debug_puts("key ");
        debug_puts(key_name(ev->key));
        debug_puts("\r\n");
    }
    int press = ev->type == KEV_PRESS, repeat = ev->type == KEV_REPEAT;
    int dir = ev->key == KEY_UP ? 1 : -1;

    if (ev->key == KEY_UP || ev->key == KEY_DOWN) {
        if (!press && !repeat)
            return;
        if (searching)
            return;
        if (scan_mode() != SCAN_OFF) {
            if (press) {                    /* move on (past a hold too) in that direction */
                scan_skip(dir);
                draw_freq();
                draw_status();
            }
            return;
        }
        if (ch_mode) {
            int n = channel_next(channel, dir, 0);
            if (n)
                channel = n;
        } else {
            vfo = dir > 0 ? vfo + steps[step] : vfo - steps[step];
            if (vfo < 1800000) vfo = 1800000;           /* 18 MHz */
            if (vfo > 66000000) vfo = 66000000;         /* 660 MHz */
        }
        retune();
        return;
    }
    if (ev->key == KEY_0 && ev->type == KEV_LONG) {
        scan_lockout_clear();
        debug_puts("lockouts cleared\r\n");
        draw_status();
        return;
    }
    if (!press)
        return;
    switch (ev->key) {
    case KEY_0:                             /* during a scan: never stop on this frequency again */
        if (scan_mode() != SCAN_OFF) {
            scan_lockout();
            draw_freq();
            draw_status();
        }
        break;
    case KEY_1:
        range = (uint8_t)((range + 1) % 3);
        draw_settings();
        break;
    case KEY_BACK:
        stop_activity();
        break;
    case KEY_AB:
        stop_activity();
        if (!ch_mode)
            vfo = radio_freq();
        ch_mode ^= 1;
        retune();
        break;
    case KEY_2:
        radio_set_squelch((radio_squelch() + 1) % 10);
        draw_settings();
        draw_thresholds();
        draw_status();
        break;
    case KEY_3:
        step = (uint8_t)((step + 1) % (sizeof steps / sizeof steps[0]));
        draw_settings();
        break;
    case KEY_9:
        if (ch_mode)
            break;
        am ^= 1;
        tune_vfo();
        break;
    case KEY_FM:
        if (ch_mode)
            break;
        narrow ^= 1;
        tune_vfo();
        break;
    case KEY_MENU:
        radio_set_monitor(!radio_monitor());
        draw_status();
        break;
    case KEY_4:
        if (searching) {
            stop_activity();
            break;
        }
        stop_activity();
        searching = 1;
        radio_search_start();
        draw_status();
        break;
    case KEY_6:
        if (scan_mode() != SCAN_OFF) {
            stop_activity();
            break;
        }
        stop_activity();
        if (ch_mode) {
            scan_start_ch(channel, 1);
        } else {
            uint32_t lo, hi;
            scan_limits(vfo, &lo, &hi);
            scan_start_vfo(lo, hi, steps[step], 1, am, narrow);
        }
        draw_status();
        break;
    case KEY_5:
        beep();
        break;
    case KEY_7:
        stop_activity();
        ch_mode = 0;
        vfo = 10450000;                     /* 104.5 MHz broadcast: a known signal */
        tune_vfo();
        break;
    case KEY_PTT:
        debug_puts("PTT: no transmit in this build\r\n");
        break;
    default:
        break;
    }
}

int main(void)
{
    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.84 ===\r\nbootloader tag ");
    debug_write((const uint8_t *)0x08000300U, 7);
    debug_puts("\r\n");

    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */

    board_init();
    lcd_init();
    keypad_init();
    bk4819_init();
    board_rf_path(0, RF_OFF);                   /* as stock before its first RX: REG_33 = 0 */
    spiflash_init();
    debug_puts("flash id ");
    debug_hex32(spiflash_jedec_id());
    debug_puts("\r\n");
    radio_init();

    gfx_text(4, 66, &font_6x10, "RSSI", C_DIM, C_BG);
    tune_vfo();

    uint32_t next_key = millis(), next_radio = millis(), next_show = millis();
    for (;;) {
        progmode_poll();
        fwdgt_feed();
        uint32_t now = millis();
        if ((int32_t)(now - next_key) >= 0) {
            next_key += KEY_SCAN_MS;
            keypad_scan();
            board_power_poll();
        }
        key_event_t ev;
        while (keypad_get(&ev))
            on_key(&ev);
        shell_poll();

        if ((int32_t)(now - next_radio) >= 0 && !tone_on) {
            next_radio = now + 10;
            if (searching) {
                uint32_t f;
                if (radio_search_poll(&f)) {
                    searching = 0;
                    bk4819_search(0);
                    ch_mode = 0;
                    vfo = f;
                    tune_vfo();
                    debug_puts("search found ");
                    debug_hex32(f);
                    debug_puts("\r\n");
                }
            } else if (scan_mode() != SCAN_OFF) {
                int e = scan_poll();
                if (e == SCAN_EV_STEP || e == SCAN_EV_RESUME) {
                    if (scan_mode() == SCAN_CH) {
                        channel_t ch;
                        channel = scan_channel();
                        if (channel_read(channel, &ch))
                            for (int i = 0; i < 13; i++)
                                ch_name[i] = ch.name[i];
                        draw_header();
                    }
                    draw_freq();
                    draw_status();
                } else if (e == SCAN_EV_HOLD) {
                    draw_status();
                }
            } else if (radio_poll()) {
                draw_status();
            }
        }
        if ((int32_t)(now - next_show) >= 0) {
            next_show = now + 250;
            draw_rssi(radio_rssi_dbm());
        }
    }
}
