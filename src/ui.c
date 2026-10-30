/* User interface: dual-watch main screen (docs/ui/mockups.png, "B dual watch"), menu, about.
 *
 * Keys on the main screen: digits type a frequency (VFO) or channel number, MENU confirms, BACK
 * cancels. A long press runs the function printed on the key: 1 bandwidth, 2 squelch, 3 step,
 * 4 search, 6 scan, 8 channel/VFO, 9 FM/AM. Up/down tune, A/B switches slot, side key 1 is monitor
 * while held, side key 2 starts/stops the channel scan, holding FM locks the keys. During a scan
 * 0 locks the channel out and up/down move on.
 */
#include <string.h>
#include "ui.h"
#include "adc.h"
#include "board.h"
#include "bk4819.h"
#include "channels.h"
#include "debug.h"
#include "gfx.h"
#include "lcd.h"
#include "radio.h"
#include "scan.h"
#include "system.h"
#include "watch.h"

#define C_BG     RGB(8, 10, 18)
#define C_PANEL  RGB(26, 32, 48)
#define C_PANEL2 RGB(40, 48, 70)
#define C_TEXT   RGB(235, 238, 245)
#define C_DIM    RGB(125, 135, 155)
#define C_CYAN   RGB(0, 190, 255)
#define C_AMBER  RGB(255, 196, 40)
#define C_GREEN  RGB(40, 210, 100)
#define C_RED    RGB(235, 55, 55)

#define SLOT_Y(i)   (15 + (i) * 50)
#define SLOT_H      46
#define HINT_Y      117
#define TOAST_MS    1500

enum { SCR_MAIN, SCR_MENU, SCR_ABOUT };

static const uint32_t steps[] = {250, 500, 625, 1000, 1250, 2500, 10000, 100000};
static const char *const step_names[] = {"2.5k", "5k", "6.25k", "10k", "12.5k", "25k", "100k", "1M"};
#define STEP_COUNT ((int)(sizeof steps / sizeof steps[0]))
static const uint8_t backlight_s[] = {0, 10, 30, 60};       /* 0 = always on */
static const char *const backlight_names[] = {"Always", "10 s", "30 s", "60 s"};

static uint8_t screen, step = 4, backlight = 2, light_on = 1, locked, searching;
static uint32_t last_key, toast_until, next_meter, next_status;
static char toast[24];
static char entry[8];
static uint8_t entry_len;
static uint8_t key_long[KEY_COUNT];     /* the key's long press already acted: no digit on release */
static int8_t shown_level[2] = {-1, -1};

/* ---- small helpers ------------------------------------------------------------------------- */

static char *put_dec(char *d, uint32_t v)
{
    char t[11];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *d++ = t[--n];
    *d = 0;
    return d;
}

static char *put_str(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

static void show_toast(const char *s)
{
    strncpy(toast, s, sizeof toast - 1);
    toast_until = millis() + TOAST_MS;
}

static int rssi_level(void)                 /* 0-16 meter segments, -125..-45 dBm */
{
    int l = (radio_rssi_dbm() + 125) / 5;
    return l < 0 ? 0 : l > 16 ? 16 : l;
}

/* ---- main screen --------------------------------------------------------------------------- */

static void draw_status(void)
{
    char s[8], *p;
    lcd_fill(0, 0, LCD_W, 12, C_PANEL);
    int open = radio_open() && !searching;
    int bars = open ? (rssi_level() + 2) / 3 : 0;
    for (int i = 0; i < 5; i++) {
        int h = 2 + i * 2;
        lcd_fill(3 + i * 3, 11 - h, 2, h, i < bars ? C_GREEN : C_PANEL2);
    }
    int x = 22;
    if (watch_dual())
        x = gfx_text(x, 2, &font_5x8, "DW", C_CYAN, C_PANEL) + 4;
    if (searching)
        gfx_text(x, 2, &font_5x8, "SEARCH", C_AMBER, C_PANEL);
    else if (scan_mode() != SCAN_OFF)
        gfx_text(x, 2, &font_5x8, scan_lockouts() ? "SCAN L" : "SCAN", C_AMBER, C_PANEL);
    if (locked)
        gfx_icon(74, 2, &icon_lock, C_DIM, C_PANEL);

    int dv = battery_decivolts();
    p = put_dec(s, (uint32_t)dv / 10);
    *p++ = '.';
    p = put_dec(p, (uint32_t)dv % 10);
    put_str(p, "V");
    gfx_text(109 - gfx_text_width(&font_5x8, s), 2, &font_5x8, s, C_DIM, C_PANEL);
    int b = battery_bars();
    gfx_icon(111, 2, &icon_battery, C_DIM, C_PANEL);
    lcd_fill(113, 4, b * 2, 4, b <= 1 ? C_RED : C_GREEN);
}

static void draw_meter(int i, int level)
{
    int y = SLOT_Y(i) + 39;
    for (int k = 0; k < 16; k++)
        lcd_fill(6 + k * 7, y, 6, 4, k < level ? (k < 9 ? C_GREEN : k < 13 ? C_AMBER : C_RED) : C_PANEL2);
    shown_level[i] = (int8_t)level;
}

/* 476.91250 MHz -> "476.91" big, "25" small (a third small digit only when it is not 0) */
static void split_freq(uint32_t f, char *big, char *small)
{
    uint32_t mhz = f / 100000, rest = f % 100000;
    char *p = put_dec(big, mhz);
    *p++ = '.';
    *p++ = (char)('0' + rest / 10000);
    *p++ = (char)('0' + rest / 1000 % 10);
    *p = 0;
    small[0] = (char)('0' + rest / 100 % 10);
    small[1] = (char)('0' + rest / 10 % 10);
    small[2] = rest % 10 ? (char)('0' + rest % 10) : 0;
    small[3] = 0;
}

static void draw_entry(int y, uint16_t bg)
{
    char s[10];
    int n = 0;
    if (watch_slot(watch_selected())->is_channel) {
        for (int k = 0; k < 3; k++)
            s[n++] = k < entry_len ? entry[k] : '-';
        s[n] = 0;
        int x = gfx_text(6, y + 21, &font_5x8, "CH", C_DIM, bg) + 4;
        gfx_aa_text(x, y + 17, &font_digits21, s, C_TEXT, bg);
    } else {
        for (int k = 0; k < 7; k++) {
            if (k == 3)
                s[n++] = '.';
            s[n++] = k < entry_len ? entry[k] : '-';
        }
        s[n] = 0;
        gfx_aa_text(6, y + 17, &font_digits21, s, C_TEXT, bg);
    }
}

static void draw_slot(int i)
{
    slot_t *s = watch_slot(i);
    int y = SLOT_Y(i), sel = i == watch_selected();
    int rx = radio_open() && !searching && watch_rx_slot() == i && scan_mode() == SCAN_OFF;
    int scanning = sel && scan_mode() == SCAN_CH;
    uint16_t bg = sel ? C_PANEL : C_BG;
    char chip[8], big[8], small[4];

    if (sel)
        gfx_rrect(2, y, 124, SLOT_H, bg);
    else
        lcd_fill(2, y, 124, SLOT_H, bg);
    if (!sel)
        lcd_fill(2, y - 2, 124, 1, C_PANEL);

    const char *name = s->name;
    uint32_t f = s->freq;
    channel_t ch;
    if (scanning && channel_read(scan_channel(), &ch)) {
        put_dec(put_str(chip, "CH "), (uint32_t)scan_channel());
        name = ch.name;
        f = ch.rx;
    } else if (s->is_channel) {
        put_dec(put_str(chip, "CH "), s->channel);
    } else {
        put_str(chip, i ? "VFO B" : "VFO A");
        name = "";
    }
    uint16_t chip_bg = rx ? C_GREEN : sel ? C_CYAN : C_PANEL2;
    uint16_t chip_fg = rx || sel ? C_BG : C_TEXT;
    int cw = gfx_text_width(&font_6x10, chip) + 4;
    gfx_rrect(5, y + 3, cw, 12, chip_bg);
    gfx_text(7, y + 4, &font_6x10, chip, chip_fg, chip_bg);
    gfx_text(cw + 9, y + 2, &font_7x13b, name, sel ? C_TEXT : C_DIM, bg);
    const char *mode = s->am ? "AM" : s->narrow ? "N" : "W";
    gfx_text(122 - gfx_text_width(&font_5x8, mode), y + 5, &font_5x8, mode, C_DIM, bg);

    if (sel && entry_len) {
        draw_entry(y, bg);
    } else {
        split_freq(f, big, small);
        uint16_t c = sel ? C_AMBER : C_DIM;
        int x = gfx_aa_text(6, y + 17, &font_digits21, big, c, bg);
        gfx_aa_text(x + 1, y + 17, &font_digits12, small, c, bg);
        gfx_text(x + 2, y + 28, &font_5x8, "MHz", C_DIM, bg);
    }
    draw_meter(i, rx ? rssi_level() : 0);
}

static void draw_hint(void)
{
    const char *t;
    char s[24];
    lcd_fill(0, HINT_Y, LCD_W, LCD_H - HINT_Y, C_BG);
    if ((int32_t)(toast_until - millis()) > 0) {
        gfx_text(4, HINT_Y + 1, &font_6x10, toast, C_AMBER, C_BG);
        return;
    }
    if (entry_len)
        t = "MENU ok  BACK cancel";
    else if (searching)
        t = "searching... BACK stop";
    else if (scan_mode() != SCAN_OFF)
        t = scan_holding() ? "0 lock out  BACK stop" : "BACK stop  0 lock out";
    else if (radio_monitor())
        t = "monitor";
    else {
        put_str(put_str(put_dec(put_str(s, "SQ"), (uint32_t)radio_squelch()), "  "), step_names[step]);
        t = s;
    }
    gfx_text(4, HINT_Y + 1, &font_5x8, t, C_DIM, C_BG);
}

static void draw_main(void)
{
    lcd_fill(0, 12, LCD_W, LCD_H - 12, C_BG);
    draw_status();
    draw_slot(0);
    draw_slot(1);
    draw_hint();
}

/* ---- menu ---------------------------------------------------------------------------------- */

enum { M_SQUELCH, M_STEP, M_BANDWIDTH, M_MODULATION, M_DUAL, M_BACKLIGHT, M_SCAN, M_ABOUT, M_COUNT };
static const char *const menu_names[M_COUNT] = {
    "Squelch", "Step", "Bandwidth", "Modulation", "Dual watch", "Backlight", "Scan channels", "About",
};
static uint8_t menu_pos, editing;

static void menu_value(int item, char *s)
{
    slot_t *sl = watch_slot(watch_selected());
    s[0] = 0;
    switch (item) {
    case M_SQUELCH:    put_dec(s, (uint32_t)radio_squelch()); break;
    case M_STEP:       put_str(s, step_names[step]); break;
    case M_BANDWIDTH:  put_str(s, sl->is_channel ? "channel" : sl->vfo_narrow ? "Narrow" : "Wide"); break;
    case M_MODULATION: put_str(s, sl->is_channel ? "channel" : sl->vfo_am ? "AM" : "FM"); break;
    case M_DUAL:       put_str(s, watch_dual() ? "On" : "Off"); break;
    case M_BACKLIGHT:  put_str(s, backlight_names[backlight]); break;
    default: break;
    }
}

static void draw_menu_row(int i)
{
    int y = 16 + i * 13, sel = i == menu_pos;
    uint16_t bg = sel ? C_PANEL2 : C_BG;
    char v[12];
    if (sel)
        gfx_rrect(2, y - 1, 124, 13, bg);
    else
        lcd_fill(2, y - 1, 124, 13, bg);
    gfx_text(6, y + 1, &font_6x10, menu_names[i], C_TEXT, bg);
    menu_value(i, v);
    gfx_text(122 - gfx_text_width(&font_6x10, v), y + 1, &font_6x10, v,
             sel && editing ? C_AMBER : C_DIM, bg);
}

static void draw_menu_header(void)
{
    char s[8];
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "Menu", C_TEXT, C_PANEL);
    put_dec(put_str(put_dec(s, menu_pos + 1u), "/"), M_COUNT);
    gfx_text(124 - gfx_text_width(&font_5x8, s), 4, &font_5x8, s, C_DIM, C_PANEL);
}

static void draw_menu(void)
{
    draw_menu_header();
    lcd_fill(0, 14, LCD_W, LCD_H - 14, C_BG);
    for (int i = 0; i < M_COUNT; i++)
        draw_menu_row(i);
}

static void draw_about(void)
{
    char s[24], *p;
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "About", C_TEXT, C_PANEL);
    lcd_fill(0, 14, LCD_W, LCD_H - 14, C_BG);
    gfx_text(4, 20, &font_6x10, "A36SE CFW v0.9", C_TEXT, C_BG);
    gfx_text(4, 34, &font_5x8, "MCU   AT32F421", C_DIM, C_BG);
    gfx_text(4, 46, &font_5x8, board_hwver2() ? "Board HWVER02" : "Board HW other", C_DIM, C_BG);
    p = put_str(s, "Batt  ");
    int dv = battery_decivolts();
    p = put_dec(p, (uint32_t)dv / 10);
    *p++ = '.';
    put_str(put_dec(p, (uint32_t)dv % 10), " V");
    gfx_text(4, 58, &font_5x8, s, C_DIM, C_BG);
    channel_t ch;
    int n = 0;
    for (int c = 1; c <= CHANNEL_COUNT; c++)
        n += channel_read(c, &ch);
    put_dec(put_str(s, "Chan  "), (uint32_t)n);
    gfx_text(4, 70, &font_5x8, s, C_DIM, C_BG);
    gfx_text(4, 112, &font_5x8, "BACK to return", C_DIM, C_BG);
}

