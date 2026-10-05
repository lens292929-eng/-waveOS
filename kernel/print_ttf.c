/* print_ttf.c */
#include "font_ttf.h"

void print_ttf_string_as_segoeui(const char *s, u32 x, u32 y)
{
    while (*s) {
        char c = *s++;

        if (c == '\n') {
            x = 0;
            y += 24;
            continue;
        }

        int adv = font_ttf_draw_char(c, x, y, 0x00FFFFFF, 0x00000000);
        x += adv;
    }
}