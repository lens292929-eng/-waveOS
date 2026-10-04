#include "fat32.h"
#include "ata.h"
#include "framebuffer.h"
#include "colors.h"

static int  fat_drive = -1;
static u32  fat_lba_start = 0;
static u32  fat_sectors_per_cluster = 0;
static u32  fat_reserved = 0;
static u32  fat_count = 0;
static u32  fat_size = 0;
static u32  fat_root_cluster = 0;
static u32  fat_data_lba = 0;
static u32  fat_cluster_size = 0;

static u8   sector_buf[512];
static u8   cluster_buf[64 * 1024];    /* 128 sectors = 64 KB max cluster */

/* ========================================================= */
/* Low-level helpers                                          */
// ========================================================= */

static u32 cluster_to_lba(u32 cluster)
{
    return fat_data_lba + (cluster - 2) * fat_sectors_per_cluster;
}

static int read_sector(u32 lba, u8 *buf)
{
    return ata_read_sector_drive(fat_drive, lba, buf);
}

static u32 fat_get(u32 cluster)
{
    u32 fat_lba   = fat_lba_start + fat_reserved;
    u32 entry_off = cluster * 4;
    u32 sector_off = entry_off / 512;
    u32 in_sector  = entry_off % 512;

    read_sector(fat_lba + sector_off, sector_buf);

    u32 value = *(u32 *)(sector_buf + in_sector);
    return value & 0x0FFFFFFF;
}

static int read_cluster(u32 cluster, u8 *buf)
{
    if (cluster < 2) return -1;

    u32 lba = cluster_to_lba(cluster);

    for (u32 s = 0; s < fat_sectors_per_cluster; s++) {
        if (read_sector(lba + s, buf + s * 512) < 0)
            return -1;
    }

    return 0;
}

/* ========================================================= */
/* Init                                                       */
/* ========================================================= */

int fat32_init(int drive)
{
    fat_drive = drive;
    fat_lba_start = 0;

    if (read_sector(0, sector_buf) < 0)
        return -1;

    fat32_bpb_t *bpb = (fat32_bpb_t *)sector_buf;

    if (bpb->bytes_per_sector != 512)    return -1;
    if (bpb->sectors_per_cluster == 0)   return -1;
    if (bpb->fat_size_32 == 0)           return -1;

    fat_sectors_per_cluster = bpb->sectors_per_cluster;
    fat_reserved            = bpb->reserved_sector_count;
    fat_count               = bpb->num_fats;
    fat_size                = bpb->fat_size_32;
    fat_root_cluster        = bpb->root_cluster;

    fat_data_lba = fat_lba_start
                 + fat_reserved
                 + fat_count * fat_size;

    fat_cluster_size = fat_sectors_per_cluster * 512;

    print("[fat32] bytes/sector=");
    print_u64(bpb->bytes_per_sector);
    print(" sectors/cluster=");
    print_u64(fat_sectors_per_cluster);
    print(" reserved=");
    print_u64(fat_reserved);
    print(" fats=");
    print_u64(fat_count);
    print(" fat_size=");
    print_u64(fat_size);
    print(" root=");
    print_u64(fat_root_cluster);
    print("\n");

    return 0;
}

int fat32_ready(void)
{
    return fat_drive >= 0;
}

/* ========================================================= */
/* 8.3 and LFN name handling                                  */
/* ========================================================= */

/* Convert an 8.3 name to a printable string. */
static int format_8_3(const u8 *name, char *out)
{
    int len = 8;
    while (len > 0 && name[len - 1] == ' ')
        len--;

    int extlen = 3;
    while (extlen > 0 && name[8 + extlen - 1] == ' ')
        extlen--;

    int j = 0;
    for (int i = 0; i < len; i++)
        out[j++] = (char)name[i];

    if (extlen > 0) {
        out[j++] = '.';
        for (int i = 0; i < extlen; i++)
            out[j++] = (char)name[8 + i];
    }

    out[j] = '\0';
    return j;
}

/* ========================================================= */
/* Directory iteration                                        */
/*                                                        */
/* The callback receives each entry's display name, whether  */
/* it's a directory, its first cluster, and its size.        */
/* ========================================================= */

