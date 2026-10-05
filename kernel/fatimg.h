#ifndef FATIMG_H
#define FATIMG_H

#include "types.h"

/* View a BMP file from the FAT32 filesystem.
 * Returns 0 on success, -1 on failure. */
int fatimg_view_bmp(const char *fat_path);
int fatimg_draw_bmp(const char *fat_path);

#endif