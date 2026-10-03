#include "wfs.h"
#include "ata.h"
#include "ramdisk.h"
#include "framebuffer.h"

/* MBR entry for the second partition. */
typedef struct {
    u8  status;
    u8  chs_first[3];
    u8  type;
    u8  chs_last[3];
    u32 lba_start;
    u32 sectors;
} __attribute__((packed)) mbr_entry_t;

typedef struct {
    u8  boot[446];
    mbr_entry_t entries[4];
    u16 signature;   /* 0xAA55 */
} __attribute__((packed)) mbr_t;

typedef struct {
    u32 magic;
    u32 version;
    u32 count;
    u8  reserved[500];
} __attribute__((packed)) wfs_header_t;

static u32 wfs_lba = 0;        /* start LBA of the wFs partition */
static u32 wfs_sectors = 0;    /* length in 512-byte sectors */

/* Fixed scratch buffers. Keep them off the stack. */
static u8  sector_buf[512];
static u8  payload_buf[WFS_MAX_BYTES];

void wfs_init(void)
{
    if (ata_read_sector(0, sector_buf) < 0) {
        print("[wfs] no disk\n");
        return;
    }

    mbr_t *mbr = (mbr_t *)sector_buf;

    if (mbr->signature != 0xAA55) {
        print("[wfs] no MBR\n");
        return;
    }

    /* Look for the second partition. */
    mbr_entry_t *p = &mbr->entries[1];

    if (p->lba_start == 0 || p->sectors == 0) {
        print("[wfs] no wFs partition\n");
        return;
    }

    wfs_lba     = p->lba_start;
    wfs_sectors = p->sectors;

    print("[wfs] partition at LBA ");
    print_u64(wfs_lba);
    print(", ");
    print_u64(wfs_sectors);
    print(" sectors\n");
}

void wfs_load(void)
{
    if (wfs_lba == 0) return;

    if (ata_read_sector(wfs_lba, sector_buf) < 0) return;

    wfs_header_t *h = (wfs_header_t *)sector_buf;

    if (h->magic != WFS_MAGIC) {
        print("[wfs] blank partition\n");
        return;
    }

    if (h->version != WFS_VERSION) {
        print("[wfs] wrong version\n");
        return;
    }

    /* Read the payload into memory. Cap at what we can hold. */
    u32 payload_bytes = wfs_sectors * 512 - 512;
    if (payload_bytes > WFS_MAX_BYTES)
        payload_bytes = WFS_MAX_BYTES;

    u32 sectors_to_read = (payload_bytes + 511) / 512;

    for (u32 s = 0; s < sectors_to_read; s++) {
        if (ata_read_sector(wfs_lba + 1 + s, payload_buf + s * 512) < 0)
            return;
    }

    /* Walk entries. */
    u32 off = 0;
    u32 restored = 0;

    while (off + 6 <= payload_bytes) {
        u8 used = payload_buf[off];
        if (!used) break;

        u8 name_len = payload_buf[off + 1];
        if (name_len == 0 || name_len > 31) break;

        char name[32];
        for (u8 i = 0; i < name_len; i++)
            name[i] = (char)payload_buf[off + 2 + i];
        name[name_len] = '\0';

        u32 data_len;
        u8 *dp = payload_buf + off + 2 + name_len;
        data_len = (u32)dp[0] | ((u32)dp[1] << 8) |
                   ((u32)dp[2] << 16) | ((u32)dp[3] << 24);

        u8 *data = dp + 4;

        if (off + 2 + name_len + 4 + data_len > payload_bytes)
            break;

        ramdisk_write(name, (const char *)data, (int)data_len);
        restored++;

        off += 2 + name_len + 4 + data_len;
    }

    print("[wfs] restored ");
    print_u64(restored);
    print(" file(s)\n");
}

int wfs_save(void)
{
    if (wfs_lba == 0) return -1;

    /* Build the header. */
    wfs_header_t h;
    h.magic   = WFS_MAGIC;
    h.version = WFS_VERSION;
    h.count   = 0;

    for (int i = 0; i < 500; i++) h.reserved[i] = 0;

    /* Pack every used entry into payload_buf. */
    u32 off = 0;

    ramdisk_save_begin();

    const char *name;
    const char *data;
    int len;

    while (ramdisk_save_next(&name, &data, &len)) {
        int nlen = 0;
        while (name[nlen]) nlen++;

        u32 need = 2 + nlen + 4 + (u32)len;
        if (off + need > WFS_MAX_BYTES) {
            print("[wfs] payload full\n");
            break;
        }

        payload_buf[off++] = 1;              /* used */
        payload_buf[off++] = (u8)nlen;
        for (int i = 0; i < nlen; i++)
            payload_buf[off++] = (u8)name[i];

        payload_buf[off++] = (u8)(len & 0xFF);
        payload_buf[off++] = (u8)((len >> 8) & 0xFF);
        payload_buf[off++] = (u8)((len >> 16) & 0xFF);
        payload_buf[off++] = (u8)((len >> 24) & 0xFF);

        for (int i = 0; i < len; i++)
            payload_buf[off++] = (u8)data[i];

        h.count++;
    }

    /* Pad the payload to a sector boundary. */
    while (off < WFS_MAX_BYTES && (off & 511) != 0)
        payload_buf[off++] = 0;

    /* Write header sector. */
    u8 *hb = (u8 *)&h;
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;
    for (int i = 0; i < (int)sizeof(h); i++) sector_buf[i] = hb[i];

    if (ata_write_sector(wfs_lba, sector_buf) < 0) return -1;

    /* Write payload sectors. */
    u32 sectors_to_write = (off + 511) / 512;
    for (u32 s = 0; s < sectors_to_write; s++) {
        if (ata_write_sector(wfs_lba + 1 + s, payload_buf + s * 512) < 0)
            return -1;
    }

    print("[wfs] saved ");
    print_u64(h.count);
    print(" file(s)\n");
    return 0;
}