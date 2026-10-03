#include "framebuffer.h"
#include "seabios_font.h"

#define FONT_WIDTH   8
#define FONT_HEIGHT  16
#define LINE_HEIGHT  (FONT_HEIGHT + 2)

#define MARGIN_X     16
#define MARGIN_Y     16

static u32 *framebuffer;

static u32 screen_width;
static u32 screen_height;
static u32 screen_pitch;

static u32 cursor_x = MARGIN_X;
static u32 cursor_y = MARGIN_Y;

static u8 cursor_visible = 1;

static u32 default_fg = 0x00FFFFFF;
static u32 default_bg = 0x00000000;

// ============================================================
// Init
// ============================================================

void framebuffer_init(BootInfo *boot_info)
{
    framebuffer  = (u32 *)(usize)boot_info->framebuffer;
    screen_width = boot_info->width;
    screen_height = boot_info->height;
    screen_pitch  = boot_info->pitch;

    cursor_x = MARGIN_X;
    cursor_y = MARGIN_Y;
}

// ============================================================
// Pixel
// ============================================================

void framebuffer_put_pixel(u32 x, u32 y, u32 color)
{
    if (!framebuffer) return;
    if (x >= screen_width || y >= screen_height) return;

    framebuffer[y * screen_pitch + x] = color;
}

u32 framebuffer_get_pixel(u32 x, u32 y)
{
    if (!framebuffer) return 0;
    if (x >= screen_width || y >= screen_height) return 0;
    return framebuffer[y * screen_pitch + x];
}

// ============================================================
// Clear
// ============================================================

void framebuffer_clear(u32 color)
{
    if (!framebuffer) return;

    for (u32 y = 0; y < screen_height; y++) {
        u32 *row = framebuffer + y * screen_pitch;
        for (u32 x = 0; x < screen_width; x++)
            row[x] = color;
    }

    cursor_x = MARGIN_X;
    cursor_y = MARGIN_Y;
}

void framebuffer_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    for (u32 dy = 0; dy < h; dy++)
        for (u32 dx = 0; dx < w; dx++)
            framebuffer_put_pixel(x + dx, y + dy, color);
}

// ============================================================
// Char
// ============================================================

void framebuffer_draw_char(char c, u32 x, u32 y,
                           u32 foreground, u32 background)
{
    const u8 *glyph = &seabios_font[(u8)c * FONT_HEIGHT];

    for (u32 row = 0; row < FONT_HEIGHT; row++) {
        u8 bits = glyph[row];

        for (u32 col = 0; col < FONT_WIDTH; col++) {
            u32 color = (bits & (0x80 >> col))
                      ? foreground
                      : background;

            framebuffer_put_pixel(x + col, y + row, color);
        }
    }
}

// ============================================================
// Scroll — shift everything up by LINE_HEIGHT pixels
// ============================================================

static void scroll_up(void)
{
    // Move every row from (MARGIN_Y + LINE_HEIGHT) up to MARGIN_Y.
    for (u32 y = MARGIN_Y; y + LINE_HEIGHT < screen_height; y++) {
        u32 *dst = framebuffer + y * screen_pitch;
        u32 *src = framebuffer + (y + LINE_HEIGHT) * screen_pitch;

        for (u32 x = 0; x < screen_width; x++)
            dst[x] = src[x];
    }

    // Clear the bottom LINE_HEIGHT rows.
    for (u32 y = screen_height - LINE_HEIGHT; y < screen_height; y++) {
        u32 *row = framebuffer + y * screen_pitch;
        for (u32 x = 0; x < screen_width; x++)
            row[x] = default_bg;
    }

    // Keep cursor at the last visible line.
    if (cursor_y >= LINE_HEIGHT)
        cursor_y -= LINE_HEIGHT;
}

// ============================================================
// Newline
// ============================================================

void print_newline(void)
{
    cursor_x = MARGIN_X;
    cursor_y += LINE_HEIGHT;

    if (cursor_y + FONT_HEIGHT > screen_height)
        scroll_up();
}

// ============================================================
// Public print API
// ============================================================


void cursor_show(void)
{
    framebuffer_fill_rect(cursor_x,
                          cursor_y + FONT_HEIGHT - 2,
                          FONT_WIDTH,
                          2,
                          default_fg);

    cursor_visible = 1;
}

void cursor_hide(void)
{
    framebuffer_fill_rect(cursor_x,
                          cursor_y + FONT_HEIGHT - 2,
                          FONT_WIDTH,
                          2,
                          default_bg);

    cursor_visible = 0;
}

void cursor_blink(void)
{
    if (cursor_visible)
        cursor_hide();
    else
        cursor_show();
}

void print_set_colors(u32 foreground, u32 background)
{
    default_fg = foreground;
    default_bg = background;
}

void printc(char c)
{
    cursor_show();

    if (c == '\n') {
        cursor_hide();
        print_newline();
        cursor_show();
        return;
    }

    if (c == '\r') {
        cursor_hide();
        cursor_x = MARGIN_X;
        cursor_show();
        return;
    }

    if (c == '\b') {
        cursor_hide();

        if (cursor_x > MARGIN_X) {
            cursor_x -= FONT_WIDTH + 1;
            framebuffer_draw_char(' ', cursor_x, cursor_y,
                                 default_fg, default_bg);
        }

        cursor_show();
        return;
    }

    if (cursor_x + FONT_WIDTH + 1 > screen_width - MARGIN_X) {
        cursor_hide();
        print_newline();
    }

    cursor_hide();

    framebuffer_draw_char(c, cursor_x, cursor_y,
                          default_fg, default_bg);

    cursor_x += FONT_WIDTH + 1;

    cursor_show();
}


void print(const char *str)
{
    while (*str)
        printc(*str++);
}

void print_u64(u64 n) {
    char buf[21];
    int i = 0;
    if (n == 0) { printc('0'); return; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) printc(buf[--i]);
}

// Keep the old API for compatibility.
void framebuffer_print(const char *str, u32 fg, u32 bg)
{
    u32 saved_fg = default_fg;
    u32 saved_bg = default_bg;

    default_fg = fg;
    default_bg = bg;

    print(str);

    default_fg = saved_fg;
    default_bg = saved_bg;
}

// framebuffer.c
void print_tagged(const char *tag, u32 tag_color, const char *msg)
{
    u32 saved_fg = default_fg;
    u32 saved_bg = default_bg;

    print_set_colors(tag_color, default_bg);
    print("[");
    print(tag);
    print("] ");

    default_fg = saved_fg;
    default_bg = saved_bg;

    print(msg);
}

u32 framebuffer_get_width(void)
{
    return screen_width;
}

u32 framebuffer_get_height(void)
{
    return screen_height;
}