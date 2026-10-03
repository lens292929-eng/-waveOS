#include "ramdisk.h"

typedef struct {
    char name[RAMDISK_NAME_MAX];
    char data[RAMDISK_DATA_MAX];
    int  len;
    int  used;
} ramfile_t;

static ramfile_t files[RAMDISK_MAX_FILES];

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int find(const char *name)
{
    for (int i = 0; i < RAMDISK_MAX_FILES; i++)
        if (files[i].used && str_eq(files[i].name, name))
            return i;
    return -1;
}

int ramdisk_write(const char *name, const char *data, int len)
{
    if (len > RAMDISK_DATA_MAX) return -1;

    int i = find(name);
    if (i < 0) {
        for (i = 0; i < RAMDISK_MAX_FILES; i++)
            if (!files[i].used) break;
        if (i == RAMDISK_MAX_FILES) return -1;
        files[i].used = 1;
        str_cpy(files[i].name, name, RAMDISK_NAME_MAX);
    }

    for (int j = 0; j < len; j++) files[i].data[j] = data[j];
    files[i].len = len;
    return len;
}

int ramdisk_read(const char *name, char *out, int max_len)
{
    int i = find(name);
    if (i < 0) return -1;
    int n = files[i].len < max_len ? files[i].len : max_len;
    for (int j = 0; j < n; j++) out[j] = files[i].data[j];
    return n;
}

int ramdisk_exists(const char *name) { return find(name) >= 0; }

int ramdisk_delete(const char *name)
{
    int i = find(name);
    if (i < 0) return -1;
    files[i].used = 0;
    return 0;
}

void ramdisk_list(void (*cb)(const char *name, int size))
{
    for (int i = 0; i < RAMDISK_MAX_FILES; i++)
        if (files[i].used)
            cb(files[i].name, files[i].len);
}

/* ramdisk.c */
static int save_iter = 0;

void ramdisk_save_begin(void)
{
    save_iter = 0;
}

int ramdisk_save_next(const char **name, const char **data, int *len)
{
    while (save_iter < RAMDISK_MAX_FILES) {
        int i = save_iter++;
        if (files[i].used) {
            *name = files[i].name;
            *data = files[i].data;
            *len  = files[i].len;
            return 1;
        }
    }
    return 0;
}