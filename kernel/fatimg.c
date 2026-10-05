#include "fatimg.h"
#include "fat32.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "colors.h"
#include "io.h"

/* ============================================================
 * Little-endian readers
 * ============================================================ */

static u16 rd_u16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 rd_u32(const u8 *p)
{
    return (u32)p[0]
         | ((u32)p[1] << 8)
         | ((u32)p[2] << 16)
         | ((u32)p[3] << 24);
}

static i32 rd_i32(const u8 *p)
{
    return (i32)rd_u32(p);
}

/* ============================================================
 * Shared image buffer
 * ============================================================
 *
 * Sized to hold the largest BMP we intend to display. 8 MB is
 * enough for a 1080p 24-bit BMP plus change. Bump this if you
 * ever want to load larger images — and remember to bump
 * KERNEL_PAGES in boot.c to match, or the .bss will exceed the
 * kernel's allocated region.
 */

#define FATIMG_MAX_BYTES (8 * 1024 * 1024)
static u8 image_buf[FATIMG_MAX_BYTES];

/* ============================================================
 * Draw a BMP, scaled to cover the screen.
 * ============================================================
 *
 * "Cover" means: scale preserving aspect ratio so the image
 * fills every pixel of the screen. If the image's aspect ratio
 * differs from the screen's, the extra edges are cropped by the
 * screen bounds — no letterbox bars, no distortion.
 *
 * The image is centered on the screen. Off-screen pixels are
 * skipped, so we never waste time drawing into the void.
 *
 * Works at any framebuffer resolution. No hardcoded sizes.
 */

int fatimg_draw_bmp(const char *fat_path)
{
    /* ---- 1. Read the file ---------------------------------- */

    int n = fat32_read_file(fat_path, (char *)image_buf, FATIMG_MAX_BYTES);
    if (n < 0) return -1;
    if (n < 54) return -1;

    /* ---- 2. Parse the BMP header --------------------------- */

    if (image_buf[0] != 'B' || image_buf[1] != 'M') return -1;

    u32 data_offset = rd_u32(image_buf + 10);
    u32 width       = rd_u32(image_buf + 18);
    i32 height_sig  = rd_i32(image_buf + 22);
    u16 bpp         = rd_u16(image_buf + 28);
    u32 compression = rd_u32(image_buf + 30);

    if (compression != 0) return -1;   /* uncompressed only */
    if (bpp != 24) return -1;          /* 24-bit only */

    int top_down = (height_sig < 0);
    u32 height   = top_down ? (u32)(-height_sig) : (u32)height_sig;

    if (width == 0 || height == 0) return -1;

    /* BMP rows are padded to 4-byte boundaries. */
    u32 row_stride = (width * 3 + 3) & ~3u;

    u32 needed = data_offset + row_stride * height;
    if (needed > (u32)n) return -1;

    /* ---- 3. Compute scale (cover, not fit) ----------------- */

    u32 screen_w = framebuffer_get_width();
    u32 screen_h = framebuffer_get_height();

    /*
     * Fixed-point 16.16 scale factors.
     *
     * scale_w = how much to scale so image width fills the screen.
     * scale_h = how much to scale so image height fills the screen.
     *
     * Taking the LARGER of the two guarantees we cover the screen.
     * The smaller dimension overflows off-screen and gets clipped.
     */

    u32 scale_w = (screen_w << 16) / width;
    u32 scale_h = (screen_h << 16) / height;
    u32 scale   = scale_w > scale_h ? scale_w : scale_h;

    if (scale == 0) scale = 1;   /* paranoia: never divide by zero */

    /* Scaled destination dimensions (may exceed screen). */
    u32 dst_w = (u32)(((u64)width  * scale) >> 16);
    u32 dst_h = (u32)(((u64)height * scale) >> 16);

    /* Center. These can go negative when the image is larger
     * than the screen in one dimension. */
    int dst_x0 = ((int)screen_w - (int)dst_w) / 2;
    int dst_y0 = ((int)screen_h - (int)dst_h) / 2;

    /* ---- 4. Blit ------------------------------------------- */

    for (u32 y = 0; y < dst_h; y++) {
        int dy = dst_y0 + (int)y;
        if (dy < 0 || dy >= (int)screen_h) continue;

        /* Map destination y back to source y via the inverse
         * of the scale. Since scale is 16.16, this is just a
         * shift-and-divide. */
        u32 src_y = (y << 16) / scale;
        if (src_y >= height) src_y = height - 1;

        /* BMP rows are bottom-up unless height was negative. */
        u32 src_row = top_down ? src_y : (height - 1 - src_y);
        const u8 *row = image_buf + data_offset + src_row * row_stride;

        for (u32 x = 0; x < dst_w; x++) {
            int dx = dst_x0 + (int)x;
            if (dx < 0 || dx >= (int)screen_w) continue;

            u32 src_x = (x << 16) / scale;
            if (src_x >= width) src_x = width - 1;

            /* BMP stores BGR. Convert to our 0x00RRGGBB. */
            u8 b = row[src_x * 3 + 0];
            u8 g = row[src_x * 3 + 1];
            u8 r = row[src_x * 3 + 2];

            u32 color = ((u32)r << 16) | ((u32)g << 8) | b;

            framebuffer_put_pixel((u32)dx, (u32)dy, color);
        }
    }

    return 0;
}

/* ============================================================
 * View a BMP interactively.
 * ============================================================
 *
 * Draws the image scaled to cover the screen, waits for
 * Ctrl+Q, then clears and returns. IRQ1 is disabled during
 * the wait so the shell doesn't eat the keystrokes.
 */

int fatimg_view_bmp(const char *fat_path)
{
    if (fatimg_draw_bmp(fat_path) != 0) {
        print("fatimg: couldn't draw ");
        print(fat_path);
        printc('\n');
        return -1;
    }

    /* Disable IRQ1 (keyboard) while we poll. */
    u8 old_mask = inb(0x21);
    outb(0x21, old_mask | 0x02);

    int ctrl_held = 0;

    for (;;) {
        u8 status = inb(0x64);

        if (!(status & 0x01)) {
            __asm__ volatile ("hlt");
            continue;
        }

        u8 sc = inb(0x60);

        if (sc == 0x1D) { ctrl_held = 1; continue; }   /* Left Ctrl make */
        if (sc == 0x9D) { ctrl_held = 0; continue; }   /* Left Ctrl break */
        if (sc == 0x10 && ctrl_held) break;            /* Ctrl+Q */
    }

    /* Restore IRQ1. */
    outb(0x21, old_mask);

    framebuffer_clear(COLOR_WAVE_BG);
    return 0;
}