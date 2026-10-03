#ifndef PIT_H
#define PIT_H

#include "types.h"

typedef void (*pit_callback_t)(void);

void pit_init(u32 hz);
void pit_handler(void);

u64 pit_ticks(void);
u64 pit_uptime_ms(void);
u64 pit_get_uptime_seconds(void);

void pit_sleep_ms(u64 ms);

void pit_set_cursor_callback(pit_callback_t callback);

#endif