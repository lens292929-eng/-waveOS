#include "desktop.h"
#include "framebuffer.h"
#include "colors.h"
#include "font.h"
#include "keyboard.h"

#define DESKTOP_BG       0x00151A24
#define DESKTOP_BG_2     0x001D2633

#define MENUBAR_BG       0x00E8E8E8
#define MENUBAR_TEXT     0x00202020

#define DOCK_BG          0x00D8D8D8
#define DOCK_BORDER      0x00B8B8B8

#define TEXT_COLOR       0x00FFFFFF
#define SUBTEXT_COLOR    0x00AAB4C0

#define ACCENT           0x004A90E2

#define MENUBAR_HEIGHT   26
#define DOCK_HEIGHT      72


static void desktop_keyboard_handler(const keyboard_event_t *event)
{
    if (!event->pressed)
        return;

    /* Desktop keyboard input will go here. */
}


/* --------------------------------------------------------- */
/* Wallpaper                                                  */
/* --------------------------------------------------------- */

static void draw_wallpaper(void)
{
    u32 w = framebuffer_get_width();
    u32 h = framebuffer_get_height();

    framebuffer_fill_rect(
        0,
        0,
        w,
        h,
        DESKTOP_BG
    );

    /*
     * Large geometric shapes to make the background
     * feel less like a flat framebuffer.
     */

    framebuffer_fill_rect(
        0,
        MENUBAR_HEIGHT,
        w,
        h - MENUBAR_HEIGHT,
        DESKTOP_BG
    );

    /* Large accent blocks */
    framebuffer_fill_rect(
        0,
        h / 2,
        w / 3,
        h / 2,
        DESKTOP_BG_2
    );

    framebuffer_fill_rect(
        (int)w - (int)w / 3,
        h / 3,
        w / 3,
        h / 2,
        DESKTOP_BG_2
    );
}


/* --------------------------------------------------------- */
/* Menu bar                                                    */
/* --------------------------------------------------------- */

static void draw_menu_bar(void)
{
    u32 w = framebuffer_get_width();

    framebuffer_fill_rect(
        0,
        0,
        w,
        MENUBAR_HEIGHT,
        MENUBAR_BG
    );

    /*
     * Text drawing can be added once we use the font
     * renderer's positioned text function.
     */
}


/* --------------------------------------------------------- */
/* Dock                                                       */
/* --------------------------------------------------------- */

static void draw_dock_icon(
    int x,
    int y,
    u32 color
)
{
    /*
     * Simple rounded-ish icon placeholder.
     * Once we have better primitives, this can become
     * an actual app icon.
     */

    framebuffer_fill_rect(
        x,
        y,
        48,
        48,
        color
    );
}


static void draw_dock(void)
{
    u32 w = framebuffer_get_width();
    u32 h = framebuffer_get_height();

    int dock_width = 420;
    int dock_height = DOCK_HEIGHT;

    int dock_x = ((int)w - dock_width) / 2;
    int dock_y = (int)h - dock_height - 12;

    /*
     * Dock shadow.
     */
    framebuffer_fill_rect(
        dock_x + 3,
        dock_y + 3,
        dock_width,
        dock_height,
        0x00505050
    );

    /*
     * Dock itself.
     */
    framebuffer_fill_rect(
        dock_x,
        dock_y,
        dock_width,
        dock_height,
        DOCK_BG
    );

    /*
     * Top border.
     */
    framebuffer_fill_rect(
        dock_x,
        dock_y,
        dock_width,
        2,
        DOCK_BORDER
    );

    /*
     * Icons.
     */

    int icon_y = dock_y + 12;

    draw_dock_icon(
        dock_x + 18,
        icon_y,
        0x005B9BD5
    );

    draw_dock_icon(
        dock_x + 78,
        icon_y,
        0x0070AD47
    );

    draw_dock_icon(
        dock_x + 138,
        icon_y,
        0x00ED7D31
    );

    draw_dock_icon(
        dock_x + 198,
        icon_y,
        0x0084472C
    );

    draw_dock_icon(
        dock_x + 258,
        icon_y,
        0x006B5B95
    );

    draw_dock_icon(
        dock_x + 318,
        icon_y,
        0x002F75B5
    );
}


/* --------------------------------------------------------- */
/* Desktop                                                     */
/* --------------------------------------------------------- */

void desktop_draw(void)
{
    draw_wallpaper();

    draw_menu_bar();

    draw_dock();
}


void desktop_enter(void)
{
    desktop_draw();

    keyboard_set_handler(
        desktop_keyboard_handler
    );
}