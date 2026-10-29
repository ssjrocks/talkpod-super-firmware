/* 4 x 4 key matrix, side keys and PTT, after Jamie793/A36Plus_Radio Drivers/Key (MIT).
 *
 * Rows PB3..PB6 are driven low one at a time and the columns read; the side keys pull a column
 * low on their own, so they are checked first with every row high.
 *
 * PTT shares PA10 with the serial receive line. PA10 stays in USART mode (flashing without the
 * side key depends on it) and PTT is read from the pin level: pressed means low for 30 ms with
 * no serial byte received in the last 50 ms, so programming-cable traffic never looks like PTT.
 */
#include "keypad.h"
#include "board.h"
#include "debug.h"
#include "system.h"

#define ROWS_ALL (KEY_ROW0_PIN | KEY_ROW1_PIN | KEY_ROW2_PIN | KEY_ROW3_PIN)
#define COLS_ALL (KEY_COL0_PIN | KEY_COL1_PIN | KEY_COL2_PIN | KEY_COL3_PIN)

static const uint32_t col_pins[4] = {KEY_COL0_PIN, KEY_COL1_PIN, KEY_COL2_PIN, KEY_COL3_PIN};

static key_event_t queue[8];
static uint8_t q_head, q_tail;

static uint8_t raw_last, held;          /* matrix/side key: last raw reading, debounced key */
static uint32_t held_since, next_repeat;
static uint8_t long_sent;

static uint8_t ptt_low_scans, ptt_high_scans, ptt_down;
static uint32_t last_rx_count, last_rx_ms;

static void push(uint8_t key, uint8_t type)
{
    uint8_t next = (uint8_t)((q_head + 1) % 8);
    if (next == q_tail)
        return;                         /* full: drop */
    queue[q_head].key = key;
    queue[q_head].type = type;
    q_head = next;
}

int keypad_get(key_event_t *ev)
{
    if (q_tail == q_head)
        return 0;
    *ev = queue[q_tail];
    q_tail = (uint8_t)((q_tail + 1) % 8);
    return 1;
}

static int col_low(int c)
{
    return gpio_input_bit_get(KEY_PORT, col_pins[c]) == RESET;
}

static uint8_t read_raw(void)
{
    gpio_bit_set(KEY_PORT, ROWS_ALL);
    for (volatile int i = 0; i < 40; i++) {}
    if (col_low(0)) return KEY_TOP;
    if (col_low(1)) return KEY_SIDE1;
    if (col_low(2)) return KEY_SIDE2;

    uint8_t key = KEY_NONE;
    for (int row = 0; row < 4 && key == KEY_NONE; row++) {
        gpio_bit_set(KEY_PORT, ROWS_ALL);
        gpio_bit_reset(KEY_PORT, KEY_ROW0_PIN << row);
        for (volatile int i = 0; i < 40; i++) {}      /* ~3 us for the line to settle */
        for (int c = 0; c < 4; c++)
            if (col_low(c)) {
                key = (uint8_t)(row * 4 + c + 1);
                break;
            }
    }
    gpio_bit_set(KEY_PORT, ROWS_ALL);
    return key;
}

void keypad_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_set(KEY_PORT, ROWS_ALL);
    gpio_mode_set(KEY_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, ROWS_ALL);
    gpio_output_options_set(KEY_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, ROWS_ALL);
    gpio_mode_set(KEY_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, COLS_ALL);
    /* keep PA10 on the USART (its alternate function stays as the bootloader set it) */
    gpio_mode_set(PTT_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, PTT_PIN);
    last_rx_count = debug_rx_count();
}

static void scan_ptt(uint32_t now)
{
    uint32_t n = debug_rx_count();
    if (n != last_rx_count) {
        last_rx_count = n;
        last_rx_ms = now;
    }
    if (gpio_input_bit_get(PTT_PORT, PTT_PIN) == RESET) {
        ptt_high_scans = 0;
        if (ptt_low_scans < 255)
            ptt_low_scans++;
        if (!ptt_down && ptt_low_scans >= 3 && now - last_rx_ms >= 50) {
            ptt_down = 1;
            push(KEY_PTT, KEV_PRESS);
        }
    } else {
        ptt_low_scans = 0;
        if (ptt_down && ++ptt_high_scans >= 2) {
            ptt_down = 0;
            push(KEY_PTT, KEV_RELEASE);
        }
    }
}

void keypad_scan(void)
{
    uint32_t now = millis();
    scan_ptt(now);

    uint8_t raw = read_raw();
    uint8_t stable = (raw == raw_last);
    raw_last = raw;
    if (!stable)
        return;                         /* debounce: same reading on two scans in a row */

    if (raw != held) {
        if (held != KEY_NONE)
            push(held, KEV_RELEASE);
        held = raw;
        long_sent = 0;
        held_since = now;
        if (held != KEY_NONE)
            push(held, KEV_PRESS);
    } else if (held != KEY_NONE) {
        if (!long_sent && now - held_since >= KEY_LONG_MS) {
            long_sent = 1;
            next_repeat = now + KEY_REPEAT_MS;
            push(held, KEV_LONG);
        } else if (long_sent && (int32_t)(now - next_repeat) >= 0) {
            next_repeat += KEY_REPEAT_MS;
            push(held, KEV_REPEAT);
        }
    }
}

int ptt_pressed(void)
{
    return ptt_down;
}

const char *key_name(uint8_t key)
{
    static const char *const names[KEY_COUNT] = {
        "none", "MENU", "UP", "DOWN", "BACK", "1", "2", "3", "A/B", "4", "5", "6", "0",
        "7", "8", "9", "FM", "TOP", "SIDE1", "SIDE2", "PTT",
    };
    return key < KEY_COUNT ? names[key] : "?";
}
