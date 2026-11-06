#include "evlog.h"
#include "debug.h"
#include "system.h"

/* 128 events of 10 bytes. RAM is the limit: about 2.4 KB were left above the minimum stack. */
#define EVLOG_N 128

static const char *const names[] = {"?", "BOOT", "TUNE", "OPEN", "CLOSE", "HOLD", "IRQ", "PKT",
                                    "PKT_LOST", "TX", "SQUELCH", "CARRIER"};

static struct __attribute__((packed)) {
    uint32_t ms, v;
    uint8_t ev, a;
} entry[EVLOG_N];
static uint8_t next, wrapped;

void evlog(uint8_t ev, uint8_t a, uint32_t v)
{
    entry[next].ms = millis();
    entry[next].v = v;
    entry[next].ev = ev;
    entry[next].a = a;
    next = (uint8_t)((next + 1) % EVLOG_N);
    if (!next)
        wrapped = 1;
}

void evlog_clear(void)
{
    next = 0;
    wrapped = 0;
}

/* "EVLOG now count", then "ms name a v" oldest first, all hex, then "END" */
void evlog_dump(void)
{
    int count = wrapped ? EVLOG_N : next;
    debug_puts("EVLOG ");
    debug_hex32(millis());
    debug_putc(' ');
    debug_hex16((uint16_t)count);
    debug_puts("\r\n");
    for (int i = 0; i < count; i++) {
        int k = wrapped ? (next + i) % EVLOG_N : i;
        debug_hex32(entry[k].ms);
        debug_putc(' ');
        debug_puts(entry[k].ev < sizeof names / sizeof names[0] ? names[entry[k].ev] : "?");
        debug_putc(' ');
        debug_hex16(entry[k].a);
        debug_putc(' ');
        debug_hex32(entry[k].v);
        debug_puts("\r\n");
        debug_flush();
        fwdgt_feed();                           /* 128 lines take 0.7 s */
    }
    debug_puts("END\r\n");
}