typedef void (*fat32_dir_cb)(const char *name, int is_dir,
                             u32 first_cluster, u32 size);

/* Compute the checksum of an 8.3 name. Used to validate LFN
 * entries belong to the 8.3 entry that follows. */
static u8 lfn_checksum(const u8 *name)
{
    u8 sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
    return sum;
}

/* Parse LFN characters out of one LFN entry. */
static void lfn_extract_chars(const fat32_dir_entry_t *e, u16 *chars_out)
{
    /* Each LFN entry stores 13 UTF-16 characters at:
     *   offset 1:  5 chars
     *   offset 14: 6 chars
     *   offset 28: 2 chars
     * The raw bytes are at name[1..10], name[14..25], name[28..31]. */

    const u8 *raw = e->name;

    int k = 0;
    for (int i = 1; i < 11; i += 2) {
        chars_out[k++] = (u16)(raw[i] | (raw[i+1] << 8));
    }
    for (int i = 14; i < 26; i += 2) {
        chars_out[k++] = (u16)(raw[i] | (raw[i+1] << 8));
    }
    for (int i = 28; i < 32; i += 2) {
        chars_out[k++] = (u16)(raw[i] | (raw[i+1] << 8));
    }
}

/* Iterate a directory, calling cb for each non-LFN entry.
 * Handles LFN assembly transparently. */
