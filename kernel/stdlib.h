#ifndef WAVE_STDLIB_H
#define WAVE_STDLIB_H

#include "types.h"

void *malloc(usize size);
void  free(void *ptr);
void *realloc(void *ptr, usize size);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif