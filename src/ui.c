/* User interface: dual-watch main screen (docs/ui/mockups.png, "B dual watch"), menu, messages,
 * about.
 *
 * Keys on the main screen: digits type a frequency (VFO) or channel number, MENU confirms, BACK
 * cancels. Long presses: 1 TX power high/low, 2 squelch, 3 step, 4 search, 5 messages, 6 scan,
 * 7 APRS, 8 pager, MENU channel/VFO, FM key lock; a short FM press switches FM/AM. Up/down tune, A/B switches slot, side key 1 is
 * monitor while held, side key 2 starts/stops the channel scan. During a scan 0 locks the channel
 * out and up/down move on. MENU and FM act on release, so a long press does not also open the menu
 * or switch the modulation.
 */
#include <stddef.h>
#include <string.h>
#include "ui.h"
#include "adc.h"
#include "board.h"
#include "bk4819.h"
#include "bk1080.h"
#include "channels.h"
#include "debug.h"
#include "gfx.h"
#include "lcd.h"
#include "radio.h"
#include "scan.h"
#include "system.h"
#include "watch.h"
#include "settings.h"
#include "voice.h"
#include "tx.h"
#include "msg.h"
#include "aprs.h"
#include "pocsag.h"
#include "roll.h"
#include "stock.h"
#include "version.h"

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

enum { SCR_MAIN, SCR_MENU, SCR_ABOUT, SCR_MSG, SCR_APRS, SCR_EDIT, SCR_PAGER, SCR_FSCAN, SCR_FM };

static const uint32_t steps[] = {250, 500, 625, 1000, 1250, 2500, 10000, 100000};
static const char *const step_names[] = {"2.5k", "5k", "6.25k", "10k", "12.5k", "25k", "100k", "1M"};
#define STEP_COUNT ((int)(sizeof steps / sizeof steps[0]))
static const uint8_t backlight_s[] = {0, 5, 10, 15, 20, 30, 60, 120, 180};  /* as stock; 0 = always on */
static const char *const backlight_names[] = {"Always", "5 s", "10 s", "15 s", "20 s", "30 s", "1 min",
                                              "2 min", "3 min"};

static uint8_t screen, step = 4, light_on = 1, locked, searching;
static const uint16_t tot_s[] = {0, 30, 60, 120, 240, 480};     /* TX time-out, as stock; 0 = none */
static const char *const tot_names[] = {"Off", "30 s", "60 s", "2 min", "4 min", "8 min"};
static uint8_t tx_timed_out;
static uint32_t next_tx_draw;
static uint32_t next_battery_warn;
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

/* key beeps (149, 151, 156) and spoken prompts / the power-on sound each have a setting */
static void snd(int id)
{
    if (tx_active())
        return;                         /* never a sound while transmitting */
    int keyish = id == SND_KEY || id == SND_CANCEL || id == SND_OPEN;
    if (keyish ? stock.beep : stock.voice)
        voice_play(id);
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
    if (tx_active()) {
        gfx_rrect(2, 1, 16, 10, C_RED);
        gfx_text(4, 2, &font_5x8, "TX", C_TEXT, C_RED);
    } else {
        for (int i = 0; i < 5; i++) {
            int h = 2 + i * 2;
            lcd_fill(3 + i * 3, 11 - h, 2, h, i < bars ? C_GREEN : C_PANEL2);
        }
    }
    int x = 22;
    if (watch_dual())
        x = gfx_text(x, 2, &font_5x8, "DW", C_CYAN, C_PANEL) + 4;
    if (searching)
        gfx_text(x, 2, &font_5x8, "SEARCH", C_AMBER, C_PANEL);
    else if (scan_mode() != SCAN_OFF)
        gfx_text(x, 2, &font_5x8, scan_lockouts() ? "SCAN L" : "SCAN", C_AMBER, C_PANEL);
    if (locked)
        gfx_icon(67, 2, &icon_lock, C_DIM, C_PANEL);
    if (msg_unread())
        gfx_icon(76, 2, &icon_envelope, C_AMBER, C_PANEL);

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
    int tx = tx_active() && i == watch_selected();
    for (int k = 0; k < 16; k++)
        lcd_fill(6 + k * 7, y, 6, 4, k >= level ? C_PANEL2 : tx ? C_RED :
                 k < 9 ? C_GREEN : k < 13 ? C_AMBER : C_RED);
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
    int scanning = sel && scan_mode() == SCAN_CH, fscan = sel && scan_mode() == SCAN_VFO;
    if (fscan && scan_holding())
        rx = 1;
    uint16_t bg = sel ? C_PANEL : C_BG;
    char chip[8], big[8], small[4];

    if (sel)
        gfx_rrect(2, y, 124, SLOT_H, bg);
    else
        lcd_fill(2, y, 124, SLOT_H, bg);
    if (!sel)
        lcd_fill(2, y - 2, 124, 1, C_PANEL);

    const char *name = s->name;
    uint32_t f = sel && tx_active() && s->tx ? s->tx : s->freq;   /* while transmitting: where to */
    channel_t ch;
    if (scanning && channel_read(scan_channel(), &ch)) {
        put_dec(put_str(chip, "CH "), (uint32_t)scan_channel());
        name = ch.name;
        f = ch.rx;
    } else if (fscan) {
        put_str(chip, "SCAN");
        name = "";
        f = radio_freq();
    } else if (s->is_channel) {
        put_dec(put_str(chip, "CH "), s->channel);
    } else {
        put_str(chip, i ? "VFO B" : "VFO A");
        name = "";
    }
    int tx = sel && tx_active();
    int shows = s->is_channel && !tx && !fscan ? (i ? stock.mdfb : stock.mdfa) : 1;   /* 0 name, 1 frequency, 2 number */
    const char *big_name = 0;
    char top[10];
    if (shows == 0 && name[0]) {            /* the name goes big, the frequency takes its place */
        big_name = name;
        split_freq(f, big, small);
        put_str(put_str(top, big), small);
        name = top;
    }
    if (tx)
        put_str(chip, "TX");
    uint16_t chip_bg = tx ? C_RED : rx ? C_GREEN : sel ? C_CYAN : C_PANEL2;
    uint16_t chip_fg = tx ? C_TEXT : rx || sel ? C_BG : C_TEXT;
    int cw = gfx_text_width(&font_6x10, chip) + 4;
    gfx_rrect(5, y + 3, cw, 12, chip_bg);
    gfx_text(7, y + 4, &font_6x10, chip, chip_fg, chip_bg);
    char mode[16], fit[13], *mp = mode;
    if (!s->tx)
        mp = put_str(mp, "X ");                             /* transmit off */
    else if (s->tx != s->freq)
        mp = put_str(mp, s->tx > s->freq ? "+ " : "- ");    /* repeater shift / split */
    mp = put_str(put_str(mp, (fscan ? channel_is_am(f) : s->am) ? "AM " : s->narrow ? "N " : "W "),
                 s->low_power ? "L" : "H");
    if (s->rx_tone.type != TONE_NONE || s->tx_tone.type != TONE_NONE)
        mp = put_str(mp, s->rx_tone.type == TONE_ENCRY || s->tx_tone.type == TONE_ENCRY ? " E"
                         : s->rx_tone.type == TONE_CTCSS || s->tx_tone.type == TONE_CTCSS ? " T" : " D");
    if (s->scramble)
        put_str(mp, s->scramble == BK_SCRAMBLE_ROLL ? " R" : " S");
    int mx = 122 - gfx_text_width(&font_5x8, mode);
    int room = mx - 3 - (cw + 9);                           /* the name stops short of the marks */
    const font_t *nf = gfx_text_width(&font_7x13b, name) <= room ? &font_7x13b : &font_6x10;
    int n = 0;
    while (n < room / nf->w && n < 12 && name[n]) {
        fit[n] = name[n];
        n++;
    }
    fit[n] = 0;
    gfx_text(cw + 9, y + (nf == &font_7x13b ? 2 : 4), nf, fit, sel ? C_TEXT : C_DIM, bg);
    gfx_text(mx, y + 5, &font_5x8, mode, C_DIM, bg);

    if (sel && entry_len) {
        draw_entry(y, bg);
    } else if (big_name) {
        gfx_text(6, y + 21, &font_7x13b, big_name, sel ? C_AMBER : C_DIM, bg);
    } else if (shows == 2) {                /* the channel number instead of the frequency */
        put_dec(big, s->channel);
        gfx_aa_text(6, y + 17, &font_digits21, big, sel ? C_AMBER : C_DIM, bg);
    } else {
        split_freq(f, big, small);
        uint16_t c = sel ? C_AMBER : C_DIM;
        int x = gfx_aa_text(6, y + 17, &font_digits21, big, c, bg);
        gfx_aa_text(x + 1, y + 17, &font_digits12, small, c, bg);
        gfx_text(x + 2, y + 28, &font_5x8, "MHz", C_DIM, bg);
    }
    draw_meter(i, tx ? (s->low_power ? 6 : 16) : rx ? rssi_level() : 0);
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
    if (tx_timed_out) {
        gfx_text(4, HINT_Y + 1, &font_5x8, "TX time-out: let go PTT", C_RED, C_BG);
        return;
    }
    if (tx_active()) {
        uint32_t sec = tx_seconds();
        char *p = put_str(s, "TX ");
        p = put_dec(p, sec / 60);
        *p++ = ':';
        *p++ = (char)('0' + sec % 60 / 10);
        *p++ = (char)('0' + sec % 10);
        put_str(put_str(p, "   time-out "), tot_names[stock.tot]);
        gfx_text(4, HINT_Y + 1, &font_5x8, s, C_RED, C_BG);
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

/* stock's settings with a plain list of values: stock_items[], from M_STOCK on */
typedef struct {
    const char *name;
    uint8_t off, count;
    const char *const *opt;
    const uint8_t *vals;                /* stored values, when they aren't 0, 1, 2... */
} stock_item_t;

#define OFFS(field) ((uint8_t)offsetof(stock_t, field))
static const char *const off_on[] = {"Off", "On"};
static const char *const save_names[] = {"Off", "Normal", "Super", "Deep"};
static const char *const vox_names[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
static const char *const voxd_names[] = {"0.5 s", "0.6 s", "0.7 s", "0.8 s", "0.9 s", "1.0 s", "1.1 s", "1.2 s",
                                         "1.3 s", "1.4 s", "1.5 s", "1.6 s", "1.7 s", "1.8 s", "1.9 s", "2.0 s"};
static const char *const pttlt_names[] = {"None", "100 ms", "200 ms", "400 ms", "600 ms", "800 ms", "1 s"};
static const char *const alarm_names[] = {"Local", "Tone", "Code"};
static const char *const screv_names[] = {"Time", "Carrier", "Stop"};
static const char *const mdf_names[] = {"Name", "Frequency", "Channel"};
static const char *const autolk_names[] = {"Off", "5 s", "10 s", "15 s"};
static const char *const dualtx_names[] = {"Selected", "A", "B"};
static const char *const rp_names[] = {"Off", "100 ms", "200 ms", "300 ms", "400 ms", "500 ms", "600 ms",
                                       "700 ms", "800 ms", "900 ms", "1 s"};
static const char *const roger_names[] = {"Off", "Beep", "Tone1200"};
static const char *const ponmsg_names[] = {"Logo", "Voltage"};
static const char *const tone_names[] = {"1000 Hz", "1450 Hz", "1750 Hz", "2100 Hz"};
static const char *const quit_names[] = {"5 s", "10 s", "15 s", "20 s", "25 s", "30 s", "35 s", "40 s",
                                         "45 s", "50 s", "60 s"};
static const uint8_t quit_s[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 60};
enum { FN_PTT_B = 0x01, FN_SOS = 0x03, FN_MONITOR = 0x05, FN_FM = 0x07, FN_LIGHT = 0x08, FN_TXP = 0x0A,
       FN_NOAA = 0x0C, FN_SCAN = 0x1C, FN_SEARCH = 0x1D, FN_REMOTE = 0x23, FN_DTMF = 0x2A, FN_REVERSE = 0x2D,
       FN_OFF = 0xFF };
static const uint8_t fn_vals[] = {FN_OFF, FN_FM, FN_TXP, FN_SCAN, FN_SEARCH, FN_LIGHT, FN_NOAA, FN_MONITOR,
                                  FN_PTT_B, FN_SOS, FN_DTMF, FN_REVERSE, FN_REMOTE};
static const char *const fn_names[] = {"Off", "FM radio", "TX power", "Scan", "Search", "Light", "NOAA",
                                       "Monitor", "PTT B", "SOS", "DTMF", "Reverse", "Remote"};
#define FN_ITEM(name, field) {name, OFFS(field), 13, fn_names, fn_vals}
static const stock_item_t stock_items[] = {
    {"Battery save", OFFS(save), 4, save_names, 0},
    {"VOX", OFFS(vox), 11, vox_names, 0},
    {"VOX delay", OFFS(voxd), 16, voxd_names, 0},
    {"PTT-ID delay", OFFS(pttlt), 7, pttlt_names, 0},
    {"Alarm", OFFS(almode), 3, alarm_names, 0},
    {"Alarm sound", OFFS(alarmsound), 2, off_on, 0},
    {"Scan resume", OFFS(screv), 3, screv_names, 0},
    {"Slot A shows", OFFS(mdfa), 3, mdf_names, 0},
    {"Slot B shows", OFFS(mdfb), 3, mdf_names, 0},
    {"Auto lock", OFFS(autolk), 4, autolk_names, 0},
    {"TX slot", OFFS(dualtx), 3, dualtx_names, 0},
    {"Roger", OFFS(roger), 3, roger_names, 0},
    {"Tail cut", OFFS(ste), 2, off_on, 0},
    {"Rpt tail cut", OFFS(rpste), 11, rp_names, 0},
    {"Rpt tail wait", OFFS(rptrl), 11, rp_names, 0},
    {"Tone burst", OFFS(tone), 4, tone_names, 0},
    {"Power-on", OFFS(ponmsg), 2, ponmsg_names, 0},
    {"Menu exit", OFFS(menuquit), 11, quit_names, 0},
    FN_ITEM("Side 2", skey2_sp),
    FN_ITEM("Side 2 long", skey2_lp),
    FN_ITEM("Side 1", skey3_sp),
    FN_ITEM("Top key", topkey_sp),
};
#define STOCK_ITEMS 22

enum { M_FSCAN, M_MESSAGES, M_APRS, M_PAGER, M_SQUELCH, M_STEP, M_BANDWIDTH, M_MODULATION, M_DUAL,
       M_BACKLIGHT, M_TOT, M_MSG_SPEED, M_MSG_RECEIPTS, M_APRS_CALL, M_APRS_PATH, M_APRS_TEXT,
       M_PAGER_RIC, M_PAGER_SPEED, M_PAGER_INVERT, M_ROLL_KEY, M_KEYBEEP, M_VOICE, M_SCAN,
       M_STOCK, M_ABOUT = M_STOCK + STOCK_ITEMS, M_COUNT };
static const char *const menu_names[M_STOCK] = {
    "Freq scanner", "Messages", "APRS", "Pager", "Squelch", "Step", "Bandwidth", "Modulation", "Dual watch",
    "Backlight", "TX timeout", "Msg speed", "Msg receipts", "APRS call", "APRS path", "APRS beacon",
    "Pager RIC", "Pager speed", "Pager invert", "Scramble key", "Key beep", "Voice", "Scan channels",
};

static const char *menu_name(int i)
{
    return i < M_STOCK ? menu_names[i] : i < M_ABOUT ? stock_items[i - M_STOCK].name : "About";
}

static int stock_index(const stock_item_t *it)
{
    uint8_t v = ((const uint8_t *)&stock)[it->off];
    if (!it->vals)
        return v < it->count ? v : 0;
    for (int i = 0; i < it->count; i++)
        if (it->vals[i] == v)
            return i;
    return 0;
}

static void stock_change(const stock_item_t *it, int dir)
{
    int i = (stock_index(it) + it->count + dir) % it->count;
    ((uint8_t *)&stock)[it->off] = it->vals ? it->vals[i] : (uint8_t)i;
}
static const char *const msg_speed_names[] = {"1200", "700", "450"};
#define MENU_ROWS 8                     /* rows on screen; the list scrolls */
static uint8_t menu_pos, menu_top, editing;

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
    case M_BACKLIGHT:  put_str(s, backlight_names[stock.abr]); break;
    case M_TOT:        put_str(s, tot_names[stock.tot]); break;
    case M_MESSAGES:   if (msg_unread()) put_str(put_dec(s, (uint32_t)msg_unread()), " new"); break;
    case M_MSG_SPEED:  put_str(s, msg_speed_names[msg_modem()]); break;
    case M_MSG_RECEIPTS: put_str(s, msg_receipts() ? "On" : "Off"); break;
    case M_APRS:       if (aprs_unread()) put_str(put_dec(s, (uint32_t)aprs_unread()), " new"); break;
    case M_APRS_CALL:  put_str(s, aprs_cfg.call[0] ? aprs_cfg.call : "not set"); break;
    case M_APRS_PATH:  put_str(s, aprs_path_names[aprs_cfg.path & 3]); break;
    case M_APRS_TEXT:
        memcpy(s, aprs_cfg.text, 9);
        s[9] = 0;
        break;
    case M_PAGER_RIC:  put_dec(s, pager_cfg.ric); break;
    case M_PAGER_SPEED: put_dec(s, pager_bauds[pager_cfg.speed]); break;
    case M_PAGER_INVERT: put_str(s, pager_cfg.invert ? "On" : "Off"); break;
    case M_ROLL_KEY:   put_str(s, roll_cfg.key[0] ? "set" : "not set"); break;
    case M_KEYBEEP:    put_str(s, stock.beep ? "On" : "Off"); break;
    case M_VOICE:      put_str(s, stock.voice ? "On" : "Off"); break;
    default:
        if (item >= M_STOCK && item < M_ABOUT)
            put_str(s, stock_items[item - M_STOCK].opt[stock_index(&stock_items[item - M_STOCK])]);
        break;
    }
}

static void draw_menu_row(int i)
{
    if (i < menu_top || i >= menu_top + MENU_ROWS)
        return;
    int y = 16 + (i - menu_top) * 13, sel = i == menu_pos;
    uint16_t bg = sel ? C_PANEL2 : C_BG;
    char v[16];
    if (sel)
        gfx_rrect(2, y - 1, 124, 13, bg);
    else
        lcd_fill(2, y - 1, 124, 13, bg);
    gfx_text(6, y + 1, &font_6x10, menu_name(i), C_TEXT, bg);
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
    gfx_text(4, 20, &font_6x10, "Super FW v" FW_VERSION, C_TEXT, C_BG);
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

/* ---- messages screen ----------------------------------------------------------------------- */

/* History above (newest at the bottom: "<" received, ">" sent, a green "+" once the other radio sent
   its receipt), the message being typed below. Typing as on a phone: a key's letters, then its
   digit, cycle while it is pressed again within TAP_MS; holding a key types its digit; 0 is a space.
   FM switches ABC / abc, MENU sends, BACK deletes and leaves when nothing is left, up/down scroll. */
#define TAP_MS     1000
#define MSG_LINE   21                   /* font_6x10 characters on a line */
#define MSG_BOTTOM 97                   /* the history ends here, the typing box starts */
static const char *const key_chars[10] = {
    " 0", ".,?!-'/@1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
};
static char comp[MSG_TEXT + 1];
static uint8_t comp_len, tap_key = 0xFF, tap_n, tap_shown, lower, msg_scroll;
static uint32_t tap_at;

static int tap_pending(void)
{
    return tap_key != 0xFF && millis() - tap_at < TAP_MS;
}

static void draw_msg_header(void)
{
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "Messages", C_TEXT, C_PANEL);
    const char *r = msg_receiving() ? "receiving" : msg_speed_names[msg_modem()];
    gfx_text(124 - gfx_text_width(&font_5x8, r), 4, &font_5x8, r, msg_receiving() ? C_GREEN : C_DIM,
             C_PANEL);
}

static void draw_msg_history(void)
{
    char full[34], line[MSG_LINE + 1];
    int y = MSG_BOTTOM;
    lcd_fill(0, 14, LCD_W, MSG_BOTTOM - 14, C_BG);
    if (!msg_count()) {
        gfx_text(4, 30, &font_6x10, "No messages yet.", C_DIM, C_BG);
        gfx_text(4, 46, &font_5x8, "Type below, MENU sends", C_DIM, C_BG);
        gfx_text(4, 56, &font_5x8, "on the selected slot.", C_DIM, C_BG);
        return;
    }
    for (int i = msg_scroll; i < msg_count() && y > 24; i++) {
        const msg_t *m = msg_get(i);
        int in = m->flags & MSG_IN;
        put_str(put_str(full, in ? "< " : m->flags & MSG_DELIVERED ? "+ " : "> "), m->text);
        int n = (int)strlen(full), lines = (n + MSG_LINE - 1) / MSG_LINE;
        uint16_t c = m->flags & MSG_SECRET ? C_DIM : in ? C_TEXT : C_CYAN;
        for (int l = lines - 1; l >= 0 && y > 24; l--) {
            y -= 10;
            memcpy(line, full + l * MSG_LINE, MSG_LINE);
            line[MSG_LINE] = 0;
            gfx_text(1, y, &font_6x10, line, c, C_BG);
            if (l == 0)
                gfx_text(1, y, &font_6x10, (char[]){full[0], 0},
                         in ? C_GREEN : m->flags & MSG_DELIVERED ? C_GREEN : C_CYAN, C_BG);
        }
    }
}

static void draw_compose(const char *hint)     /* the messages screen's own hint when 0 */
{
    char line[MSG_LINE + 1], s[24];
    lcd_fill(0, MSG_BOTTOM, LCD_W, LCD_H - MSG_BOTTOM, C_PANEL);
    for (int l = 0; l < 2; l++) {
        int from = l * MSG_LINE, n = comp_len - from;
        if (n <= 0)
            break;
        memcpy(line, comp + from, (size_t)(n > MSG_LINE ? MSG_LINE : n));
        line[n > MSG_LINE ? MSG_LINE : n] = 0;
        gfx_text(1, MSG_BOTTOM + 2 + l * 10, &font_6x10, line, C_TEXT, C_PANEL);
    }
    tap_shown = (uint8_t)tap_pending();
    int last = comp_len - 1, cx = 1 + (comp_len % MSG_LINE) * 6, cy = MSG_BOTTOM + 2 + comp_len / MSG_LINE * 10;
    if (tap_shown && last >= 0)                 /* the letter still cycling */
        gfx_text(1 + (last % MSG_LINE) * 6, MSG_BOTTOM + 2 + last / MSG_LINE * 10, &font_6x10,
                 (char[]){comp[last], 0}, C_BG, C_AMBER);
    else if (comp_len < MSG_TEXT)
        lcd_fill(cx, cy + 9, 5, 1, C_CYAN);    /* cursor */
    if ((int32_t)(toast_until - millis()) > 0) {
        gfx_text(1, LCD_H - 9, &font_5x8, toast, C_AMBER, C_PANEL);
        return;
    }
    if (!hint) {
        put_str(put_dec(put_str(put_dec(put_str(s, lower ? "abc " : "ABC "), comp_len), "/"), MSG_TEXT),
                comp_len ? "  MENU send" : "  BACK exit");
        hint = s;
    }
    gfx_text(1, LCD_H - 9, &font_5x8, hint, C_DIM, C_PANEL);
}

static void draw_msg_compose(void)
{
    draw_compose(0);
}

static void draw_msg(void)
{
    draw_msg_header();
    draw_msg_history();
    draw_msg_compose();
}

static void open_messages(void)
{
    screen = SCR_MSG;
    msg_read_all();
    msg_scroll = 0;
    draw_msg();
}

static int digit_of(int key);

enum { E_TEXT, E_CALL, E_RIC, E_KEY };   /* what is typed: any text, a callsign, digits, a key */

/* a key typed into buf (len characters, at most max) */
static void type_key(char *buf, uint8_t *len, int max, int digit, int hold, int kind)
{
    static const char *const call_chars[10] = {
        "0", "-1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
    };
    char one[2] = {(char)('0' + digit), 0};
    const char *set = kind == E_RIC ? one : (kind == E_CALL ? call_chars : key_chars)[digit];
    uint32_t now = millis();
    if (!hold && digit == tap_key && now - tap_at < TAP_MS && *len) {
        tap_n = (uint8_t)((tap_n + 1) % strlen(set));
        (*len)--;                               /* the same key again: the next character */
    } else {
        tap_n = 0;
        if (*len >= max) {
            show_toast("full");
            return;
        }
    }
    char c = hold ? (char)('0' + digit) : set[tap_n];
    if (lower && (kind == E_TEXT || kind == E_KEY) && c >= 'A' && c <= 'Z')
        c = (char)(c + 32);
    buf[(*len)++] = c;
    buf[*len] = 0;
    tap_key = hold ? 0xFF : (uint8_t)digit;
    tap_at = now;
}

/* digits, FM (ABC / abc) and a held BACK (clear) for a text being typed; 1 if the key was used */
static int text_key(const key_event_t *ev, char *buf, uint8_t *len, int max, int kind)
{
    int d = digit_of(ev->key);
    if (d >= 0) {
        if (ev->type == KEV_PRESS) {
            key_long[ev->key] = 0;
        } else if (ev->type == KEV_LONG) {
            key_long[ev->key] = 1;
            type_key(buf, len, max, d, 1, kind);
        } else if (ev->type == KEV_RELEASE && !key_long[ev->key]) {
            type_key(buf, len, max, d, 0, kind);
        }
        return 1;
    }
    if (ev->key == KEY_FM && ev->type == KEV_PRESS && (kind == E_TEXT || kind == E_KEY)) {
        lower ^= 1;
        tap_key = 0xFF;
        return 1;
    }
    if (ev->key == KEY_BACK && ev->type == KEV_LONG) {
        *len = 0;
        buf[0] = 0;
        return 1;
    }
    return 0;
}

/* ---- APRS screen --------------------------------------------------------------------------- */

/* Heard stations and our own packets, newest at the bottom; up/down pick one (highlighted), and
   what is typed below goes to it as an APRS message. MENU with nothing typed sends the beacon. */
static int8_t aprs_sel = -1;
static uint8_t data_mode;               /* what the modem listens for: 0 messages, 1 APRS, 2 pager */

static void draw_aprs_header(void)
{
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "APRS", C_TEXT, C_PANEL);
    const char *r = aprs_receiving() ? "receiving" : aprs_cfg.call[0] ? aprs_cfg.call : "no call set";
    gfx_text(124 - gfx_text_width(&font_5x8, r), 4, &font_5x8, r,
             aprs_receiving() ? C_GREEN : aprs_cfg.call[0] ? C_DIM : C_AMBER, C_PANEL);
}

/* a list of heard / sent entries (APRS, pager), newest at the bottom, sel highlighted and shown
   at the bottom; row() gives an entry's coloured head (call, RIC) and its text */
typedef struct {
    char head[12];
    const char *text;
    uint16_t color;
} row_t;

static void draw_rows(int count, int sel, void (*row)(int i, row_t *r), const char *const *empty)
{
    char full[64], line[MSG_LINE + 1];
    int y = MSG_BOTTOM;
    lcd_fill(0, 14, LCD_W, MSG_BOTTOM - 14, C_BG);
    if (!count) {
        gfx_text(4, 24, &font_6x10, empty[0], C_DIM, C_BG);
        for (int i = 1; empty[i]; i++)
            gfx_text(4, 30 + i * 10, &font_5x8, empty[i], C_DIM, C_BG);
        return;
    }
    for (int i = sel > 0 ? sel : 0; i < count && y > 24; i++) {
        row_t r;
        row(i, &r);
        char *e = put_str(put_str(full, r.head), " ");
        int head_len = (int)(e - full) - 1;
        put_str(e, r.text);
        int n = (int)strlen(full), lines = n > MSG_LINE ? 2 : 1;
        uint16_t bg = i == sel ? C_PANEL2 : C_BG, cc = r.color;
        if (y - lines * 10 < 15)
            break;
        lcd_fill(0, y - lines * 10, LCD_W, lines * 10, bg);
        for (int l = lines - 1; l >= 0; l--) {
            y -= 10;
            memcpy(line, full + l * MSG_LINE, MSG_LINE);
            line[MSG_LINE] = 0;
            gfx_text(1, y, &font_6x10, line, C_TEXT, bg);
            if (l == 0) {
                line[head_len < MSG_LINE ? head_len : MSG_LINE] = 0;
                gfx_text(1, y, &font_6x10, line, cc, bg);
            }
        }
    }
}

static void aprs_row(int i, row_t *r)
{
    const aprs_pkt_t *p = aprs_get(i);
    int out = p->flags & APRS_OUT;
    put_str(put_str(r->head, out ? (p->flags & APRS_ACKED ? "+" : ">") : ""), p->from);
    r->text = p->text;
    r->color = out ? C_CYAN : p->flags & APRS_TO_ME ? C_AMBER : C_GREEN;
}

static void draw_aprs_list(void)
{
    static const char *const empty[] = {
        "Nothing heard yet.", "MENU sends a beacon on", "the selected slot. Set",
        "APRS call in the menu.", "Receiving: experimental", 0,
    };
    draw_rows(aprs_count(), aprs_sel, aprs_row, empty);
}

static void draw_aprs_compose(void)
{
    char s[24];
    const aprs_pkt_t *p = aprs_sel >= 0 ? aprs_get(aprs_sel) : 0;
    if (comp_len)
        put_str(put_str(put_str(s, "to "), p ? p->from : "?"), "  MENU send");
    else
        put_str(s, p ? "type to message" : "MENU beacon  up: pick");
    draw_compose(s);
}

static void draw_aprs(void)
{
    draw_aprs_header();
    draw_aprs_list();
    draw_aprs_compose();
}

static void open_aprs(void)
{
    screen = SCR_APRS;
    aprs_read_all();
    aprs_sel = -1;
    draw_aprs();
}

/* ---- pager screen -------------------------------------------------------------------------- */

/* Pages heard and sent; what is typed goes to the picked page's RIC, or to "Pager RIC" (menu).
   MENU with nothing typed sends a tone-only page. */
static int8_t pager_sel = -1;

static void page_row(int i, row_t *r)
{
    const page_t *p = pocsag_get(i);
    put_str(put_str(r->head, p->flags & PAGE_OUT ? ">" : ""), p->from);
    r->text = p->text;
    r->color = p->flags & PAGE_OUT ? C_CYAN : C_GREEN;
}

static uint32_t pager_target(void)
{
    const page_t *p = pager_sel >= 0 ? pocsag_get(pager_sel) : 0;
    uint32_t ric = 0;
    if (!p)
        return pager_cfg.ric;
    for (const char *c = p->from; *c; c++)
        ric = ric * 10 + (uint32_t)(*c - '0');
    return ric;
}

