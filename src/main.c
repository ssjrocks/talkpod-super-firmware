#include "board.h"
#include "system.h"
#include "debug.h"
#include "progmode.h"
#include "lcd.h"
#include "gfx.h"
#include "keypad.h"
#include "bk4819.h"

/* talkpod-firmware checks these markers before flashing */
const char __attribute__((used, section(".rodata.markers"))) fw_marker1[] = "PROGRAMJC37U";
const char __attribute__((used, section(".rodata.markers"))) fw_marker2[] = "TalkpodA36Plus";
const char __attribute__((used, section(".rodata.markers"))) fw_version[]  = " V0.72";

#define C_BG     RGB(0, 0, 0)
#define C_BAR    RGB(0, 40, 110)
#define C_TEXT   RGB(255, 255, 255)
#define C_DIM    RGB(140, 140, 140)
#define C_FREQ   RGB(255, 210, 0)
#define C_OPEN   RGB(0, 200, 60)
#define C_SHUT   RGB(50, 50, 50)
#define C_RSSI   RGB(0, 160, 255)

/* Receive test bench: tune, watch RSSI, beep, and poke the audio path from the serial shell. */
static uint32_t freq = 47690000;            /* 10 Hz units: UHF CB channel 11 */
static const uint32_t steps[] = {1250, 10000, 100000};      /* 12.5 kHz, 100 kHz, 1 MHz */
static const char *const step_names[] = {"12.5k", "100k", "1M"};
static uint8_t step, am, narrow, monitor, open, tone_on;
static int squelch = -115;                  /* dBm */
static const uint32_t aux_pins[3] = {AUX_PA11_PIN, MIC_EN_PIN, AUX_PA8_PIN};
static uint8_t aux[3];

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

static void field(int x, int y, int width, const char *s, uint16_t fg)
{
    int end = gfx_text(x, y, &font_6x10, s, fg, C_BG);
    if (end < x + width)
        lcd_fill(end, y, x + width - end, font_6x10.h, C_BG);
}

