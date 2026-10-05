#ifndef WAVE_CURSOR_H
#define WAVE_CURSOR_H

#include "types.h"

#define CURSOR_W 16
#define CURSOR_H 16
#define CURSOR_PAD 4
#define CURSOR_DIRTY_SIZE (CURSOR_W + CURSOR_PAD * 2)

void cursor_draw(int x, int y);
void cursor_update(int old_x, int old_y, int new_x, int new_y);
void cursor_tick(void);
void cursor_reset(void);

#endif /* WAVE_CURSOR_H */