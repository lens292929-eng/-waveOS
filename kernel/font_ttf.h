#ifndef FONT_TTF_H
#define FONT_TTF_H

#include "types.h"

/* Load a TTF from FAT32 at the given pixel size.
 * Returns 0 on success, -1 on failure. */
int font_ttf_load(const char *fat_path, int size_px);

/* Is a TTF currently loaded? */
int font_ttf_ready(void);

/* Draw one character. Returns the advance width in pixels. */
int font_ttf_draw_char(char c, u32 x, u32 y, u32 fg, u32 bg);

#endif