static void draw_freq(void)
{
    char s[10];
    uint32_t mhz = freq / 100000, frac = freq % 100000;
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

static void draw_settings(void)
{
    char s[24], n[8];
    int i = 0;
    for (const char *p = am ? "AM " : "FM "; *p;) s[i++] = *p++;
    for (const char *p = narrow ? "N " : "W "; *p;) s[i++] = *p++;
    for (const char *p = step_names[step]; *p;) s[i++] = *p++;
    s[i++] = ' ';
    s[i++] = 'S';
    s[i++] = 'Q';
    dec(n, squelch);
    for (const char *p = n; *p;) s[i++] = *p++;
    s[i] = 0;
    field(4, 54, 120, s, C_TEXT);
}

static void draw_aux(void)
{
    static const char *const names[3] = {"A11:", "A12:", "A8:"};
    int x = 4;
    for (int i = 0; i < 3; i++) {
        x = gfx_text(x, 92, &font_6x10, names[i], C_DIM, C_BG);
        x = gfx_text(x, 92, &font_6x10, aux[i] ? "1 " : "0 ", aux[i] ? C_OPEN : C_TEXT, C_BG);
    }
}

static void draw_open(void)
{
    uint16_t c = open ? C_OPEN : C_SHUT;
    lcd_fill(4, 106, 56, 16, c);
    gfx_text(8, 109, &font_6x10, open ? (monitor ? "MONITOR" : "OPEN") : "closed", C_TEXT, c);
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

static void set_aux(int i, int on)
{
    aux[i] = (uint8_t)on;
    if (on)
        gpio_bit_set(GPIOA, aux_pins[i]);
    else
        gpio_bit_reset(GPIOA, aux_pins[i]);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, aux_pins[i]);
    draw_aux();
}

static void set_open(int on)
{
    open = (uint8_t)on;
    bk4819_af(on ? (am ? BK_AF_AM : BK_AF_FM) : BK_AF_MUTE);
    board_led(0, on);
    set_aux(1, on);                         /* PA12: speaker amplifier */
    draw_open();
}

static void tune(void)
{
    bk4819_rx(freq, narrow);
    board_rf_path(freq, RF_RX);
    set_open(monitor);
    draw_freq();
}

static void toggle_aux(int i)
{
    set_aux(i, !aux[i]);
    debug_puts(i == 0 ? "PA11 " : i == 1 ? "PA12 " : "PA8 ");
    debug_puts(aux[i] ? "high\r\n" : "low\r\n");
}

static void beep(void)
{
    debug_puts("beep\r\n");
    set_aux(1, 1);
    bk4819_beep(1, 1000);
    delay_ms(300);
    bk4819_beep(0, 0);
    tune();
}

/* Serial debug shell, one command per line:
 *   r RR          read BK4819 register RR (hex)
 *   w RR VVVV     write it (hex); a REG_30 value with the PA gain bit (3) is refused: no transmit
 *   p N 0|1|z     set PA pin N (8, 11, 12, 13, 14 or 15) low / high / floating input
 *   t HZ          continuous local tone (no RF) at HZ, 0 = stop and receive again
 *   n NUM         show a big number where the frequency goes (for listening tests)
 *   f HZ          receive on HZ
 *   d             dump the main BK4819 registers
 *   b             1 kHz beep
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
        for (int i = 0; i < 3; i++)
            if (aux_pins[i] == (1U << pin))
                aux[i] = (uint8_t)(on != 0);
        if (floating) {
            gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_NONE, 1U << pin);
        } else {
            if (on)
                gpio_bit_set(GPIOA, 1U << pin);
            else
                gpio_bit_reset(GPIOA, 1U << pin);
            gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, 1U << pin);
        }
        draw_aux();
        debug_puts("ok\r\n");
        break;
    }
    case 't': {
        uint32_t hz = parse_num(&p, 10);
        tone_on = hz != 0;                          /* squelch handling pauses meanwhile */
        if (hz) {
            bk4819_beep(1, (uint16_t)hz);
            bk4819_write(0x48, 0xB3FF);             /* louder than the stock beep */
        } else {
            bk4819_beep(0, 0);
            tune();
        }
        debug_puts("ok\r\n");
        break;
    }
    case 'n': {
        char num[8];
        int v = (int)parse_num(&p, 10);
        dec(num, v);
        lcd_fill(0, 30, LCD_W, 20, C_BG);
        gfx_text(44, 30, &font_10x20, num, C_TEXT, C_BG);
        debug_puts("ok\r\n");
        break;
    }
    case 'f': {
        uint32_t hz = parse_num(&p, 10);
        if (hz < 18000000 || hz > 660000000) {
            debug_puts("18-660 MHz\r\n");
            break;
        }
        freq = hz / 10;
        tune();
        debug_puts("ok\r\n");
        break;
    }
    case 'd': {
        static const uint8_t regs[] = {0x0C, 0x30, 0x33, 0x37, 0x43, 0x47, 0x48, 0x65, 0x67, 0x70, 0x7E};
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
    default:
        debug_puts("r RR | w RR VVVV | p PIN 0/1/z | f HZ | t HZ | n NUM | d | b\r\n");
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
    switch (ev->key) {
    case KEY_UP:
    case KEY_DOWN:
        if (!press && !repeat)
            return;
        freq = ev->key == KEY_UP ? freq + steps[step] : freq - steps[step];
        if (freq < 1800000) freq = 1800000;           /* 18 MHz */
        if (freq > 66000000) freq = 66000000;         /* 660 MHz */
        tune();
        return;
    default:
        break;
    }
    if (!press)
        return;
    switch (ev->key) {
    case KEY_BACK: step = (uint8_t)((step + 1) % 3); draw_settings(); break;
    case KEY_0:    am ^= 1; set_open(open); draw_settings(); break;
    case KEY_FM:   narrow ^= 1; tune(); draw_settings(); break;
    case KEY_4:    squelch -= 2; draw_settings(); break;
    case KEY_6:    squelch += 2; draw_settings(); break;
    case KEY_MENU: monitor ^= 1; set_open(monitor); break;
    case KEY_1:    toggle_aux(0); break;
    case KEY_2:    toggle_aux(1); break;
    case KEY_3:    toggle_aux(2); break;
    case KEY_5:    beep(); break;
    case KEY_7:    freq = 10450000; tune(); break;   /* 104.5 MHz broadcast: a known signal */
    case KEY_8:    freq = 47690000; tune(); break;
    case KEY_PTT:  debug_puts("PTT: no transmit in this build\r\n"); break;
    default: break;
    }
}