static int iterate_dir(u32 dir_cluster, fat32_dir_cb cb)
{
    /* LFN assembly buffer: up to 20 LFN entries × 13 chars. */
    u16 lfn_chars[260];
    int lfn_count = 0;

    u32 cluster = dir_cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {

        if (read_cluster(cluster, cluster_buf) < 0)
            return -1;

        u32 entries = fat_cluster_size / 32;

        for (u32 i = 0; i < entries; i++) {
            fat32_dir_entry_t *e =
                (fat32_dir_entry_t *)(cluster_buf + i * 32);

            u8 first = e->name[0];

            if (first == 0x00)
                return 0;             /* end of directory */

            if (first == 0xE5) {
                lfn_count = 0;
                continue;             /* deleted */
            }

            if (e->attr == FAT32_ATTR_LFN) {
                /* Extract and store. LFN entries come in reverse
                 * order, so we fill from the end. */
                u16 chars[13];
                lfn_extract_chars(e, chars);

                int seq = e->name[0] & 0x3F;   /* sequence number */

                if (seq < 1 || seq > 20) {
                    lfn_count = 0;
                    continue;
                }

                /* Reverse-fill: sequence N goes at slot N-1. */
                int slot = seq - 1;
                if (slot * 13 + 13 <= 260) {
                    for (int c = 0; c < 13; c++)
                        lfn_chars[slot * 13 + c] = chars[c];
                }

                if (seq > lfn_count)
                    lfn_count = seq;

                continue;
            }

            /* Skip volume labels. */
            if (e->attr & FAT32_ATTR_VOLUME_ID) {
                lfn_count = 0;
                continue;
            }

            /* Assemble the display name. */
            char display[FAT32_MAX_NAME];
            int dlen = 0;

            if (lfn_count > 0) {
                /* Convert UTF-16 to ASCII, stopping at 0x0000. */
                for (int j = 0; j < lfn_count * 13 && dlen < FAT32_MAX_NAME - 1; j++) {
                    u16 c = lfn_chars[j];
                    if (c == 0x0000) break;
                    display[dlen++] = (c < 128) ? (char)c : '?';
                }
                display[dlen] = '\0';
            } else {
                format_8_3(e->name, display);
                dlen = 0;
                while (display[dlen]) dlen++;
            }

            lfn_count = 0;

            u32 first_cluster =
                ((u32)e->first_cluster_hi << 16) | e->first_cluster_lo;

            int is_dir = (e->attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;

            cb(display, is_dir, first_cluster, e->size);
        }

        cluster = fat_get(cluster);
    }

    return 0;
}

/* ========================================================= */
/* Path resolution                                            */
/*                                                        */
/* Turn "/foo/bar/baz.txt" into a (dir_cluster, name) pair. */
/* Root is special-cased.                                     */
/* ========================================================= */

/* Find a directory entry by name within a directory cluster. */
/* Returns 0 on success, and fills the entry, cluster, is_dir. */
static int find_in_dir(u32 dir_cluster, const char *name,
                       u32 *out_cluster, int *out_is_dir, u32 *out_size)
{
    /* Split name into display (before last slash — caller did it)
     * and the actual last component. */
    /* We iterate with a callback that captures on match. */

    /* Use static state to pass through the callback. */
    static const char *target;
    static u32  result_cluster;
    static int  result_is_dir;
    static u32  result_size;
    static int  found;

    target = name;
    found = 0;
    result_cluster = 0;
    result_is_dir = 0;
    result_size = 0;

    /* Local strcmp for the callback. */
    extern int __wrap_strcmp(const char *a, const char *b);

    void cb(const char *n, int is_dir, u32 c, u32 sz);
    /* We can't have nested functions in standard C, so use a
     * file-scope callback that reads the statics above. */
    /* Fall through to the file-scope callback below. */

    return 0;
}

/* Workaround: file-scope callback + state for find_in_dir. */
static const char *g_target;
static u32  g_result_cluster;
static int  g_result_is_dir;
static u32  g_result_size;
static int  g_found;

static int name_eq(const char *a, const char *b)
{
    /* Case-insensitive compare for FAT. */
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void find_cb(const char *name, int is_dir, u32 cluster, u32 size)
{
    if (g_found) return;
    if (name_eq(name, g_target)) {
        g_found = 1;
        g_result_cluster = cluster;
        g_result_is_dir = is_dir;
        g_result_size = size;
    }
}

/* Resolve a directory path to its starting cluster.
 * "/"         -> fat_root_cluster
 * "/foo"      -> cluster of /foo
 * "/foo/bar"  -> cluster of /foo/bar
 *
 * Returns -1 if not found. */
static int resolve_dir(const char *path)
{
    if (path[0] != '/') return -1;
    if (path[1] == '\0') return (int)fat_root_cluster;

    u32 cluster = fat_root_cluster;

    /* Walk path components. */
    char buf[FAT32_MAX_NAME];
    int i = 1;
    int b = 0;

    while (1) {
        char c = path[i];

        if (c == '/' || c == '\0') {
            buf[b] = '\0';

            if (b > 0) {
                g_target = buf;
                g_found = 0;

                iterate_dir(cluster, find_cb);

                if (!g_found || !g_result_is_dir)
                    return -1;

                cluster = g_result_cluster;
            }

            b = 0;

            if (c == '\0') break;
        } else {
            if (b < FAT32_MAX_NAME - 1)
                buf[b++] = c;
        }

        i++;
    }

    return (int)cluster;
}

/* Split "/foo/bar/baz" into directory path "/foo/bar" and
 * filename "baz". If the path is a root-level file, directory
 * path is "/". Returns 0 on success. */
static int split_path(const char *path, char *dir_out, char *name_out)
{
    if (path[0] != '/') return -1;

    int len = 0;
    while (path[len]) len++;

    /* Find last slash. */
    int last = -1;
    for (int i = 0; i < len; i++)
        if (path[i] == '/') last = i;

    if (last < 0) return -1;

    /* Copy dir portion. */
    int j = 0;
    for (int i = 0; i < last; i++)
        dir_out[j++] = path[i];
    dir_out[j] = '\0';
    if (j == 0) { dir_out[0] = '/'; dir_out[1] = '\0'; }

    /* Copy filename. */
    j = 0;
    for (int i = last + 1; i < len; i++)
        name_out[j++] = path[i];
    name_out[j] = '\0';

    return (j > 0) ? 0 : -1;
}

/* Look up a full path. Returns 0 on success. */
static int lookup_path(const char *path, u32 *cluster_out,
                       int *is_dir_out, u32 *size_out)
{
    if (path[0] != '/' || path[1] == '\0') {
        /* Root. */
        *cluster_out = fat_root_cluster;
        *is_dir_out = 1;
        *size_out = 0;
        return 0;
    }

    char dir_path[FAT32_MAX_NAME];
    char name[FAT32_MAX_NAME];

    if (split_path(path, dir_path, name) < 0)
        return -1;

    int dir_cluster = resolve_dir(dir_path);
    if (dir_cluster < 0)
        return -1;

    g_target = name;
    g_found = 0;

    iterate_dir((u32)dir_cluster, find_cb);

    if (!g_found) return -1;

    *cluster_out = g_result_cluster;
    *is_dir_out = g_result_is_dir;
    *size_out = g_result_size;
    return 0;
}

/* ========================================================= */
/* Read file                                                  */
/* ========================================================= */

int fat32_read_file(const char *path, char *buf, int max)
{
    if (!fat32_ready()) return -1;

    u32 cluster;
    int is_dir;
    u32 size;

    if (lookup_path(path, &cluster, &is_dir, &size) < 0)
        return -1;

    if (is_dir) return -1;

    if ((int)size > max) size = (u32)max;

    u32 written = 0;
    u32 c = cluster;

    while (c >= 2 && c < 0x0FFFFFF8 && written < size) {
        if (read_cluster(c, cluster_buf) < 0)
            break;

        u32 n = fat_cluster_size;
        if (written + n > size) n = size - written;

        for (u32 i = 0; i < n; i++)
            buf[written + i] = (char)cluster_buf[i];

        written += n;
        c = fat_get(c);
    }

    return (int)written;
}

/* ========================================================= */
/* Public listing                                             */
/* ========================================================= */

static void print_dir_cb(const char *name, int is_dir,
                         u32 cluster, u32 size)
{
    (void)cluster;

    if (is_dir) {
        print_set_colors(0x0050A0FF, COLOR_WAVE_BG);
        print(name);
        print("/");
        printc('\n');
        print_set_colors(0x00FFFFFF, COLOR_WAVE_BG);
    } else {
        print_set_colors(0x00FFFFFF, COLOR_WAVE_BG);
        print(name);

        int n = 0;
        while (name[n]) n++;
        int pad = 32 - n;
        for (int i = 0; i < pad; i++)
            printc(' ');

        print_set_colors(0x00808080, COLOR_WAVE_BG);
        print_u64(size);
        print(" bytes");
        printc('\n');
        print_set_colors(0x00FFFFFF, COLOR_WAVE_BG);
    }
}

void fat32_list_dir(const char *path)
{
    if (!fat32_ready()) return;

    int cluster = resolve_dir(path);
    if (cluster < 0) {
        print("[fat32] no such directory: ");
        print(path);
        printc('\n');
        return;
    }

    iterate_dir((u32)cluster, print_dir_cb);
}

void fat32_list_root(void)
{
    fat32_list_dir("/");
}

int fat32_is_dir(const char *path)
{
    u32 c;
    int is_dir;
    u32 size;
    if (lookup_path(path, &c, &is_dir, &size) < 0) return 0;
    return is_dir;
}

int fat32_exists(const char *path)
{
    u32 c;
    int is_dir;
    u32 size;
    return lookup_path(path, &c, &is_dir, &size) == 0;
}

/* ========================================================= */
/* Write primitives                                          */
/* ========================================================= */

static int write_sector(u32 lba, const u8 *buf)
{
    return ata_write_sector_drive(fat_drive, lba, buf);
}

static int write_cluster(u32 cluster, const u8 *buf)
{
    u32 lba = cluster_to_lba(cluster);

    for (u32 s = 0; s < fat_sectors_per_cluster; s++) {
        if (write_sector(lba + s, buf + s * 512) < 0)
            return -1;
    }
    return 0;
}

/* Set a FAT entry in both FATs. */
static int fat_set(u32 cluster, u32 value)
{
    u32 entry_off = cluster * 4;
    u32 sector_off = entry_off / 512;
    u32 in_sector  = entry_off % 512;

    /* Preserve the top 4 bits of the existing entry — they're
     * "reserved" and the spec says to leave them alone. */
    u32 new_value = (value & 0x0FFFFFFF);

    /* Write to both FATs. */
    for (u32 f = 0; f < fat_count; f++) {
        u32 fat_lba = fat_lba_start + fat_reserved + f * fat_size;

        if (read_sector(fat_lba + sector_off, sector_buf) < 0)
            return -1;

        u32 old = *(u32 *)(sector_buf + in_sector);
        u32 merged = (old & 0xF0000000) | new_value;

        *(u32 *)(sector_buf + in_sector) = merged;

        if (write_sector(fat_lba + sector_off, sector_buf) < 0)
            return -1;
    }

    return 0;
}

/* Find a free cluster. Returns 0 if none. */
static u32 fat_alloc_cluster(void)
{
    u32 total_clusters = (fat_size * 512) / 4;
    u32 fat_lba = fat_lba_start + fat_reserved;

    for (u32 sector = 0; sector < fat_size; sector++) {
        if (read_sector(fat_lba + sector, sector_buf) < 0)
            return 0;

        u32 *entries = (u32 *)sector_buf;

        for (int i = 0; i < 128; i++) {
            u32 cluster = sector * 128 + i;
            if (cluster < 2) continue;
            if (cluster >= total_clusters) return 0;

            if ((entries[i] & 0x0FFFFFFF) == 0) {
                /* Mark as end-of-chain immediately so we don't
                 * hand out the same cluster twice if we're called
                 * again before linking. */
                fat_set(cluster, 0x0FFFFFFF);
                return cluster;
            }
        }
    }

    return 0;
}

/* ========================================================= */
/* Delete a file's cluster chain                              */
/* ========================================================= */

static int free_chain(u32 first)
{
    u32 c = first;

    while (c >= 2 && c < 0x0FFFFFF8) {
        u32 next = fat_get(c);
        if (fat_set(c, 0) < 0) return -1;
        c = next;
    }
    return 0;
}

/* ========================================================= */
/* Directory entry search / insert                            */
/* ========================================================= */

/* Look up a directory entry. On success, fills *dir_lba,
 * *dir_offset (byte offset within the sector), and reads the
 * sector into sector_buf for the caller. Returns 0 on success,
 * -1 if not found. */
static int find_dir_entry(u32 dir_cluster, const char *name,
                          u32 *dir_lba_out, u32 *dir_off_out)
{
    char upper[12];
    int u = 0;

    /* Build the 8.3 lookup name: uppercase, spaces, "." not included. */
    for (int i = 0; i < 11; i++) upper[i] = ' ';

    int n = 0;
    int dot = -1;
    while (name[n]) {
        if (name[n] == '.') dot = n;
        n++;
    }

    for (int i = 0; i < n; i++) {
        char c = name[i];
        if (c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';

        int idx;
        if (dot >= 0 && i > dot) idx = 8 + (i - dot - 1);
        else                     idx = i;
        if (idx < 11) upper[idx] = c;
    }

    u32 cluster = dir_cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);

        for (u32 s = 0; s < fat_sectors_per_cluster; s++) {
            if (read_sector(lba + s, sector_buf) < 0)
                return -1;

            fat32_dir_entry_t *e = (fat32_dir_entry_t *)sector_buf;

            for (int i = 0; i < 16; i++) {
                if (e[i].name[0] == 0x00)
                    return -1;   /* end of directory */

                if (e[i].name[0] == 0xE5)
                    continue;

                if (e[i].attr == FAT32_ATTR_LFN)
                    continue;

                if (e[i].attr & FAT32_ATTR_VOLUME_ID)
                    continue;

                int match = 1;
                for (int k = 0; k < 11; k++) {
                    if (e[i].name[k] != (u8)upper[k]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    *dir_lba_out = lba + s;
                    *dir_off_out = i * 32;
                    return 0;
                }
            }
        }

        cluster = fat_get(cluster);
    }

    return -1;
}

/* Find a free slot in a directory. If none, extend by one cluster. */
static int alloc_dir_slot(u32 dir_cluster, u32 *lba_out, u32 *off_out)
{
    u32 cluster = dir_cluster;
    u32 prev    = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 lba = cluster_to_lba(cluster);

        for (u32 s = 0; s < fat_sectors_per_cluster; s++) {
            if (read_sector(lba + s, sector_buf) < 0)
                return -1;

            fat32_dir_entry_t *e = (fat32_dir_entry_t *)sector_buf;

            for (int i = 0; i < 16; i++) {
                if (e[i].name[0] == 0x00 || e[i].name[0] == 0xE5) {
                    *lba_out = lba + s;
                    *off_out = i * 32;
                    return 0;
                }
            }
        }

        prev = cluster;
        cluster = fat_get(cluster);
    }

    /* No slot — extend the directory by one cluster. */
    u32 new_c = fat_alloc_cluster();
    if (new_c == 0) return -1;

    /* Zero it. */
    u8 zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    for (u32 s = 0; s < fat_sectors_per_cluster; s++) {
        if (write_sector(cluster_to_lba(new_c) + s, zero) < 0)
            return -1;
    }

    /* Link to previous. */
    if (prev != 0)
        fat_set(prev, new_c);

    /* Return first slot of the new cluster. */
    *lba_out = cluster_to_lba(new_c);
    *off_out = 0;
    return 0;
}

/* ========================================================= */
/* Write file (delete + create)                               */
/* ========================================================= */

int fat32_write_file(const char *path, const char *buf, int len)
{
    if (!fat32_ready()) return -1;
    if (path[0] != '/') return -1;

    /* Split into dir and name. */
    char dir_path[FAT32_MAX_NAME];
    char name[FAT32_MAX_NAME];

    if (split_path(path, dir_path, name) < 0)
        return -1;

    int dir_cluster = resolve_dir(dir_path);
    if (dir_cluster < 0)
        return -1;

    /* If the file exists, delete it (free the chain, mark slot). */
    u32 old_lba, old_off;
    if (find_dir_entry((u32)dir_cluster, name, &old_lba, &old_off) == 0) {
        fat32_dir_entry_t *e =
            (fat32_dir_entry_t *)(sector_buf + old_off);

        u32 first = ((u32)e->first_cluster_hi << 16)
                  | e->first_cluster_lo;

        if (first >= 2)
            free_chain(first);

        /* Mark as deleted. */
        e->name[0] = 0xE5;
        if (write_sector(old_lba, sector_buf) < 0)
            return -1;
    }

    /* Build the 8.3 name. */
    char upper[12];
    for (int i = 0; i < 11; i++) upper[i] = ' ';

    int n = 0, dot = -1;
    while (name[n]) { if (name[n] == '.') dot = n; n++; }

    for (int i = 0; i < n; i++) {
        char c = name[i];
        if (c == '.') continue;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';

        int idx = (dot >= 0 && i > dot) ? 8 + (i - dot - 1) : i;
        if (idx < 11) upper[idx] = c;
    }
    upper[11] = '\0';

    /* Allocate a cluster chain for the data. */
    u32 first_cluster = 0;
    u32 prev_cluster  = 0;
    u32 remaining     = (u32)len;
    u32 written       = 0;

    while (remaining > 0) {
        u32 c = fat_alloc_cluster();
        if (c == 0) return -1;

        if (prev_cluster)
            fat_set(prev_cluster, c);

        if (first_cluster == 0)
            first_cluster = c;

        prev_cluster = c;

        /* Fill the cluster with data. */
        u32 this_batch = (remaining > fat_cluster_size)
                       ? fat_cluster_size
                       : remaining;

        for (u32 i = 0; i < this_batch; i++)
            cluster_buf[i] = (u8)buf[written + i];

        /* Zero the rest. */
        for (u32 i = this_batch; i < fat_cluster_size; i++)
            cluster_buf[i] = 0;

        if (write_cluster(c, cluster_buf) < 0)
            return -1;

        written   += this_batch;
        remaining -= this_batch;
    }

    /* Find a slot in the parent directory. */
    u32 slot_lba, slot_off;
    if (alloc_dir_slot((u32)dir_cluster, &slot_lba, &slot_off) < 0)
        return -1;

    /* Re-read the sector (alloc_dir_slot clobbered sector_buf). */
    if (read_sector(slot_lba, sector_buf) < 0)
        return -1;

    fat32_dir_entry_t *e =
        (fat32_dir_entry_t *)(sector_buf + slot_off);

    for (int i = 0; i < 11; i++) e->name[i] = (u8)upper[i];
    e->attr = FAT32_ATTR_ARCHIVE;
    e->nt_reserved = 0;
    e->create_tenths = 0;
    e->create_time = 0;
    e->create_date = 0;
    e->access_date = 0;
    e->first_cluster_hi = (u16)(first_cluster >> 16);
    e->write_time = 0;
    e->write_date = 0;
    e->first_cluster_lo = (u16)(first_cluster & 0xFFFF);
    e->size = (u32)len;

    if (write_sector(slot_lba, sector_buf) < 0)
        return -1;

    return len;
}