static void draw_pager(void)
{
    static const char *const empty[] = {
        "No pages yet.", "Type a page; MENU sends", "it to the Pager RIC", "(menu), or pick a page",
        "with up to answer it.", 0,
    };
    char s[24];
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "Pager", C_TEXT, C_PANEL);
    if (pocsag_receiving())
        put_str(s, "receiving");
    else
        put_str(put_dec(s, pager_bauds[pager_cfg.speed]), " bit/s");
    gfx_text(124 - gfx_text_width(&font_5x8, s), 4, &font_5x8, s,
             pocsag_receiving() ? C_GREEN : C_DIM, C_PANEL);
    draw_rows(pocsag_count(), pager_sel, page_row, empty);
    put_str(put_dec(put_str(s, "to "), pager_target()), "  MENU send");
    draw_compose(s);
}

static void open_pager(void)
{
    screen = SCR_PAGER;
    pager_sel = -1;
    draw_pager();
}

/* ---- text editor (menu: APRS call, APRS beacon, pager RIC, scramble key) ------------------- */

static char ebuf[16];
static uint8_t elen, edit_kind;
#define EDIT_MAX (edit_kind == E_CALL ? 9 : edit_kind == E_RIC ? 7 : 14)

static void draw_edit_field(void)
{
    lcd_fill(2, 36, 124, 20, C_PANEL);
    gfx_text(6, 40, &font_7x13b, ebuf, C_TEXT, C_PANEL);
    tap_shown = (uint8_t)tap_pending();
    if (tap_shown && elen)
        gfx_text(6 + (elen - 1) * 7, 40, &font_7x13b, (char[]){ebuf[elen - 1], 0}, C_BG, C_AMBER);
    else if (elen < EDIT_MAX)
        lcd_fill(6 + elen * 7, 53, 6, 1, C_CYAN);
    lcd_fill(0, LCD_H - 10, LCD_W, 10, C_BG);
    if ((int32_t)(toast_until - millis()) > 0)
        gfx_text(4, LCD_H - 9, &font_5x8, toast, C_AMBER, C_BG);
}

static void draw_edit(void)
{
    static const char *const text[4][3] = {
        {"APRS beacon", "Status text, sent with", "MENU on the APRS screen"},
        {"APRS call", "Callsign-SSID, e.g.", "VK2ABC-7 (1 key: -)"},
        {"Pager RIC", "The pager address the", "pager screen sends to"},
        {"Scramble key", "For Rolling channels;", "the same on every radio"},
    };
    const char *const *t = text[edit_kind];
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, t[0], C_TEXT, C_PANEL);
    lcd_fill(0, 14, LCD_W, LCD_H - 14, C_BG);
    gfx_text(4, 20, &font_5x8, t[1], C_DIM, C_BG);
    gfx_text(4, 28, &font_5x8, t[2], C_DIM, C_BG);
    gfx_text(4, 70, &font_5x8, "MENU save", C_DIM, C_BG);
    gfx_text(4, 80, &font_5x8, "BACK delete / cancel", C_DIM, C_BG);
    if (edit_kind == E_TEXT || edit_kind == E_KEY)
        gfx_text(4, 90, &font_5x8, "FM abc / ABC", C_DIM, C_BG);
    draw_edit_field();
}

static void open_edit(int kind)
{
    edit_kind = (uint8_t)kind;
    if (kind == E_RIC)
        put_dec(ebuf, pager_cfg.ric);
    else if (kind == E_KEY)
        strcpy(ebuf, roll_cfg.key);
    else
        strcpy(ebuf, kind == E_CALL ? aprs_cfg.call : aprs_cfg.text);
    elen = (uint8_t)strlen(ebuf);
    tap_key = 0xFF;
    screen = SCR_EDIT;
    draw_edit();
}

/* ---- frequency scanner (menu): From, To, Step, then it runs on the main screen ---------------- */

static uint8_t fs_field, fs_len;        /* 0 From, 1 To, 2 Step; digits typed in this field */
static char fs_digits[8];

static void stop_activity(void);

static uint32_t fs_value(int field)
{
    return field ? roll_cfg.scan_hi : roll_cfg.scan_lo;
}

static void put_mhz(char *s, uint32_t f)
{
    uint32_t v = f / 10;                /* 100 Hz */
    char *p = put_dec(s, v / 10000);
    *p++ = '.';
    for (uint32_t d = 1000; d; d /= 10)
        *p++ = (char)('0' + v / d % 10);
    *p = 0;
}

static void draw_fscan(void)
{
    static const char *const labels[3] = {"From", "To", "Step"};
    char v[12];
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "Freq scanner", C_TEXT, C_PANEL);
    lcd_fill(0, 14, LCD_W, LCD_H - 14, C_BG);
    for (int i = 0; i < 3; i++) {
        int y = 20 + i * 18, on = i == fs_field;
        uint16_t bg = on ? C_PANEL2 : C_BG;
        if (on)
            gfx_rrect(2, y - 2, 124, 16, bg);
        if (i == 2) {
            put_str(v, step_names[roll_cfg.scan_step]);
        } else if (on && fs_len) {
            for (int k = 0, n = 0; k < 8; k++)
                v[k] = k == 3 ? '.' : n < fs_len ? fs_digits[n++] : '-';
            v[8] = 0;
        } else {
            put_mhz(v, fs_value(i));
        }
        gfx_text(6, y + 1, &font_6x10, labels[i], C_TEXT, bg);
        gfx_text(122 - gfx_text_width(&font_7x13b, v), y, &font_7x13b, v, on ? C_AMBER : C_DIM, bg);
    }
    gfx_text(4, 80, &font_5x8, fs_field < 2 ? "Type MHz, 7 digits" : "UP / DOWN change", C_DIM, C_BG);
    gfx_text(4, 90, &font_5x8, fs_field < 2 ? "MENU next" : "MENU start scanning", C_DIM, C_BG);
    gfx_text(4, 100, &font_5x8, "BACK delete / leave", C_DIM, C_BG);
    lcd_fill(0, LCD_H - 10, LCD_W, 10, C_BG);
    if ((int32_t)(toast_until - millis()) > 0)
        gfx_text(4, LCD_H - 9, &font_5x8, toast, C_AMBER, C_BG);
}

static void open_fscan(void)
{
    if (!roll_cfg.scan_lo) {
        roll_cfg.scan_lo = 47600000;
        roll_cfg.scan_hi = 47740000;
        roll_cfg.scan_step = 4;
    }
    if (roll_cfg.scan_step >= STEP_COUNT)
        roll_cfg.scan_step = 4;
    fs_field = 0;
    fs_len = 0;
    screen = SCR_FSCAN;
    draw_fscan();
}

/* what was typed goes in, as a frequency is typed on the main screen: MHz first */
static int fs_commit(void)
{
    if (!fs_len)
        return 1;
    uint32_t v = 0;
    for (int i = 0; i < 7; i++)
        v = v * 10 + (uint32_t)(i < fs_len ? fs_digits[i] - '0' : 0);
    v *= 10;
    fs_len = 0;
    if (v < 1800000 || v > 130000000) {
        show_toast("18 to 1300 MHz");
        return 0;
    }
    if (fs_field)
        roll_cfg.scan_hi = v;
    else
        roll_cfg.scan_lo = v;
    return 1;
}

static void fs_start(void)
{
    slot_t *s = watch_slot(watch_selected());
    uint32_t lo = roll_cfg.scan_lo, hi = roll_cfg.scan_hi;
    if (lo > hi) {
        roll_cfg.scan_lo = hi;
        roll_cfg.scan_hi = lo;
    }
    if (roll_cfg.scan_hi - roll_cfg.scan_lo < steps[roll_cfg.scan_step]) {
        show_toast("range under one step");
        draw_fscan();
        return;
    }
    roll_save();
    stop_activity();
    voice_stop();
    watch_pause(1);
    screen = SCR_MAIN;
    scan_start_vfo(roll_cfg.scan_lo, roll_cfg.scan_hi, steps[roll_cfg.scan_step], s->vfo_narrow);
    draw_main();
}

static void key_fscan(const key_event_t *ev)
{
    int d = digit_of(ev->key);
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    if (d >= 0 && fs_field < 2) {
        fs_digits[fs_len++] = (char)('0' + d);
        if (fs_len == 7 && fs_commit())
            fs_field++;
    } else if (ev->key == KEY_MENU) {
        if (fs_field == 2) {
            fs_start();
            return;
        }
        if (fs_commit())
            fs_field++;
    } else if (ev->key == KEY_UP || ev->key == KEY_DOWN) {
        int dir = ev->key == KEY_DOWN ? 1 : -1;
        if (fs_field == 2) {
            roll_cfg.scan_step = (uint8_t)((roll_cfg.scan_step + STEP_COUNT + dir) % STEP_COUNT);
        } else if (fs_commit()) {
            fs_field = (uint8_t)(fs_field ^ 1);
        }
    } else if (ev->key == KEY_BACK) {
        if (fs_len) {
            fs_len--;
        } else if (fs_field) {
            fs_field--;
        } else {
            screen = SCR_MENU;
            draw_menu();
            return;
        }
    }
    draw_fscan();
}

/* ---- FM broadcast radio (a side key set to FM radio) ------------------------------------------ */

static uint8_t fm_len;
static char fm_digits[5];

static void draw_fm(void)
{
    char s[8], *p;
    lcd_fill(0, 0, LCD_W, 14, C_PANEL);
    gfx_text(4, 1, &font_7x13b, "FM radio", C_TEXT, C_PANEL);
    lcd_fill(0, 14, LCD_W, LCD_H - 14, C_BG);
    if (fm_len) {
        memcpy(s, fm_digits, fm_len);
        s[fm_len] = 0;
    } else {
        p = put_dec(s, roll_cfg.fm / 10u);
        *p++ = '.';
        put_dec(p, roll_cfg.fm % 10u);
    }
    int x = gfx_aa_text(6, 30, &font_digits21, s, C_AMBER, C_BG);
    gfx_text(x + 3, 41, &font_5x8, "MHz", C_DIM, C_BG);
    gfx_text(4, 76, &font_5x8, "UP / DOWN tune", C_DIM, C_BG);
    gfx_text(4, 86, &font_5x8, "0-9 type, MENU sets", C_DIM, C_BG);
    gfx_text(4, 96, &font_5x8, "BACK leave, PTT talks", C_DIM, C_BG);
}