static void redraw(void)
{
    if (screen == SCR_MAIN)
        draw_main();
    else if (screen == SCR_MENU)
        draw_menu();
    else
        draw_about();
}

/* ---- actions ------------------------------------------------------------------------------- */

static void apply_backlight(int on)
{
    light_on = (uint8_t)on;
    lcd_backlight(on);
}

static void stop_activity(void)
{
    int sel = watch_selected();
    if (searching) {
        searching = 0;
        bk4819_search(0);
        watch_pause(0);
    }
    if (scan_mode() != SCAN_OFF) {
        slot_t *s = watch_slot(sel);
        scan_stop();
        if (scan_channel()) {
            s->is_channel = 1;
            s->channel = (uint16_t)scan_channel();
        }
        watch_pause(0);
        watch_update(sel);
    }
    radio_set_monitor(0);
}

static void tune_step(int dir)
{
    int sel = watch_selected();
    slot_t *s = watch_slot(sel);
    if (s->is_channel) {
        int n = channel_next(s->channel, dir, 0);
        if (n)
            s->channel = (uint16_t)n;
    } else {
        uint32_t st = steps[step];
        s->vfo = (s->vfo / st) * st;
        s->vfo = dir > 0 ? s->vfo + st : s->vfo - st;
        if (s->vfo < 1800000) s->vfo = 1800000;
        if (s->vfo > 130000000) s->vfo = 130000000;
    }
    watch_update(sel);
    draw_slot(sel);
}

