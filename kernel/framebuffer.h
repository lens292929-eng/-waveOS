#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"
#include "..\\includes\\boot_info.h"

void framebuffer_init(BootInfo *boot_info);

void framebuffer_present(void);

void framebuffer_clear(u32 color);

void framebuffer_put_pixel(
    u32 x,
    u32 y,
    u32 color
);

u32 framebuffer_get_pixel(
    u32 x,
    u32 y
);

void framebuffer_draw_char(
    char c,
    u32 x,
    u32 y,
    u32 foreground,
    u32 background
);

void framebuffer_fill_rect(
    u32 x,
    u32 y,
    u32 w,
    u32 h,
    u32 color
);

void framebuffer_print(
    const char *str,
    u32 foreground,
    u32 background
);

void print(
    const char *str
);

void print_set_colors(
    u32 foreground,
    u32 background
);

void printc(
    char c
);

void print_newline(void);

void print_clear(void);

void print_tagged(
    const char *tag,
    u32 tag_color,
    const char *msg
);

void print_u64(u64 n);

void cursor_show(void);

void cursor_hide(void);

void cursor_blink(void);

u32 framebuffer_get_width(void);

u32 framebuffer_get_height(void);

#endif