#ifndef WFS_H
#define WFS_H

#include "types.h"

#define WFS_MAGIC       0x31534657u   /* "WFS1" little-endian */
#define WFS_VERSION     1

/* Max size of the wFs payload we handle in memory.
 * 1 MB is plenty for 64 files × 4 KB + names. */
#define WFS_MAX_BYTES   (1024 * 1024)

void wfs_init(void);
void wfs_load(void);
int  wfs_save(void);

#endif