static void open_fm(void)
{
    if (stock.fmradio) {
        show_toast("FM radio is off (Talkpod Manager)");
        return;
    }
    stop_activity();
    voice_stop();
    watch_pause(1);
    radio_mute();
    radio_sleep();
    if (roll_cfg.fm < 650 || roll_cfg.fm > 1080)
        roll_cfg.fm = 1000;
    bk1080_on();
    bk1080_tune(roll_cfg.fm);
    board_speaker(SPK_FM, 1);
    fm_len = 0;
    screen = SCR_FM;
    draw_fm();
}

static void close_fm(void)
{
    board_speaker(SPK_FM, 0);
    bk1080_off();
    roll_save();
    watch_pause(0);
    radio_retune();
    screen = SCR_MAIN;
}

static void fm_commit(void)
{
    uint32_t v = 0;
    for (int i = 0; i < fm_len; i++)
        v = v * 10 + (uint32_t)(fm_digits[i] - '0');
    fm_len = 0;
    if (v < 650 || v > 1080) {
        show_toast("65.0 to 108.0 MHz");
    } else {
        roll_cfg.fm = (uint16_t)v;
        bk1080_tune(roll_cfg.fm);
    }
}

static void key_fm(const key_event_t *ev)
{
    int d = digit_of(ev->key);
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    if (d >= 0) {
        fm_digits[fm_len++] = (char)('0' + d);
        if (fm_len == 4)
            fm_commit();
    } else if (ev->key == KEY_MENU && fm_len) {
        fm_commit();
    } else if (ev->key == KEY_UP || ev->key == KEY_DOWN) {
        int f = roll_cfg.fm + (ev->key == KEY_UP ? 1 : -1);
        roll_cfg.fm = (uint16_t)(f > 1080 ? 650 : f < 650 ? 1080 : f);
        bk1080_tune(roll_cfg.fm);
    } else if (ev->key == KEY_BACK) {
        if (fm_len) {
            fm_len = 0;
        } else {
            close_fm();
            draw_main();
            return;
        }
    }
    draw_fm();
}

