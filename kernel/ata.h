#ifndef ATA_H
#define ATA_H

#include "types.h"

/* Initialize the primary ATA bus. Prints status. */
void ata_init(void);

/* Read one 512-byte sector into buf. Returns 0 on success, -1 on error. */
int ata_read_sector(u32 lba, u8 *buf);

/* Write one 512-byte sector from buf. Returns 0 on success, -1 on error. */
int ata_write_sector(u32 lba, const u8 *buf);

#endif