static void commit_entry(void)
{
    int sel = watch_selected();
    slot_t *s = watch_slot(sel);
    uint32_t v = 0;
    for (int i = 0; i < entry_len; i++)
        v = v * 10 + (uint32_t)(entry[i] - '0');
    if (s->is_channel) {
        channel_t ch;
        if (channel_read((int)v, &ch))
            s->channel = (uint16_t)v;
        else
            show_toast("no such channel");
    } else {
        for (int i = entry_len; i < 7; i++)
            v *= 10;                        /* typed digits are MHz then fractions */
        v *= 10;                            /* 100 Hz -> 10 Hz units */
        if (v >= 1800000 && v <= 130000000)
            s->vfo = v;
        else
            show_toast("out of range");
    }
    entry_len = 0;
    watch_update(sel);
}

static void start_scan(void)
{
    slot_t *s = watch_slot(watch_selected());
    stop_activity();
    watch_pause(1);
    scan_start_ch(s->is_channel ? s->channel : 0, 1);
    if (scan_mode() == SCAN_OFF) {
        watch_pause(0);
        show_toast("no scan channels");
    }
}

static void long_press(int key)
{
    int sel = watch_selected();
    slot_t *s = watch_slot(sel);
    char t[24];
    switch (key) {
    case KEY_1:
        if (s->is_channel) {
            show_toast("channel: fixed");
            break;
        }
        s->vfo_narrow ^= 1;
        watch_update(sel);
        show_toast(s->vfo_narrow ? "Narrow" : "Wide");
        break;
    case KEY_2:
        radio_set_squelch((radio_squelch() + 1) % 10);
        put_dec(put_str(t, "Squelch "), (uint32_t)radio_squelch());
        show_toast(t);
        break;
    case KEY_3:
        step = (uint8_t)((step + 1) % STEP_COUNT);
        put_str(put_str(t, "Step "), step_names[step]);
        show_toast(t);
        break;
    case KEY_4:
        if (s->is_channel) {
            s->is_channel = 0;
            s->vfo = s->freq;
            watch_update(sel);
        }
        stop_activity();
        watch_pause(1);
        searching = 1;
        radio_search_start();
        break;
    case KEY_6:
        if (scan_mode() != SCAN_OFF)
            stop_activity();
        else
            start_scan();
        break;
    case KEY_8:
        if (!s->is_channel) {
            s->is_channel = 1;
            if (!s->channel)
                s->channel = (uint16_t)channel_next(0, 1, 0);
        } else {
            s->is_channel = 0;
            s->vfo = s->freq;
            s->vfo_am = s->am;
            s->vfo_narrow = s->narrow;
        }
        watch_update(sel);
        break;
    case KEY_9:
        if (s->is_channel) {
            show_toast("channel: fixed");
            break;
        }
        s->vfo_am ^= 1;
        watch_update(sel);
        show_toast(s->vfo_am ? "AM" : "FM");
        break;
    case KEY_FM:
        locked ^= 1;
        show_toast(locked ? "Keys locked" : "Keys unlocked");
        break;
    default:
        return;
    }
    draw_main();
}

static int digit_of(int key)
{
    switch (key) {
    case KEY_0: return 0;
    case KEY_1: return 1;
    case KEY_2: return 2;
    case KEY_3: return 3;
    case KEY_4: return 4;
    case KEY_5: return 5;
    case KEY_6: return 6;
    case KEY_7: return 7;
    case KEY_8: return 8;
    case KEY_9: return 9;
    default:    return -1;
    }
}

