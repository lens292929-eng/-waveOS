/* kernel/assert.h */
#ifndef WAVE_ASSERT_H
#define WAVE_ASSERT_H

#include "types.h"

/* stb_truetype uses assert for internal invariant checks.
 * We make it a no-op so failed assertions don't hang the kernel. */
#define assert(x) ((void)0)

#endif