static void redraw(void)
{
    if (screen == SCR_MAIN)
        draw_main();
    else if (screen == SCR_MENU)
        draw_menu();
    else if (screen == SCR_MSG)
        draw_msg();
    else if (screen == SCR_APRS)
        draw_aprs();
    else if (screen == SCR_EDIT)
        draw_edit();
    else if (screen == SCR_PAGER)
        draw_pager();
    else if (screen == SCR_FSCAN)
        draw_fscan();
    else if (screen == SCR_FM)
        draw_fm();
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
        int fscan = scan_mode() == SCAN_VFO;
        scan_stop();
        if (fscan) {                        /* stays on this frequency, in VFO mode */
            s->is_channel = 0;
            s->vfo = radio_freq();
            s->vfo_am = (uint8_t)channel_is_am(s->vfo);
        } else if (scan_channel()) {
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
        s->low_power ^= 1;
        show_toast(s->low_power ? "TX power low" : "TX power high");
        snd(s->low_power ? SND_LOW : SND_HIGH);
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
    case KEY_MENU:
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
        snd(s->is_channel ? SND_CHANNEL_MODE : SND_FREQUENCY_MODE);
        break;
    case KEY_FM:
        locked ^= 1;
        show_toast(locked ? "Keys locked" : "Keys unlocked");
        break;
    case KEY_5:
        open_messages();
        return;
    case KEY_7:
        open_aprs();
        return;
    case KEY_8:
        open_pager();
        return;
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

static uint8_t side_short(int key);
static int side_key(const key_event_t *ev);
static uint8_t ptt_paused;              /* PTT took the receiver (not when it was refused) */
static int8_t ptt_back = -1;            /* the slot to go back to: PTT B or Dual TX */
static uint32_t tail_until;             /* HOLD_TAIL until then; 0: until the carrier goes */
static uint8_t tail_held;

static void ptt_refused(const char *why)
{
    show_toast(why);
    snd(SND_CANCEL);
    draw_hint();
}

static void ptt_down(void)
{
    if (ptt_back < 0 && watch_dual() && stock.dualtx && stock.dualtx - 1 != watch_selected()) {
        ptt_back = (int8_t)watch_selected();    /* Dual TX: this slot transmits */
        watch_select(stock.dualtx - 1);
    }
    int sel = watch_selected();
    slot_t *s = watch_slot(sel);
    entry_len = 0;
    if (!s->tx) {
        ptt_refused("TX off on this channel");
        return;
    }
    if (s->bcl && watch_rx_slot() == sel && radio_busy()) {
        ptt_refused("Channel busy");         /* busy channel lockout */
        return;
    }
    if (s->scramble == BK_SCRAMBLE_ROLL && !roll_cfg.key[0]) {
        ptt_refused("Set Scramble key (menu)");
        return;
    }
    stop_activity();
    voice_stop();
    watch_pause(1);
    ptt_paused = 1;
    tx_timed_out = 0;
    if (!tx_start(s->tx, s->narrow, s->low_power, s->tx_tone.type, s->tx_tone.value, s->scramble)) {
        watch_pause(0);
        ptt_paused = 0;
        show_toast(tx_allowed(s->tx) ? "TX failed" : "TX not allowed here");
        draw_main();
        return;
    }
    if (s->scramble == BK_SCRAMBLE_ROLL && !roll_tx(s->narrow)) {   /* the burst, then voice */
        tx_stop();
        watch_pause(0);
        ptt_paused = 0;
        show_toast("TX failed");
        draw_main();
        return;
    }
    if (s->pttid & 1) {                     /* stock's PTT-ID at key-up, after its delay */
        static const uint16_t lt_ms[7] = {0, 100, 200, 400, 600, 800, 1000};
        delay_ms(lt_ms[stock.pttlt < 7 ? stock.pttlt : 0]);
        tx_ptt_id(s->scode);
    }
    debug_puts("TX on\r\n");
    next_tx_draw = millis() + 1000;
    draw_main();
}

static void ptt_up(void)
{
    if (tx_active()) {
        slot_t *s = watch_slot(watch_selected());
        if (s->pttid & 2)
            tx_ptt_id(s->scode);
        tx_end();
        debug_puts("TX off\r\n");
        if (stock.rpste || stock.rptrl) {       /* the repeater's tail isn't heard */
            radio_audio_hold(HOLD_TAIL, 1);
            tail_until = millis() + (stock.rpste + stock.rptrl) * 100u;
        }
    }
    tx_timed_out = 0;
    if (ptt_back >= 0) {
        watch_select(ptt_back);
        ptt_back = -1;
    }
    if (ptt_paused) {
        ptt_paused = 0;
        watch_pause(0);
    }
    draw_main();
}

static void key_main(const key_event_t *ev)
{
    int sel = watch_selected();
    int busy = searching || scan_mode() != SCAN_OFF;

    if (ev->key == KEY_PTT) {
        if (ev->type == KEV_PRESS)
            ptt_down();
        else if (ev->type == KEV_RELEASE)
            ptt_up();
        return;
    }
    if (tx_active() && (ev->key == KEY_SIDE1 || ev->key == KEY_SIDE2 || ev->key == KEY_TOP)) {
        if (side_short(ev->key) == FN_MONITOR) {        /* stock: the tone burst */
            if (ev->type == KEV_PRESS || ev->type == KEV_RELEASE)
                tx_tone_burst(ev->type == KEV_PRESS);
        } else if (side_short(ev->key) == FN_PTT_B && ev->type == KEV_RELEASE) {
            ptt_up();
        }
        return;
    }
    if (tx_active() && digit_of(ev->key) >= 0) {        /* DTMF while held */
        if (ev->type == KEV_PRESS)
            tx_dtmf_key(digit_of(ev->key));
        else if (ev->type == KEV_RELEASE)
            tx_dtmf_key(-1);
        return;
    }
    if (tx_active() || tx_timed_out)
        return;                             /* other keys wait until PTT is released */

    if (side_key(ev))
        return;
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
    if (ev->key == KEY_MENU || ev->key == KEY_FM) {      /* short press acts on release */
        if (ev->type == KEV_PRESS)
            key_long[ev->key] = 0;
        if (ev->type != KEV_RELEASE || key_long[ev->key])
            return;
        if (ev->key == KEY_FM) {
            slot_t *s = watch_slot(sel);
            if (busy || entry_len)
                return;
            if (s->is_channel) {
                show_toast("channel: fixed");
            } else {
                s->vfo_am ^= 1;
                watch_update(sel);
                show_toast(s->vfo_am ? "AM" : "FM");
            }
            draw_main();
        } else if (entry_len) {
            commit_entry();
            draw_main();
        } else if (!busy) {
            screen = SCR_MENU;
            editing = 0;
            snd(SND_MENU);
            draw_menu();
        }
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
    case KEY_BACK:
        entry_len = 0;
        stop_activity();
        snd(SND_CANCEL);
        draw_main();
        break;
    default:
        break;
    }
}

/* Stock's alarm (SOS on a side key), until any key: Local on the speaker alone, Tone on the air too,
   Code on the air as well with the ANI code as DTMF every few seconds. */
static uint8_t alarm_on;
static uint32_t alarm_next;
static uint16_t alarm_hz;
static uint8_t alarm_n;

static void alarm_stop(void)
{
    if (!alarm_on)
        return;
    alarm_on = 0;
    if (tx_active()) {
        bk4819_tone_tx(0);
        tx_stop();
    }
    bk4819_beep(0, 0);
    board_speaker(SPK_TEST, 0);
    radio_mute();
    watch_pause(0);
    draw_main();
}

static void alarm_toggle(void)
{
    slot_t *s = watch_slot(watch_selected());
    if (alarm_on) {
        alarm_stop();
        return;
    }
    stop_activity();
    voice_stop();
    watch_pause(1);
    alarm_on = 1;
    alarm_hz = 1000;
    alarm_next = 0;
    if (stock.almode && tx_start(s->tx, s->narrow, s->low_power, s->tx_tone.type, s->tx_tone.value, 0))
        bk4819_tone_tx(1);
    show_toast("ALARM: any key stops");
}

static void alarm_tick(uint32_t now)
{
    if (!alarm_on || (int32_t)(now - alarm_next) < 0)
        return;
    alarm_next = now + 300;
    alarm_hz = alarm_hz == 1000 ? 1450 : 1000;
    if (tx_active()) {                  /* the speaker needs the chip, which is transmitting */
        bk4819_tone_hz(alarm_hz);
        if (stock.almode == 2 && ++alarm_n % 10 == 0) {
            bk4819_tone_tx(0);
            tx_ani();
            bk4819_tone_tx(1);
        }
    } else if (stock.alarmsound || !stock.almode) {
        board_speaker(SPK_TEST, 1);
        bk4819_beep(1, alarm_hz);
    }
}

/* Stock's battery save: 3 minutes after the last activity the receiver sleeps, and listens in short
   spells (stock's tables at 0x800E486: 3 of 8, 2 of 9, 1 of 10 ticks). 1 while asleep. */
static int save_tick(uint32_t now)
{
    static const uint8_t on_t[4] = {0, 3, 2, 1}, off_t[4] = {0, 5, 7, 9};
    static uint8_t asleep;
    static uint32_t next;
    if (!stock.save || now - last_key < 180000 || radio_busy() || screen != SCR_MAIN || msg_receiving()) {
        if (asleep) {
            asleep = 0;
            radio_retune();
        }
        return 0;
    }
    if ((int32_t)(now - next) >= 0) {
        asleep ^= 1;
        next = now + (asleep ? off_t[stock.save] : on_t[stock.save]) * 100u;
        if (asleep)
            radio_sleep();
        else
            radio_retune();
    }
    return asleep;
}

/* What a side key does (stock's PF2 = Side 2, PF3 = Side 1, Top key). press: 1 going down, 0 coming
   up, which only the keys held down care about. */
static void side_function(uint8_t fn, int press)
{
    int other = watch_selected() ^ 1;
    slot_t *s = watch_slot(watch_selected());
    switch (fn) {
    case FN_MONITOR:
        radio_set_monitor(press);
        draw_main();
        return;
    case FN_PTT_B:
        if (press) {
            ptt_back = (int8_t)watch_selected();
            watch_select(other);
            ptt_down();
        } else {
            ptt_up();
        }
        return;
    default:
        break;
    }
    if (!press)
        return;
    switch (fn) {
    case FN_OFF:
        return;
    case FN_TXP:
        long_press(KEY_1);
        break;
    case FN_SEARCH:
        long_press(KEY_4);
        break;
    case FN_SCAN:
        if (scan_mode() != SCAN_OFF)
            stop_activity();
        else
            start_scan();
        break;
    case FN_SOS:
        alarm_toggle();
        break;
    case FN_FM:
        open_fm();
        if (screen == SCR_FM)
            return;
        break;
    case FN_NOAA: {                     /* the weather channels, one more each press, in the VFO */
        static const uint32_t wx[7] = {16255000, 16240000, 16247500, 16242500, 16245000, 16250000, 16252500};
        static uint8_t n;
        char t[12];
        stop_activity();
        s->is_channel = 0;
        s->vfo = wx[n];
        s->vfo_am = 0;
        n = (uint8_t)((n + 1) % 7);
        watch_update(watch_selected());
        put_dec(put_str(t, "NOAA "), n ? n : 7u);
        show_toast(t);
        break;
    }
    case FN_REVERSE:
        s->reverse ^= 1;
        watch_update(watch_selected());
        show_toast(s->reverse ? "Reverse on" : "Reverse off");
        break;
    default:
        show_toast("Not available yet");
        break;
    }
    draw_main();
}

/* the function of a side key, short press */
static uint8_t side_short(int key)
{
    return key == KEY_SIDE2 ? stock.skey2_sp : key == KEY_SIDE1 ? stock.skey3_sp : stock.topkey_sp;
}

/* Hold functions act at once. The rest of a key with a long function wait for the release. */
static int side_key(const key_event_t *ev)
{
    if (ev->key != KEY_SIDE1 && ev->key != KEY_SIDE2 && ev->key != KEY_TOP)
        return 0;
    uint8_t fn = side_short(ev->key);
    int held = fn == FN_MONITOR || fn == FN_PTT_B;
    int has_long = ev->key == KEY_SIDE2 && stock.skey2_lp != FN_OFF;
    if (ev->type == KEV_PRESS) {
        key_long[ev->key] = 0;
        if (held || !has_long)
            side_function(fn, 1);
    } else if (ev->type == KEV_LONG && has_long && !held) {
        key_long[ev->key] = 1;
        side_function(stock.skey2_lp, 1);
    } else if (ev->type == KEV_RELEASE) {
        if (held)
            side_function(fn, 0);
        else if (has_long && !key_long[ev->key])
            side_function(fn, 1);
    }
    return 1;
}

static void send_message(void)
{
    slot_t *s = watch_slot(watch_selected());
    stop_activity();
    voice_stop();
    watch_pause(1);
    tap_key = 0xFF;
    lcd_fill(0, LCD_H - 10, LCD_W, 10, C_PANEL);
    gfx_text(1, LCD_H - 9, &font_5x8, "sending...", C_RED, C_PANEL);
    int r = msg_send(comp, s->tx, s->narrow, s->low_power);
    watch_pause(0);
    if (r > 0) {
        comp_len = 0;
        comp[0] = 0;
        msg_scroll = 0;
        debug_puts("message sent\r\n");
    } else {
        show_toast(r < 0 ? "TX not allowed here" : "not sent");
    }
    draw_msg();
}

static void key_msg(const key_event_t *ev)
{
    if (text_key(ev, comp, &comp_len, MSG_TEXT, 0)) {
        draw_msg_compose();
        return;
    }
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    switch (ev->key) {
    case KEY_MENU:
        if (comp_len)
            send_message();
        break;
    case KEY_BACK:
        snd(SND_CANCEL);
        tap_key = 0xFF;
        if (comp_len) {
            comp[--comp_len] = 0;
            draw_msg_compose();
        } else {
            screen = SCR_MAIN;
            draw_main();
        }
        break;
    case KEY_UP:
        if (msg_scroll + 1 < msg_count()) {
            msg_scroll++;
            draw_msg_history();
        }
        break;
    case KEY_DOWN:
        if (msg_scroll) {
            msg_scroll--;
            draw_msg_history();
        }
        break;
    default:
        break;
    }
}

static void aprs_send(void)
{
    slot_t *s = watch_slot(watch_selected());
    const aprs_pkt_t *p = aprs_sel >= 0 ? aprs_get(aprs_sel) : 0;
    if (comp_len && (!p || (p->flags & APRS_OUT && !(p->flags & APRS_MSG)))) {
        show_toast("pick a station: up");
        draw_aprs_compose();
        return;
    }
    stop_activity();
    voice_stop();
    watch_pause(1);
    tap_key = 0xFF;
    lcd_fill(0, LCD_H - 10, LCD_W, 10, C_PANEL);
    gfx_text(1, LCD_H - 9, &font_5x8, "sending...", C_RED, C_PANEL);
    int r = comp_len ? aprs_message(p->from, comp, s->tx, s->narrow, s->low_power)
                     : aprs_beacon(s->tx, s->narrow, s->low_power);
    watch_pause(0);
    if (r > 0) {
        debug_puts("APRS sent\r\n");
        if (comp_len) {
            comp_len = 0;
            comp[0] = 0;
        }
        aprs_sel = -1;
    } else {
        show_toast(r == -2 ? "set APRS call (menu)" : r < 0 ? "TX not allowed here" : "not sent");
    }
    draw_aprs();
}

static void key_aprs(const key_event_t *ev)
{
    if (text_key(ev, comp, &comp_len, MSG_TEXT, 0)) {
        draw_aprs_compose();
        return;
    }
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    switch (ev->key) {
    case KEY_MENU:
        aprs_send();
        break;
    case KEY_BACK:
        snd(SND_CANCEL);
        tap_key = 0xFF;
        if (comp_len) {
            comp[--comp_len] = 0;
            draw_aprs_compose();
        } else if (aprs_sel >= 0) {
            aprs_sel = -1;
            draw_aprs();
        } else {
            screen = SCR_MAIN;
            draw_main();
        }
        break;
    case KEY_UP:
        if (aprs_sel + 1 < aprs_count()) {
            aprs_sel++;
            draw_aprs_list();
            draw_aprs_compose();
        }
        break;
    case KEY_DOWN:
        if (aprs_sel >= 0) {
            aprs_sel--;
            draw_aprs_list();
            draw_aprs_compose();
        }
        break;
    default:
        break;
    }
}

static void pager_send(void)
{
    slot_t *s = watch_slot(watch_selected());
    stop_activity();
    voice_stop();
    watch_pause(1);
    tap_key = 0xFF;
    lcd_fill(0, LCD_H - 10, LCD_W, 10, C_PANEL);
    gfx_text(1, LCD_H - 9, &font_5x8, "sending...", C_RED, C_PANEL);
    int r = pocsag_send(pager_target(), comp, s->tx, s->narrow, s->low_power);
    watch_pause(0);
    if (r > 0) {
        debug_puts("page sent\r\n");
        comp_len = 0;
        comp[0] = 0;
        pager_sel = -1;
    } else {
        show_toast(r < 0 ? "TX not allowed here" : "not sent");
    }
    draw_pager();
}

static void key_pager(const key_event_t *ev)
{
    if (text_key(ev, comp, &comp_len, MSG_TEXT, E_TEXT)) {
        draw_pager();
        return;
    }
    if (ev->type != KEV_PRESS && !(ev->type == KEV_REPEAT && (ev->key == KEY_UP || ev->key == KEY_DOWN)))
        return;
    switch (ev->key) {
    case KEY_MENU:
        pager_send();
        break;
    case KEY_BACK:
        snd(SND_CANCEL);
        tap_key = 0xFF;
        if (comp_len) {
            comp[--comp_len] = 0;
        } else if (pager_sel >= 0) {
            pager_sel = -1;
        } else {
            screen = SCR_MAIN;
            draw_main();
            break;
        }
        draw_pager();
        break;
    case KEY_UP:
        if (pager_sel + 1 < pocsag_count()) {
            pager_sel++;
            draw_pager();
        }
        break;
    case KEY_DOWN:
        if (pager_sel >= 0) {
            pager_sel--;
            draw_pager();
        }
        break;
    default:
        break;
    }
}

static void key_edit(const key_event_t *ev)
{
    if (text_key(ev, ebuf, &elen, EDIT_MAX, edit_kind)) {
        draw_edit_field();
        return;
    }
    if (ev->type != KEV_PRESS)
        return;
    if (ev->key == KEY_MENU) {
        if (edit_kind == E_RIC) {
            uint32_t ric = 0;
            for (int i = 0; i < elen; i++)
                ric = ric * 10 + (uint32_t)(ebuf[i] - '0');
            if (!elen || ric >= 1u << 21) {
                show_toast("RIC 0 to 2097151");
                draw_edit_field();
                return;
            }
            pager_cfg.ric = ric;
            pocsag_save();
        } else if (edit_kind == E_KEY) {
            strcpy(roll_cfg.key, ebuf);
            roll_save();
        } else {
            strcpy(edit_kind == E_CALL ? aprs_cfg.call : aprs_cfg.text, ebuf);
            aprs_save();
        }
        snd(SND_OPEN);
        screen = SCR_MENU;
        draw_menu();
    } else if (ev->key == KEY_BACK) {
        snd(SND_CANCEL);
        tap_key = 0xFF;
        if (elen) {
            ebuf[--elen] = 0;
            draw_edit_field();
        } else {
            screen = SCR_MENU;
            draw_menu();
        }
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
        stock.abr = (uint8_t)((stock.abr + 9 + dir) % 9);
        break;
    case M_TOT:
        stock.tot = (uint8_t)((stock.tot + 6 + dir) % 6);
        break;
    case M_MSG_SPEED:
        msg_set_modem((msg_modem() + 3 - dir) % 3);
        break;
    case M_MSG_RECEIPTS:
        msg_set_receipts(!msg_receipts());
        break;
    case M_APRS_PATH:
        aprs_cfg.path = (uint8_t)((aprs_cfg.path + 4 + dir) % 4);
        aprs_save();
        break;
    case M_PAGER_SPEED:
        pager_cfg.speed = (uint8_t)((pager_cfg.speed + 3 + dir) % 3);
        pocsag_apply();
        pocsag_save();
        break;
    case M_PAGER_INVERT:
        pager_cfg.invert ^= 1;
        pocsag_save();
        break;
    case M_KEYBEEP:
        stock.beep ^= 1;
        break;
    case M_VOICE:
        stock.voice ^= 1;
        break;
    default:
        if (menu_pos >= M_STOCK && menu_pos < M_ABOUT)
            stock_change(&stock_items[menu_pos - M_STOCK], dir);
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
            int top = menu_top;
            if (menu_pos < menu_top)
                menu_top = menu_pos;
            if (menu_pos >= menu_top + MENU_ROWS)
                menu_top = (uint8_t)(menu_pos - MENU_ROWS + 1);
            if (menu_top != top) {
                draw_menu();
            } else {
                draw_menu_row(old);
                draw_menu_row(menu_pos);
                draw_menu_header();
            }
        }
        break;
    }
    case KEY_MENU:
        snd(SND_OPEN);
        if (menu_pos == M_SCAN) {
            screen = SCR_MAIN;
            start_scan();
            draw_main();
        } else if (menu_pos == M_FSCAN) {
            open_fscan();
        } else if (menu_pos == M_MESSAGES) {
            open_messages();
        } else if (menu_pos == M_APRS) {
            open_aprs();
        } else if (menu_pos == M_PAGER) {
            open_pager();
        } else if (menu_pos == M_APRS_CALL || menu_pos == M_APRS_TEXT || menu_pos == M_PAGER_RIC ||
                   menu_pos == M_ROLL_KEY) {
            open_edit(menu_pos == M_APRS_CALL ? E_CALL : menu_pos == M_PAGER_RIC ? E_RIC
                      : menu_pos == M_ROLL_KEY ? E_KEY : E_TEXT);
        } else if (menu_pos == M_ABOUT) {
            screen = SCR_ABOUT;
            draw_about();
        } else {
            editing ^= 1;
            draw_menu_row(menu_pos);
        }
        break;
    case KEY_BACK:
        snd(SND_CANCEL);
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

/* ---- saved settings ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    struct __attribute__((packed)) {
        uint8_t is_channel, flags;      /* flags: 1 VFO AM, 2 VFO narrow, 4 low power */
        uint16_t channel;
        uint32_t vfo;
    } slot[2];
    uint8_t selected, dual, squelch, step, backlight, sounds, tot;      /* dual, squelch, backlight, sounds
                                           and tot moved to the stock record in 1.14: see stock_takes_over() */
    uint8_t msg;                        /* message speed (BK_FSK_*) | 4 no receipts */
    uint8_t stock_done;                 /* 1 from 1.14 on */
} saved_t;