static void key_main(const key_event_t *ev)
{
    int sel = watch_selected();
    int busy = searching || scan_mode() != SCAN_OFF;

    if (ev->key == KEY_SIDE1) {             /* monitor while held */
        if (ev->type == KEV_PRESS || ev->type == KEV_RELEASE) {
            radio_set_monitor(ev->type == KEV_PRESS);
            draw_main();
        }
        return;
    }
    if (ev->type == KEV_LONG) {
        key_long[ev->key] = 1;
        if (!busy || ev->key == KEY_6)
            long_press(ev->key);
        return;
    }
    if (ev->key == KEY_UP || ev->key == KEY_DOWN) {
        int dir = ev->key == KEY_UP ? 1 : -1;
        if (ev->type != KEV_PRESS && ev->type != KEV_REPEAT)
            return;
        if (scan_mode() != SCAN_OFF) {
            if (ev->type == KEV_PRESS) {
                scan_skip(dir);
                draw_slot(sel);
            }
        } else if (!searching && !entry_len) {
            tune_step(dir);
        }
        return;
    }
    if (digit_of(ev->key) >= 0) {
        if (ev->type == KEV_PRESS)
            key_long[ev->key] = 0;
        if (ev->type != KEV_RELEASE || key_long[ev->key])
            return;
        if (scan_mode() != SCAN_OFF) {
            if (ev->key == KEY_0) {
                scan_lockout();
                show_toast("locked out");
                draw_main();
            }
            return;
        }
        if (busy)
            return;
        int max = watch_slot(sel)->is_channel ? 3 : 7;
        if (entry_len < max)
            entry[entry_len++] = (char)('0' + digit_of(ev->key));
        if (entry_len == max)
            commit_entry();
        draw_slot(sel);
        draw_hint();
        return;
    }
    if (ev->type != KEV_PRESS)
        return;
    switch (ev->key) {
    case KEY_AB:
        if (busy || entry_len)
            break;
        watch_select(sel ^ 1);
        draw_main();
        break;
    case KEY_MENU:
        if (entry_len) {
            commit_entry();
            draw_main();
        } else if (!busy) {
            screen = SCR_MENU;
            editing = 0;
            draw_menu();
        }
        break;
    case KEY_BACK:
        entry_len = 0;
        stop_activity();
        draw_main();
        break;
    case KEY_SIDE2:
        if (scan_mode() != SCAN_OFF)
            stop_activity();
        else
            start_scan();
        draw_main();
        break;
    case KEY_PTT:
        show_toast("TX not built yet");
        draw_hint();
        break;
    default:
        break;
    }
}

static void menu_change(int dir)
{
    slot_t *s = watch_slot(watch_selected());
    switch (menu_pos) {
    case M_SQUELCH:
        radio_set_squelch((radio_squelch() + 10 + dir) % 10);
        break;
    case M_STEP:
        step = (uint8_t)((step + STEP_COUNT + dir) % STEP_COUNT);
        break;
    case M_BANDWIDTH:
        if (!s->is_channel) {
            s->vfo_narrow ^= 1;
            watch_update(watch_selected());
        }
        break;
    case M_MODULATION:
        if (!s->is_channel) {
            s->vfo_am ^= 1;
            watch_update(watch_selected());
        }
        break;
    case M_DUAL:
        watch_set_dual(!watch_dual());
        break;
    case M_BACKLIGHT:
        backlight = (uint8_t)((backlight + 4 + dir) % 4);
        break;
    default:
        break;
    }
}

static void key_menu(const key_event_t *ev)
{
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    int old = menu_pos;
    switch (ev->key) {
    case KEY_UP:
    case KEY_DOWN: {
        int dir = ev->key == KEY_DOWN ? 1 : -1;
        if (editing) {
            menu_change(ev->key == KEY_UP ? 1 : -1);
            draw_menu_row(menu_pos);
        } else {
            menu_pos = (uint8_t)((menu_pos + M_COUNT + dir) % M_COUNT);
            draw_menu_row(old);
            draw_menu_row(menu_pos);
            draw_menu_header();
        }
        break;
    }
    case KEY_MENU:
        if (menu_pos == M_SCAN) {
            screen = SCR_MAIN;
            start_scan();
            draw_main();
        } else if (menu_pos == M_ABOUT) {
            screen = SCR_ABOUT;
            draw_about();
        } else {
            editing ^= 1;
            draw_menu_row(menu_pos);
        }
        break;
    case KEY_BACK:
        if (editing) {
            editing = 0;
            draw_menu_row(menu_pos);
        } else {
            screen = SCR_MAIN;
            draw_main();
        }
        break;
    default:
        break;
    }
}