int main(void)
{
    debug_init(57600);
    debug_puts("\r\n=== A36SE FW v0.72 ===\r\nbootloader tag ");
    debug_write((const uint8_t *)0x08000300U, 7);
    debug_puts("\r\nGPIOA mode ");
    debug_hex32(GPIO_CTL(GPIOA));
    debug_puts(" out ");
    debug_hex32(GPIO_OCTL(GPIOA));
    debug_puts(" in ");
    debug_hex32(GPIO_ISTAT(GPIOA));
    debug_puts("\r\nGPIOF in ");
    debug_hex32(GPIO_ISTAT(GPIOF));
    debug_puts("\r\n");

    systick_init();
    __enable_irq();                 /* the startup code left interrupts masked */

    for (int i = 0; i < 3; i++)                 /* the bootloader already drives these */
        aux[i] = (GPIO_OCTL(GPIOA) & aux_pins[i]) != 0;
    board_init();
    lcd_init();
    keypad_init();
    bk4819_init();
    board_rf_path(0, RF_OFF);                   /* as stock before its first RX: REG_33 = 0 */
    debug_puts("BK4819 REG37 ");
    debug_hex16(bk4819_read(0x37));
    debug_puts(" REG48 ");
    debug_hex16(bk4819_read(0x48));
    debug_puts(" (expect 1D0F B3FF)\r\n");

    lcd_fill(0, 0, LCD_W, 13, C_BAR);
    gfx_text(4, 2, &font_6x10, "A36SE CFW v0.72 RX", C_TEXT, C_BAR);
    gfx_text(4, 16, &font_6x10, board_hwver2() ? "HWVER02" : "HW other", C_DIM, C_BG);
    gfx_text(4, 66, &font_6x10, "RSSI", C_DIM, C_BG);
    draw_settings();
    draw_aux();
    tune();
    debug_puts("BK4819 REG30 ");
    debug_hex16(bk4819_read(0x30));
    debug_puts(" (expect BFF1)\r\n");

    uint32_t next_scan = millis(), next_sq = millis(), next_show = millis(), next_beat = millis() + 5000;
    int rssi = -160;
    uint8_t shown_switch = 2;
    for (;;) {
        progmode_poll();
        fwdgt_feed();
        uint32_t now = millis();
        if ((int32_t)(now - next_scan) >= 0) {
            next_scan += KEY_SCAN_MS;
            keypad_scan();
            board_power_poll();
            uint8_t sw = gpio_input_bit_get(POWER_SENSE_PORT, POWER_SENSE_PIN) == SET;
            if (sw != shown_switch) {
                shown_switch = sw;
                field(64, 16, 60, sw ? "switch:off" : "switch:on", C_DIM);
            }
        }
        key_event_t ev;
        while (keypad_get(&ev))
            on_key(&ev);
        shell_poll();

        if ((int32_t)(now - next_sq) >= 0 && !tone_on) {
            next_sq = now + 50;
            rssi = bk4819_rssi_dbm();
            int want = monitor || rssi >= (open ? squelch - 3 : squelch);
            if (want != open)
                set_open(want);
        }
        if ((int32_t)(now - next_show) >= 0) {
            next_show += 250;
            draw_rssi(rssi);
        }
        if ((int32_t)(now - next_beat) >= 0) {
            char s[12];
            next_beat += 5000;
            dec(s, rssi);
            debug_puts("rssi ");
            debug_puts(s);
            debug_puts(open ? " open\r\n" : " closed\r\n");
        }
    }
}
