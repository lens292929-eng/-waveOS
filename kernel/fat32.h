#ifndef FAT32_H
#define FAT32_H

#include "types.h"

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#define FAT32_EOC            0x0FFFFFF8

typedef struct {
    u8  name[11];        /* 8.3 name, space-padded */
    u8  attr;
    u8  nt_reserved;
    u8  create_tenths;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 first_cluster_hi;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_lo;
    u32 size;
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct {
    u8  jump[3];
    u8  oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sector_count;
    u8  num_fats;
    u16 root_entry_count;      /* 0 on FAT32 */
    u16 total_sectors_16;
    u8  media;
    u16 fat_size_16;           /* 0 on FAT32 */
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;

    /* FAT32 extended */
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot_sector;
    u8  reserved[12];
    u8  drive_num;
    u8  reserved1;
    u8  boot_sig;
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

/* Initialize FAT32 for the given ATA drive. */
int  fat32_init(int drive);

/* Return true if initialized. */
int  fat32_ready(void);

/* List the root directory. */
void fat32_list_root(void);

/* Long filename buffer size (255 chars + null). */
#define FAT32_MAX_NAME 256

/* Read a file by path. Returns bytes read, or -1. */
int  fat32_read_file(const char *path, char *buf, int max);

/* List a directory by path. */
void fat32_list_dir(const char *path);

/* Check if a path is a directory. */
int  fat32_is_dir(const char *path);

/* Check if a path exists (file or dir). */
int  fat32_exists(const char *path);

int fat32_write_file(const char *path, const char *buf, int len);

#endif