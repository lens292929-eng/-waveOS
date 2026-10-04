#ifndef MTRR_H
#define MTRR_H

#include "types.h"

/* Memory types */
#define MTRR_TYPE_UC 0
#define MTRR_TYPE_WC 1
#define MTRR_TYPE_WT 4
#define MTRR_TYPE_WP 5
#define MTRR_TYPE_WB 6

/* MSR addresses */
#define MSR_MTRRCAP         0x000000FE
#define MSR_MTRR_DEF_TYPE   0x000002FF
#define MSR_MTRR_PHYS_BASE  0x00000200
#define MSR_MTRR_PHYS_MASK  0x00000201

/* Set up a variable MTRR for the given physical range */
int mtrr_set_wc(u64 base, u64 size);

#endif