static saved_t saved;                   /* what flash holds now */
static uint32_t next_save_check;

static void gather(saved_t *v)
{
    memset(v, 0, sizeof *v);
    for (int i = 0; i < 2; i++) {
        slot_t *s = watch_slot(i);
        v->slot[i].is_channel = s->is_channel;
        v->slot[i].flags = (uint8_t)(s->vfo_am | s->vfo_narrow << 1 | s->low_power << 2);
        v->slot[i].channel = s->channel;
        v->slot[i].vfo = s->vfo;
    }
    v->selected = (uint8_t)watch_selected();
    v->step = step;
    v->msg = (uint8_t)(msg_modem() | (msg_receipts() ? 0 : 4));
    v->stock_done = 1;
}

/* Before 1.14 these settings were in saved_t, with other lists of values. The first start of 1.14
   keeps what the radio had, and the look of its main screen (stock's default shows channel names). */
static void stock_takes_over(const saved_t *v)
{
    static const uint8_t abr[4] = {0, 2, 5, 6}, tot[6] = {0, 1, 2, 3, 4, 4};
    if (v->squelch < 10)
        stock.sql = v->squelch;
    stock.tdr = v->dual != 0;
    stock.abr = abr[v->backlight & 3];
    stock.tot = tot[v->tot < 6 ? v->tot : 4];
    stock.beep = v->sounds & 1;
    stock.voice = (v->sounds >> 1) & 1;
    stock.mdfa = stock.mdfb = 1;
}

static void apply(const saved_t *v)
{
    channel_t ch;
    for (int i = 0; i < 2; i++) {
        slot_t *s = watch_slot(i);
        if (v->slot[i].vfo >= 1800000 && v->slot[i].vfo <= 130000000)
            s->vfo = v->slot[i].vfo;
        s->vfo_am = v->slot[i].flags & 1;
        s->vfo_narrow = (v->slot[i].flags >> 1) & 1;
        if (channel_read(v->slot[i].channel, &ch))
            s->channel = v->slot[i].channel;
        s->is_channel = v->slot[i].is_channel && channel_read(s->channel, &ch);
        s->freq = 0;                    /* so watch_update() takes the channel's power... */
        watch_update(i);
        s->low_power = (v->slot[i].flags >> 2) & 1;    /* ...and then the saved one wins */
    }
    if (v->step < STEP_COUNT)
        step = v->step;
    msg_init((v->msg & 3) < 3 ? v->msg & 3 : BK_FSK_1200, !(v->msg & 4));
    watch_select(v->selected & 1);
}

/* values this firmware can't show (the PC can write anything) go back to stock's defaults */
static void stock_check(void)
{
    if (stock.abr > 8) stock.abr = 3;
    if (stock.tot > 5) stock.tot = 0;
    if (stock.sql > 9) stock.sql = 3;
    stock.beep &= 1;
    stock.voice &= 1;
    if (stock.autolk > 3) stock.autolk = 0;
    if (stock.save > 3) stock.save = 0;
    if (stock.vox > 10) stock.vox = 0;
    if (stock.voxd > 15) stock.voxd = 0;
    if (stock.pttlt > 6) stock.pttlt = 0;
    if (stock.almode > 2) stock.almode = 0;
    stock.alarmsound &= 1;
    if (stock.menuquit > 10) stock.menuquit = 1;
    if (stock.screv > 2) stock.screv = 1;
    if (stock.mdfa > 2) stock.mdfa = 1;
    if (stock.mdfb > 2) stock.mdfb = 1;
    if (stock.dualtx > 2) stock.dualtx = 0;
    if (stock.roger > 2) stock.roger = 0;
    if (stock.rpste > 10) stock.rpste = 0;
    if (stock.rptrl > 10) stock.rptrl = 0;
}

/* from flash: at power-on, and after the PC wrote them (Talkpod Manager edits them) */
static void load_settings(void)
{
    saved_t v;
    int found;
    stock_load();
    aprs_init();
    pocsag_init();
    pocsag_apply();
    roll_init();
    gather(&v);                         /* defaults for anything an older record lacks */
    v.stock_done = 0;
    found = settings_load(SET_MAIN, &v, sizeof v);
    if (found) {
        apply(&v);
        debug_puts("settings loaded\r\n");
    }
    if (found && !v.stock_done)
        stock_takes_over(&v);
    stock_check();
    radio_set_squelch(stock.sql);
    watch_set_dual(stock.tdr);
    gather(&saved);
    if (found && !v.stock_done) {
        settings_save(SET_MAIN, &saved, sizeof saved);
        stock_save();
    }
}

/* saved 3 s after the last change, never mid-scan or mid-search */
static void save_if_changed(uint32_t now)
{
    saved_t v;
    if ((int32_t)(now - next_save_check) < 0)
        return;
    next_save_check = now + 1000;
    if (searching || scan_mode() != SCAN_OFF || entry_len || now - last_key < 3000)
        return;
    gather(&v);
    stock.sql = (uint8_t)radio_squelch();
    stock.tdr = (uint8_t)watch_dual();
    if (!stock_save())
        debug_puts("settings: stock save failed\r\n");
    if (memcmp(&v, &saved, sizeof v) == 0)
        return;
    if (settings_save(SET_MAIN, &v, sizeof v))
        saved = v;
    else
        debug_puts("settings: save failed\r\n");
}

