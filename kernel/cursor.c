#include "cursor.h"
#include "framebuffer.h"
#include "colors.h"
#include "mouse.h"

/* ============================================================
 * Cursor sprite (16x16 arrow)
 * ============================================================ */

static const u8 cursor_mask[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0},
    {1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0},
    {1,1,1,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
};

/* ============================================================
 * Soft drop shadow (26x26)
 * ============================================================
 * Generated offline: the arrow was offset by (2, 2) and blurred
 * with a 3px radial falloff, capped at 110/255 opacity.
 */

#define CURSOR_SHADOW_W 26
#define CURSOR_SHADOW_H 26

static const u8 cursor_shadow[CURSOR_SHADOW_H][CURSOR_SHADOW_W] = {
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  1,  4,  4,  4,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  4, 11, 15, 14,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  9, 21, 30, 32, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 12, 32, 48, 53, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 39, 62, 74, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 89, 87, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 96,102, 87, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 97,109,102, 87, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 97,110,109,102, 87, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 97,110,110,109,102, 87, 66, 44, 23,  8,  1,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 97,110,110,109,105, 93, 75, 53, 32, 14,  4,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 96,106,106,102, 95, 84, 67, 48, 30, 15,  4,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 40, 70, 89, 95, 92, 87, 80, 69, 51, 33, 21, 11,  4,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 13, 39, 62, 74, 74, 70, 70, 66, 55, 35, 17,  9,  4,  1,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0, 12, 32, 48, 53, 48, 45, 51, 55, 50, 33, 12,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  9, 21, 30, 32, 24, 20, 34, 50, 54, 43, 22,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  4, 11, 15, 14,  8,  5, 22, 42, 49, 42, 26,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  1,  4,  4,  4,  1,  1, 12, 29, 36, 33, 22,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4, 14, 21, 21, 14,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  4,  8,  8,  4,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
};

/* ============================================================
 * State
 * ============================================================ */

static u32 cursor_background[CURSOR_DIRTY_SIZE * CURSOR_DIRTY_SIZE];
static int cursor_background_valid = 0;

static int cursor_last_x = -1;
static int cursor_last_y = -1;

/* ============================================================
 * Low-level pixel helpers with clipping
 * ============================================================ */

static u32 fb_get(int x, int y)
{
    if (x < 0 || y < 0) return 0;
    if (x >= (int)framebuffer_get_width()) return 0;
    if (y >= (int)framebuffer_get_height()) return 0;
    return framebuffer_get_pixel((u32)x, (u32)y);
}

static void fb_put(int x, int y, u32 color)
{
    if (x < 0 || y < 0) return;
    if (x >= (int)framebuffer_get_width()) return;
    if (y >= (int)framebuffer_get_height()) return;
    framebuffer_put_pixel((u32)x, (u32)y, color);
}

static u32 blend_toward_black(u32 bg, u8 alpha)
{
    if (alpha == 0) return bg;

    u32 br   = (bg >> 16) & 0xFF;
    u32 bg_g = (bg >> 8)  & 0xFF;
    u32 bb   =  bg        & 0xFF;

    u32 inv = 255 - alpha;

    u32 r = (br   * inv) / 255;
    u32 g = (bg_g * inv) / 255;
    u32 b = (bb   * inv) / 255;

    return (r << 16) | (g << 8) | b;
}

/* ============================================================
 * Cursor drawing
 * ============================================================ */

void cursor_draw(int x, int y)
{
    /* Soft shadow: centered on the sprite, offset baked into the
     * mask already. (CURSOR_W - CURSOR_SHADOW_W) is negative,
     * so this centers the larger shadow array on the sprite. */
    int shadow_ox = x + (CURSOR_W - CURSOR_SHADOW_W) / 2;
    int shadow_oy = y + (CURSOR_H - CURSOR_SHADOW_H) / 2;

    for (int row = 0; row < CURSOR_SHADOW_H; row++) {
        for (int col = 0; col < CURSOR_SHADOW_W; col++) {
            u8 alpha = cursor_shadow[row][col];
            if (alpha == 0) continue;

            int px = shadow_ox + col;
            int py = shadow_oy + row;

            u32 bg = fb_get(px, py);
            fb_put(px, py, blend_toward_black(bg, alpha));
        }
    }

    /* White outline around the arrow. */
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            if (!cursor_mask[row][col]) continue;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    fb_put(x + col + dx, y + row + dy, PRISM_TEXT);
                }
            }
        }
    }

    /* Black fill on top. */
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            if (cursor_mask[row][col])
                fb_put(x + col, y + row, PRISM_BLACK);
        }
    }
}

/* ============================================================
 * Background save / restore
 * ============================================================ */

static void cursor_save_background(int x, int y)
{
    for (int row = 0; row < CURSOR_DIRTY_SIZE; row++) {
        for (int col = 0; col < CURSOR_DIRTY_SIZE; col++) {
            cursor_background[row * CURSOR_DIRTY_SIZE + col] =
                fb_get(x + col, y + row);
        }
    }
}

static void cursor_restore_background(int x, int y)
{
    for (int row = 0; row < CURSOR_DIRTY_SIZE; row++) {
        for (int col = 0; col < CURSOR_DIRTY_SIZE; col++) {
            fb_put(x + col, y + row,
                   cursor_background[row * CURSOR_DIRTY_SIZE + col]);
        }
    }
}

/* ============================================================
 * Update
 * ============================================================ */

void cursor_update(int old_x, int old_y, int new_x, int new_y)
{
    if (cursor_background_valid)
        cursor_restore_background(old_x - CURSOR_PAD,
                                  old_y - CURSOR_PAD);

    cursor_save_background(new_x - CURSOR_PAD,
                           new_y - CURSOR_PAD);

    cursor_draw(new_x, new_y);

    cursor_background_valid = 1;
}

void cursor_tick(void)
{
    int mx = mouse_get_x();
    int my = mouse_get_y();

    if (mx == cursor_last_x && my == cursor_last_y)
        return;

    if (cursor_last_x < 0) {
        cursor_save_background(mx - CURSOR_PAD, my - CURSOR_PAD);
        cursor_draw(mx, my);
        cursor_background_valid = 1;
    } else {
        cursor_update(cursor_last_x, cursor_last_y, mx, my);
    }

    cursor_last_x = mx;
    cursor_last_y = my;
}

/* ============================================================
 * Reset
 * ============================================================ */

void cursor_invalidate(void)
{
    cursor_background_valid = 0;
}

void cursor_reset(void)
{
    cursor_background_valid = 0;
    cursor_last_x = -1;
    cursor_last_y = -1;
}