/* ---- entry points -------------------------------------------------------------------------- */

void ui_init(void)
{
    if (radio_squelch() == 0)
        radio_set_squelch(1);           /* 0 is always open, which leaves dual watch on slot A */
    watch_init();
    last_key = millis();
    apply_backlight(1);
    draw_main();
}

void ui_refresh(void)
{
    redraw();
}

void ui_key(const key_event_t *ev)
{
    if (ev->type == KEV_PRESS) {
        debug_puts("key ");
        debug_puts(key_name(ev->key));
        debug_puts("\r\n");
    }
    last_key = millis();
    if (!light_on) {                        /* the first key only wakes the display */
        apply_backlight(1);
        if (ev->type == KEV_PRESS)
            key_long[ev->key] = 1;          /* and must not type a digit on release */
        return;
    }
    if (locked && !(ev->key == KEY_FM && ev->type == KEV_LONG) && ev->key != KEY_SIDE1) {
        if (ev->type == KEV_PRESS && screen == SCR_MAIN) {
            show_toast("locked: hold FM");
            draw_hint();
        }
        return;
    }
    if (screen == SCR_MAIN)
        key_main(ev);
    else if (screen == SCR_MENU)
        key_menu(ev);
    else if (ev->type == KEV_PRESS && (ev->key == KEY_BACK || ev->key == KEY_MENU)) {
        screen = SCR_MENU;
        draw_menu();
    }
}

void ui_tick(void)
{
    uint32_t now = millis();
    int ev = WATCH_EV_NONE;

    if (searching) {
        uint32_t f;
        if (radio_search_poll(&f)) {
            int sel = watch_selected();
            searching = 0;
            bk4819_search(0);
            watch_slot(sel)->is_channel = 0;
            watch_slot(sel)->vfo = f;
            watch_pause(0);
            watch_update(sel);
            show_toast("found");
            if (screen == SCR_MAIN)
                draw_main();
        }
    } else if (scan_mode() != SCAN_OFF) {
        int e = scan_poll();
        if (e != SCAN_EV_NONE && screen == SCR_MAIN) {
            draw_slot(watch_selected());
            draw_hint();
        }
        if (e == SCAN_EV_HOLD || e == SCAN_EV_RESUME)
            ev = e == SCAN_EV_HOLD ? WATCH_EV_OPEN : WATCH_EV_CLOSE;
    } else {
        ev = watch_poll();
        if (ev != WATCH_EV_NONE && screen == SCR_MAIN) {
            draw_slot(watch_rx_slot());
            draw_status();
        }
    }
    if (ev == WATCH_EV_OPEN)
        apply_backlight(1), last_key = now;

    if (screen == SCR_MAIN && (int32_t)(now - next_meter) >= 0) {
        next_meter = now + 200;
        int i = watch_rx_slot(), level = radio_open() && !searching ? rssi_level() : 0;
        if (scan_mode() != SCAN_OFF)
            i = watch_selected();
        if (level != shown_level[i])
            draw_meter(i, level);
        if (toast_until && (int32_t)(now - toast_until) >= 0) {
            toast_until = 0;
            draw_hint();
        }
    }
    if (screen == SCR_MAIN && (int32_t)(now - next_status) >= 0) {
        next_status = now + 5000;
        draw_status();
    }
    if (backlight_s[backlight] && light_on && !radio_open() &&
        now - last_key > (uint32_t)backlight_s[backlight] * 1000)
        apply_backlight(0);
}
