#include "pit.h"
#include "io.h"

#define PIT_INPUT_HZ 1193182
#define SHELL_CURSOR_BLINK_TICKS 50

static volatile u64 uptime_ticks = 0;
static volatile u64 ticks = 0;

static u32 pit_freq = 0;

static volatile u32 cursor_blink_ticks = 0;

static pit_callback_t cursor_callback = 0;


void pit_init(u32 hz)
{
    if (hz == 0)
        hz = 100;

    pit_freq = hz;

    u32 divisor = PIT_INPUT_HZ / hz;

    outb(0x43, 0x36);

    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));

    ticks = 0;
    uptime_ticks = 0;
    cursor_blink_ticks = 0;
}


u64 pit_ticks(void)
{
    return ticks;
}


u64 pit_uptime_ms(void)
{
    if (pit_freq == 0)
        return 0;

    return (ticks * 1000) / pit_freq;
}


u64 pit_get_uptime_seconds(void)
{
    if (pit_freq == 0)
        return 0;

    return uptime_ticks / pit_freq;
}


void pit_set_cursor_callback(pit_callback_t callback)
{
    cursor_callback = callback;
}


void pit_handler(void)
{
    uptime_ticks++;
    ticks++;

    cursor_blink_ticks++;

    if (cursor_blink_ticks >= SHELL_CURSOR_BLINK_TICKS) {
        cursor_blink_ticks = 0;

        if (cursor_callback)
            cursor_callback();
    }
}


void pit_sleep_ms(u64 ms)
{
    u64 target = ticks + (ms * pit_freq) / 1000;

    while (ticks < target)
        __asm__ volatile ("hlt");
}