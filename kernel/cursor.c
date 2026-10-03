#include "cursor.h"
#include "framebuffer.h"
#include "colors.h"
#include "mouse.h"

static u32 cursor_background[CURSOR_DIRTY_SIZE * CURSOR_DIRTY_SIZE];
static int cursor_background_valid = 0;

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

/* --------------------------------------------------------- */

void cursor_invalidate(void)
{
    cursor_background_valid = 0;
}

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

/* --------------------------------------------------------- */

void cursor_draw(int x, int y)
{
    /* Shadow: +2, +2 in black. */
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++)
            if (cursor_mask[row][col])
                fb_put(x + col + 2, y + row + 2, PRISM_BLACK);

    /* White outline: 8-neighbor spray around every mask pixel. */
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++) {
            if (!cursor_mask[row][col]) continue;

            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    fb_put(x + col + dx, y + row + dy, PRISM_TEXT);
        }

    /* Black fill on top. */
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++)
            if (cursor_mask[row][col])
                fb_put(x + col, y + row, PRISM_BLACK);
}

/* --------------------------------------------------------- */

static void cursor_save_background(int x, int y)
{
    for (int row = 0; row < CURSOR_DIRTY_SIZE; row++)
        for (int col = 0; col < CURSOR_DIRTY_SIZE; col++)
            cursor_background[row * CURSOR_DIRTY_SIZE + col] =
                fb_get(x + col, y + row);
}

static void cursor_restore_background(int x, int y)
{
    for (int row = 0; row < CURSOR_DIRTY_SIZE; row++)
        for (int col = 0; col < CURSOR_DIRTY_SIZE; col++)
            fb_put(x + col, y + row,
                   cursor_background[row * CURSOR_DIRTY_SIZE + col]);
}

/* --------------------------------------------------------- */

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

static int cursor_last_x = -1;
static int cursor_last_y = -1;

void cursor_tick(void)
{
    int mx = mouse_get_x();
    int my = mouse_get_y();

    if (mx == cursor_last_x && my == cursor_last_y)
        return;

    if (cursor_last_x < 0) {
        /* first draw */
        cursor_save_background(mx - CURSOR_PAD, my - CURSOR_PAD);
        cursor_draw(mx, my);
        cursor_background_valid = 1;
    } else {
        cursor_update(cursor_last_x, cursor_last_y, mx, my);
    }

    cursor_last_x = mx;
    cursor_last_y = my;
}

void cursor_reset(void)
{
    cursor_background_valid = 0;
    cursor_last_x = -1;
    cursor_last_y = -1;
}