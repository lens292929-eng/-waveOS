/*
 * stb_shims.c — freestanding C library shims for stb_truetype.
 *
 * stb_truetype expects a hosted environment with <math.h>,
 * <string.h>, <stdlib.h>, and <assert.h>. This file provides
 * minimal implementations of the functions it needs.
 *
 * Not all of these are actually exercised by the code paths
 * we call (stbtt_GetCodepointBitmap). Some — sqrt, fmod, acos,
 * cos, pow — are only needed to satisfy the linker, since
 * they appear in stbtt_GetGlyphSDF which we don't use.
 */

#include "types.h"
#include "math.h"
#include "string.h"
#include "stdlib.h"

/* ========================================================= */
/* Math                                                       */
/* ========================================================= */

double floor(double x)
{
    long i = (long)x;
    return (x < i) ? (double)(i - 1) : (double)i;
}

double ceil(double x)
{
    long i = (long)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

double fabs(double x)
{
    return x < 0 ? -x : x;
}

double sqrt(double x)
{
    if (x <= 0) return 0;

    /* Newton-Raphson. Good enough for glyph rasterization. */
    double r = x;
    for (int i = 0; i < 20; i++)
        r = 0.5 * (r + x / r);
    return r;
}

float sqrtf(float x)
{
    return (float)sqrt((double)x);
}

double fmod(double x, double y)
{
    if (y == 0) return 0;
    double q = (double)(long)(x / y);
    return x - q * y;
}

double acos(double x)
{
    (void)x;
    return 0;   /* stub — only used by stbtt_GetGlyphSDF */
}

double cos(double x)
{
    (void)x;
    return 1;   /* stub */
}

double sin(double x)
{
    (void)x;
    return 0;   /* stub */
}

double atan2(double y, double x)
{
    (void)y; (void)x;
    return 0;   /* stub */
}

double pow(double x, double y)
{
    (void)x; (void)y;
    return 0;   /* stub */
}

float powf(float x, float y)
{
    (void)x; (void)y;
    return 0;   /* stub */
}

/* ========================================================= */
/* String                                                     */
/* ========================================================= */

usize strlen(const char *s)
{
    usize n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, usize n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)(u8)*a - (int)(u8)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

char *strncpy(char *dst, const char *src, usize n)
{
    usize i = 0;
    while (i < n && src[i]) { dst[i] = src[i]; i++; }
    while (i < n) { dst[i] = 0; i++; }
    return dst;
}

/* ========================================================= */
/* Memory                                                     */
/* ========================================================= */

void *memcpy(void *dst, const void *src, usize n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, usize n)
{
    u8 *d = dst;
    const u8 *s = src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, usize n)
{
    u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

int memcmp(const void *a, const void *b, usize n)
{
    const u8 *p = a;
    const u8 *q = b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++;
        q++;
    }
    return 0;
}

/* ========================================================= */
/* Memory allocation                                          */
/* ========================================================= */

/*
 * Bump allocator. 4 MB static pool, no free.
 * stb_truetype only allocates a few KB per loaded font
 * and small glyph bitmaps. When you have a real heap,
 * replace this with kmalloc/kfree.
 */

#define SHIM_HEAP_SIZE (4 * 1024 * 1024)
static u8    shim_heap[SHIM_HEAP_SIZE];
static usize shim_heap_used = 0;

void *malloc(usize size)
{
    /* Align to 8 bytes */
    size = (size + 7) & ~(usize)7;

    if (shim_heap_used + size > SHIM_HEAP_SIZE)
        return 0;

    void *p = &shim_heap[shim_heap_used];
    shim_heap_used += size;
    return p;
}

void free(void *ptr)
{
    (void)ptr;   /* bump allocator: no-op */
}

void *realloc(void *ptr, usize size)
{
    /* We can't grow in place, so just allocate fresh.
     * Caller is responsible for copying. */
    (void)ptr;
    return malloc(size);
}