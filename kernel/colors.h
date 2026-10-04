#ifndef ZENITH_COLORS_H
#define ZENITH_COLORS_H

#include "types.h"

// ============================================================
// Zenith color palette
//
// Format assumed: 0x00RRGGBB  (u32, top byte ignored)
//
// On OVMF/QEMU the GOP is usually PixelBlueGreenRedReserved8BitPerColor,
// which means the u32 in memory is actually 0x00BBGGRR when read as a
// little-endian word — i.e. R and B are swapped relative to this header.
//
// If red shows up as blue on your machine, swap the R and B nibbles in
// each constant below, or redefine them as 0x00BBGGRR.
// ============================================================

#define COLOR_BLACK         0x00000000
#define COLOR_BLUE          0x000000AA
#define COLOR_GREEN         0x0000AA00
#define COLOR_CYAN          0x0000AAAA
#define COLOR_RED           0x00AA0000
#define COLOR_MAGENTA       0x00AA00AA
#define COLOR_BROWN         0x00AA5500
#define COLOR_LIGHT_GRAY    0x00AAAAAA

#define COLOR_DARK_GRAY     0x00555555
#define COLOR_LIGHT_BLUE    0x005555FF
#define COLOR_LIGHT_GREEN   0x0055FF55
#define COLOR_LIGHT_CYAN    0x0055FFFF
#define COLOR_LIGHT_RED     0x00FF5555
#define COLOR_LIGHT_MAGENTA 0x00FF55FF
#define COLOR_YELLOW        0x00FFFF55
#define COLOR_WHITE         0x00FFFFFF
#define COLOR_WAVE_BG       0x00101820   

// Convenience aliases
#define COLOR_GREY          COLOR_LIGHT_GRAY
#define COLOR_GRAY          COLOR_LIGHT_GRAY

// Zenith log levels
#define COLOR_LOG_INFO      COLOR_LIGHT_GRAY
#define COLOR_LOG_OK        COLOR_LIGHT_GREEN
#define COLOR_LOG_WARN      COLOR_YELLOW
#define COLOR_LOG_ERROR     COLOR_LIGHT_RED
#define COLOR_LOG_DEBUG     COLOR_DARK_GRAY

// Common UI
#define COLOR_BG            COLOR_BLACK
#define COLOR_FG            COLOR_WHITE
#define COLOR_ACCENT        COLOR_LIGHT_CYAN

/* colors.h — Prism theme */
#define PRISM_BG        0x001E1E22
#define PRISM_TEXT      0x00FFFFFF
#define PRISM_BLACK     0x00000000
#define PRISM_SHADOW    0x00000000
#define PRISM_ACCENT    0x00528BFF
#define PRISM_HIGHLIGHT 0x00264F78

#endif