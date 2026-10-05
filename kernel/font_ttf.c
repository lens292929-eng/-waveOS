#include "font_ttf.h"
#include "fat32.h"
#include "framebuffer.h"
#include "types.h"

#define STB_TRUETYPE_IMPLEMENTATION_DISABLED
#include "stb_truetype.h"

#define TTF_MAX_BYTES (4 * 1024 * 1024)

static u8  ttf_buf[TTF_MAX_BYTES];
static stbtt_fontinfo font_info;
static int font_loaded = 0;
static float font_scale = 1.0f;


static u32 blend_alpha(u32 fg, u32 bg, u8 alpha)
{
    u32 fg_r = (fg >> 16) & 0xFF;
    u32 fg_g = (fg >> 8)  & 0xFF;
    u32 fg_b =  fg        & 0xFF;

    u32 bg_r = (bg >> 16) & 0xFF;
    u32 bg_g = (bg >> 8)  & 0xFF;
    u32 bg_b =  bg        & 0xFF;

    u32 a   = alpha;
    u32 inv = 255 - a;

    u32 r = (fg_r * a + bg_r * inv) / 255;
    u32 g = (fg_g * a + bg_g * inv) / 255;
    u32 b = (fg_b * a + bg_b * inv) / 255;

    return (r << 16) | (g << 8) | b;
}

int font_ttf_load(const char *fat_path, int size_px)
{
    int n = fat32_read_file(fat_path, (char *)ttf_buf, TTF_MAX_BYTES);
    if (n < 0) return -1;

    int offset = stbtt_GetFontOffsetForIndex(ttf_buf, 0);
    if (offset < 0) return -1;

    if (!stbtt_InitFont(&font_info, ttf_buf, offset))
        return -1;

    font_scale = stbtt_ScaleForPixelHeight(&font_info, size_px);
    font_loaded = 1;
    return 0;
}

int font_ttf_ready(void) { return font_loaded; }

int font_ttf_draw_char(char c, u32 x, u32 y, u32 fg, u32 bg)
{
    if (!font_loaded) return 0;

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font_info, c, &advance, &lsb);

    int w, h, xoff, yoff;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(
        &font_info, 0, font_scale, c, &w, &h, &xoff, &yoff);

    if (!bitmap) return (int)(advance * font_scale);

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            u8 alpha = bitmap[row * w + col];
            if (alpha == 0) continue;

            int px = (int)x + col + xoff;
            int py = (int)y + row + yoff;

            if (px < 0 || py < 0) continue;
            if (px >= (int)framebuffer_get_width())  continue;
            if (py >= (int)framebuffer_get_height()) continue;

            u32 blended = blend_alpha(fg, bg, alpha);
            framebuffer_put_pixel((u32)px, (u32)py, blended);
        }
    }

    stbtt_FreeBitmap(bitmap, 0);
    return (int)(advance * font_scale);
}