#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"      // wherever u32/u8/usize live
#include "..\includes\boot_info.h"   // wherever BootInfo lives

void framebuffer_init(BootInfo *boot_info);
void framebuffer_clear(u32 color);

void framebuffer_put_pixel(u32 x, u32 y, u32 color);
u32 framebuffer_get_pixel(u32 x, u32 y);

void framebuffer_draw_char(char c, u32 x, u32 y,
                           u32 foreground, u32 background);

void framebuffer_print(const char *str, u32 foreground, u32 background);

// --- convenience API ---

// Print with the current default colors.
void print(const char *str);

// Change defaults used by print() and printc().
void print_set_colors(u32 foreground, u32 background);

// Single character, default colors.
void printc(char c);

// Position control.
void print_newline(void);
void print_clear(void);

// framebuffer.h
void print_tagged(const char *tag, u32 tag_color, const char *msg);

void framebuffer_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

void print_u64(u64 n);

void cursor_show(void);
void cursor_hide(void);
void cursor_blink(void);

u32 framebuffer_get_width(void);
u32 framebuffer_get_height(void);

#endif