#ifndef RAMDISK_H
#define RAMDISK_H

/* ramdisk.h */
#define RAMDISK_MAX_FILES  64
#define RAMDISK_NAME_MAX   128
#define RAMDISK_DATA_MAX   4096

int  ramdisk_write(const char *name, const char *data, int len);
int  ramdisk_read (const char *name, char *out, int max_len);
int  ramdisk_exists(const char *name);
int  ramdisk_delete(const char *name);
void ramdisk_list(void (*cb)(const char *name, int size));
/* ramdisk.h */
void ramdisk_save_begin(void);
int  ramdisk_save_next(const char **name, const char **data, int *len);

#endif