void ui_init(void)
{
    if (radio_squelch() == 0)
        radio_set_squelch(1);           /* 0 is always open, which leaves dual watch on slot A */
    watch_init();
    msg_init(BK_FSK_1200, 1);
    load_settings();
    last_key = millis();
    apply_backlight(1);
    if (stock.ponmsg == 1) {
        char s[16], *p = put_str(s, "Battery ");
        int dv = battery_decivolts();
        p = put_dec(p, (uint32_t)dv / 10);
        *p++ = '.';
        put_str(put_dec(p, (uint32_t)dv % 10), " V");
        show_toast(s);
    }
    draw_main();
    snd(SND_POWER_ON);
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
    if (ev->key == KEY_PTT) {               /* PTT always works: any screen, keys locked, display off */
        apply_backlight(1);
        if (screen == SCR_FM)
            close_fm();
        if (screen != SCR_MAIN) {
            screen = SCR_MAIN;
            editing = 0;
        }
        key_main(ev);
        return;
    }
    if (alarm_on && ev->type == KEV_PRESS) {
        alarm_stop();
        return;
    }
    if (!light_on) {                        /* the first key only wakes the display */
        apply_backlight(1);
        if (ev->type == KEV_PRESS)
            key_long[ev->key] = 1;          /* and must not type a digit on release */
        return;
    }
    if (ev->type == KEV_PRESS && ev->key != KEY_PTT && !locked)
        snd(SND_KEY);
    if (locked && !(ev->key == KEY_FM && ev->type == KEV_LONG) && side_short(ev->key) != FN_MONITOR) {
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
    else if (screen == SCR_MSG)
        key_msg(ev);
    else if (screen == SCR_APRS)
        key_aprs(ev);
    else if (screen == SCR_EDIT)
        key_edit(ev);
    else if (screen == SCR_PAGER)
        key_pager(ev);
    else if (screen == SCR_FSCAN)
        key_fscan(ev);
    else if (screen == SCR_FM)
        key_fm(ev);
    else if (ev->type == KEV_PRESS && (ev->key == KEY_BACK || ev->key == KEY_MENU)) {
        screen = SCR_MENU;
        draw_menu();
    }
}

/* incoming packets, receipts to send, and telling the user */
static void messages(uint32_t now)
{
    int e = msg_poll();
    if (e & MSG_EV_ROLL) {
        uint32_t end;
        int r = roll_rx(msg_roll_burst(&end), end);
        debug_puts(r > 0 ? "rolling: in step\r\n" : r < 0 ? "rolling: another key\r\n" : "rolling: bad burst\r\n");
        if (r)
            show_toast(r > 0 ? "Rolling scramble" : "Scramble key differs");
        if (r && screen == SCR_MAIN)
            draw_hint();
    }
    if (e & MSG_EV_TAIL && stock.ste) {         /* the sender's tail: mute before the squelch closes */
        radio_audio_hold(HOLD_TAIL, 1);
        tail_held = 1;
    }
    if (e & MSG_EV_RECEIPT_DUE && !searching) {
        int scanning = scan_mode() != SCAN_OFF;
        voice_stop();
        if (!scanning)
            watch_pause(1);
        msg_send_receipt(watch_slot(watch_rx_slot())->low_power);
        if (scanning)
            radio_retune();
        else
            watch_pause(0);
        if (screen == SCR_MAIN)
            draw_main();
    }
    if (e & MSG_EV_NEW) {
        const msg_t *m = msg_get(0);
        char t[24];
        debug_puts("message: ");
        debug_puts(m->text);
        debug_puts("\r\n");
        apply_backlight(1);
        last_key = now;
        snd(SND_OPEN);
        if (screen == SCR_MSG) {
            msg_read_all();
            msg_scroll = 0;
        } else {
            memcpy(put_str(t, "MSG "), m->text, 19);
            t[23] = 0;
            show_toast(t);
            toast_until = now + 4000;
        }
    }
    if (e & MSG_EV_DELIVERED && screen != SCR_MSG)
        show_toast("Message delivered");
    if (!e)
        return;
    if (screen == SCR_MSG)
        draw_msg();
    else if (screen == SCR_MAIN)
        draw_status(), draw_hint();
}

static void aprs_events(uint32_t now)
{
    int e = aprs_poll();
    if (e & APRS_EV_ACK_DUE && !searching) {
        int scanning = scan_mode() != SCAN_OFF;
        voice_stop();
        if (!scanning)
            watch_pause(1);
        aprs_send_ack(watch_slot(watch_rx_slot())->low_power);
        if (scanning)
            radio_retune();
        else
            watch_pause(0);
    }
    if (e & APRS_EV_NEW) {
        const aprs_pkt_t *p = aprs_get(0);
        debug_puts("APRS ");
        debug_puts(p->from);
        debug_puts(": ");
        debug_puts(p->text);
        debug_puts("\r\n");
        apply_backlight(1);
        last_key = now;
        snd(SND_OPEN);
        aprs_read_all();
        if (aprs_sel >= 0 && ++aprs_sel >= aprs_count())
            aprs_sel = -1;                      /* keep the picked station picked */
    }
    if (e && screen == SCR_APRS)
        draw_aprs();
}

static void pager_events(uint32_t now)
{
    int e = pocsag_poll();
    if (e & PAGE_EV_NEW) {
        const page_t *p = pocsag_get(0);
        debug_puts("page ");
        debug_puts(p->from);
        debug_puts(": ");
        debug_puts(p->text);
        debug_puts("\r\n");
        apply_backlight(1);
        last_key = now;
        snd(SND_OPEN);
        if (pager_sel >= 0 && ++pager_sel >= pocsag_count())
            pager_sel = -1;
    }
    if (e && screen == SCR_PAGER)
        draw_pager();
}

/* the PC wrote channels or settings through program mode (src/progmode.c) */
static volatile uint8_t reload;

void progmode_changed(void)
{
    reload = 1;
}

/* Stock's VOX: the microphone level on ADC channel 0 against a threshold, 8 lower while transmitting;
   it holds for VOX delay after the level drops. Not while something is received or going on. */
static void vox_tick(uint32_t now)
{
    static const uint8_t level[11] = {127, 21, 31, 41, 51, 61, 71, 81, 91, 101, 111};
    static uint8_t vox_tx;
    static uint32_t vox_until, next;
    if (!stock.vox || screen != SCR_MAIN || (int32_t)(now - next) < 0)
        return;
    next = now + 50;
    if (vox_tx && !tx_active()) {
        vox_tx = 0;
        return;
    }
    int loud = (adc_read(0) >> 4) > level[stock.vox < 11 ? stock.vox : 10] - (vox_tx ? 8 : 0);
    if (loud)
        vox_until = now + (stock.voxd + 5u) * 100;
    if (vox_tx) {
        if ((int32_t)(now - vox_until) >= 0) {
            vox_tx = 0;
            ptt_up();
        }
    } else if (loud && !tx_active() && !tx_timed_out && !radio_busy() && !searching && scan_mode() == SCAN_OFF) {
        vox_tx = 1;
        ptt_down();
    }
}

void ui_tick(void)
{
    uint32_t now = millis();
    int ev = WATCH_EV_NONE;

    if (reload && !tx_active()) {
        reload = 0;
        load_settings();                /* also the slots' channels, as written */
        watch_update(watch_selected() ^ 1);
        watch_update(watch_selected());
        if (data_mode == 1)             /* apply() set the modem up for messages */
            aprs_listen(1);
        else if (data_mode == 2)
            pocsag_listen(1);
        redraw();
    }

    int want = screen == SCR_APRS ? 1 : screen == SCR_PAGER ? 2 : 0;
    if (want != data_mode) {                    /* the modem listens for what is on screen */
        if (data_mode == 1)
            aprs_listen(0);
        else if (data_mode == 2)
            pocsag_listen(0);
        data_mode = (uint8_t)want;
        if (want == 1)
            aprs_listen(1);
        else if (want == 2)
            pocsag_listen(1);
    }
    if (!tx_active()) {
        if (data_mode == 1)
            aprs_events(now);
        else if (data_mode == 2)
            pager_events(now);
        else
            messages(now);
    }
    if (tx_active()) {
        if (tot_s[stock.tot] && tx_seconds() >= tot_s[stock.tot]) {
            tx_stop();
            tx_timed_out = 1;           /* until PTT is released */
            debug_puts("TX time-out\r\n");
            draw_main();
        } else if (screen == SCR_MAIN && (int32_t)(now - next_tx_draw) >= 0) {
            next_tx_draw = now + 1000;
            draw_hint();
        }
        last_key = now;                 /* keeps the backlight on */
    } else if (msg_receiving() || aprs_receiving() || pocsag_receiving()) {
        /* a packet is arriving: no retuning until it is complete */
    } else if (voice_busy()) {
        /* no retuning while a sound plays: each retune switches the BK4819's audio stage, which the
           speaker (on for the sound) turns into a pop; stock stops the receiver for its prompts */
    } else if (searching) {
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
    } else if (!save_tick(now)) {
        ev = watch_poll();
        if (ev != WATCH_EV_NONE && screen == SCR_MAIN) {
            draw_slot(watch_rx_slot());
            draw_status();
        }
    }
    if (ev == WATCH_EV_OPEN)
        apply_backlight(1), last_key = now;

    if (screen == SCR_MAIN && !tx_active() && (int32_t)(now - next_meter) >= 0) {
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
    int typing = screen == SCR_MSG || screen == SCR_APRS || screen == SCR_EDIT || screen == SCR_PAGER;
    if (typing && ((tap_shown && !tap_pending()) ||          /* the cycling letter is settled */
                   (toast_until && (int32_t)(now - toast_until) >= 0))) {
        toast_until = 0;
        if (screen == SCR_MSG)
            draw_msg_compose();
        else if (screen == SCR_APRS)
            draw_aprs_compose();
        else if (screen == SCR_PAGER)
            draw_pager();
        else
            draw_edit_field();
    }
    if (screen == SCR_MAIN && (int32_t)(now - next_status) >= 0) {
        next_status = now + 5000;
        draw_status();
    }
    save_if_changed(now);
    if ((int32_t)(now - next_battery_warn) >= 0) {   /* lowest battery level: say so every 2 min */
        next_battery_warn = now + 10000;
        if (battery_bars() == 0) {
            next_battery_warn = now + 120000;
            snd(SND_LOW_VOLTAGE);
            show_toast("Low voltage");
            if (screen == SCR_MAIN)
                draw_hint();
        }
    }
    alarm_tick(now);
    vox_tick(now);
    if (tail_held && !radio_busy()) {
        tail_held = 0;
        radio_audio_hold(HOLD_TAIL, 0);
    }
    if (tail_until && (int32_t)(now - tail_until) >= 0) {
        tail_until = 0;
        radio_audio_hold(HOLD_TAIL, 0);
    }
    if (stock.autolk && !locked && screen == SCR_MAIN && now - last_key > stock.autolk * 5000u) {
        locked = 1;
        draw_status();
    }
    if (screen == SCR_MENU && !tx_active() && now - last_key > quit_s[stock.menuquit] * 1000u) {
        editing = 0;
        screen = SCR_MAIN;
        draw_main();
    }
    if (backlight_s[stock.abr] && light_on && !radio_open() &&
        now - last_key > (uint32_t)backlight_s[stock.abr] * 1000)
        apply_backlight(0);
}
