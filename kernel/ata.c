#include "ata.h"
#include "io.h"
#include "framebuffer.h"

/* Primary ATA bus I/O ports. */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_FEATURES    0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7
#define ATA_CONTROL     0x3F6

/* Status register bits. */
#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DF       0x20
#define ATA_SR_DSC      0x10
#define ATA_SR_DRQ      0x08
#define ATA_SR_CORR     0x04
#define ATA_SR_IDX      0x02
#define ATA_SR_ERR      0x01

/* Commands. */
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_IDENT   0xEC

static int ata_present = 0;

/* Wait 400ns by reading the status port four times. */
static void ata_delay400(void)
{
    for (int i = 0; i < 4; i++)
        (void)inb(ATA_STATUS);
}

/* Wait for BSY to clear. Returns 0 on success, -1 on timeout. */
static int ata_wait_not_busy(void)
{
    for (u32 i = 0; i < 1000000; i++) {
        u8 status = inb(ATA_STATUS);
        if (!(status & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

/* Wait for DRQ to set. Returns 0 on success, -1 on error. */
static int ata_wait_drq(void)
{
    for (u32 i = 0; i < 1000000; i++) {
        u8 status = inb(ATA_STATUS);

        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
            return 0;
    }
    return -1;
}

void ata_init(void)
{
    /* Select master drive, LBA mode. */
    outb(ATA_DRIVE, 0xA0);
    ata_delay400();

    /* Zero out the sector-count and LBA registers. */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW,  0);
    outb(ATA_LBA_MID,  0);
    outb(ATA_LBA_HIGH, 0);

    /* Send IDENTIFY. */
    outb(ATA_COMMAND, ATA_CMD_IDENT);

    ata_delay400();

    /* If status is 0, no drive. */
    u8 status = inb(ATA_STATUS);
    if (status == 0) {
        print("[ata] no drive\n");
        ata_present = 0;
        return;
    }

    /* Wait for BSY to clear. */
    if (ata_wait_not_busy() < 0) {
        print("[ata] timeout\n");
        ata_present = 0;
        return;
    }

    /* If LBA_MID or LBA_HIGH are non-zero, it's not an ATA drive. */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) {
        print("[ata] not ATA\n");
        ata_present = 0;
        return;
    }

    /* Wait for DRQ or ERR. */
    for (u32 i = 0; i < 1000000; i++) {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            print("[ata] identify failed\n");
            ata_present = 0;
            return;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
            break;
    }

    /* Drain the 256-word IDENTIFY response. */
    for (int i = 0; i < 256; i++)
        (void)inw(ATA_DATA);

    ata_present = 1;
    print("[ata] primary master detected\n");
}

int ata_read_sector(u32 lba, u8 *buf)
{
    if (!ata_present) return -1;

    if (ata_wait_not_busy() < 0) return -1;

    /* Select master, LBA mode, top 4 bits of LBA in the low nibble. */
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay400();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  (u8)(lba & 0xFF));
    outb(ATA_LBA_MID,  (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (u8)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_delay400();

    if (ata_wait_drq() < 0) return -1;

    /* Read 256 words = 512 bytes. */
    u16 *p = (u16 *)buf;
    for (int i = 0; i < 256; i++)
        p[i] = inw(ATA_DATA);

    ata_delay400();

    return 0;
}

int ata_write_sector(u32 lba, const u8 *buf)
{
    if (!ata_present) return -1;

    if (ata_wait_not_busy() < 0) return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay400();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW,  (u8)(lba & 0xFF));
    outb(ATA_LBA_MID,  (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (u8)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_delay400();

    if (ata_wait_drq() < 0) return -1;

    const u16 *p = (const u16 *)buf;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, p[i]);

    /* Flush the write cache. */
    outb(ATA_COMMAND, 0xE7);
    ata_delay400();

    if (ata_wait_not_busy() < 0) return -1;

    return 0;
}