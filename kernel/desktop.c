#include "desktop.h"
#include "framebuffer.h"
#include "fatimg.h"
#include "colors.h"
#include "io.h"
#include "mouse.h"
#include "cursor.h"

#define WALLPAPER_PATH "/system/WALLPAPER.BMP"

/* Dock geometry — from the Figma spec. */
#define DOCK_H         74
#define DOCK_MARGIN    20      /* gap from screen bottom */
#define DOCK_RADIUS    6
#define DOCK_WHITE_ALPHA  77   /* 30% of 255 ≈ 77 */
#define DOCK_ICON_H    50
#define DOCK_ICON_GAP  15

static void draw_wallpaper(void)
{
    if (fatimg_draw_bmp(WALLPAPER_PATH) != 0)
        framebuffer_clear(COLOR_WAVE_BG);
}

/* Blend `fg` over `bg` by alpha (0..255). */
static u32 blend(u32 fg, u32 bg, u8 alpha)
{
    u32 fr = (fg >> 16) & 0xFF, fg_g = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    u32 br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    u32 inv = 255 - alpha;
    u32 r = (fr * alpha + br * inv) / 255;
    u32 g = (fg_g * alpha + bg_g * inv) / 255;
    u32 b = (fb * alpha + bb * inv) / 255;
    return (r << 16) | (g << 8) | b;
}

/* Draw a rounded rectangle. Only rounds the top corners here
 * since the dock sits near the bottom of the screen. */
static void fill_round_rect_top(int x0, int y0, int w, int h, int r,
                                u32 (*pixel_fn)(int, int, void *),
                                void *ctx)
{
    for (int y = 0; y < h; y++) {
        int inset = 0;
        if (y < r) {
            /* Circle row: x-inset for the top corners */
            int dy = r - y - 1;
            int dx = r;
            /* Solve dx^2 + dy^2 = r^2 -> dx = sqrt(r^2 - dy^2) */
            while (dx > 0 && dx * dx + dy * dy > r * r) dx--;
            inset = r - dx;
        }
        for (int x = inset; x < w - inset; x++) {
            u32 bg = pixel_fn(x0 + x, y0 + y, ctx);
            u32 out = blend(0x00FFFFFF, bg, DOCK_WHITE_ALPHA);
            framebuffer_put_pixel(x0 + x, y0 + y, out);
        }
    }
}

/* Read the current framebuffer pixel (fallback for non-dock rows). */
static u32 read_fb(int x, int y, void *ctx)
{
    (void)ctx;
    return framebuffer_get_pixel((u32)x, (u32)y);
}

static void draw_dock(void)
{
    int sw = (int)framebuffer_get_width();
    int sh = (int)framebuffer_get_height();

    int dock_w = 700;
    if (dock_w > sw - 40) dock_w = sw - 40;

    int dock_x = (sw - dock_w) / 2;
    int dock_y = sh - DOCK_H - DOCK_MARGIN;

    /* Translucent white bar with rounded top corners. */
    fill_round_rect_top(dock_x, dock_y, dock_w, DOCK_H,
                        DOCK_RADIUS, read_fb, 0);

    /* Icons. Placeholder squares for now. */
    int n_icons = 5;
    int icon_size = DOCK_ICON_H;
    int total_w = n_icons * icon_size + (n_icons - 1) * DOCK_ICON_GAP;
    int ix0 = dock_x + (dock_w - total_w) / 2;
    int iy0 = dock_y + (DOCK_H - icon_size) / 2;

    for (int i = 0; i < n_icons; i++) {
        int ix = ix0 + i * (icon_size + DOCK_ICON_GAP);
        /* Placeholder color circle */
        u32 colors[5] = {
            0x00FF6B6B, 0x006BCBFF, 0x00FFD93D, 0x006BFFA0, 0x00C77DFF
        };
        for (int yy = 0; yy < icon_size; yy++) {
            for (int xx = 0; xx < icon_size; xx++) {
                framebuffer_put_pixel(ix + xx, iy0 + yy, colors[i]);
            }
        }
    }
}

void desktop_enter(void)
{
    draw_wallpaper();
    draw_dock();

    cursor_reset();
    for (;;) {
        cursor_tick();
        __asm__ volatile ("hlt");
    }
}