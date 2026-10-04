#include "mtrr.h"
#include "io.h" // We'll need a helper for rdmsr/wrmsr

/* Helper to read a Model Specific Register */
static inline u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

/* Helper to write a Model Specific Register */
static inline void wrmsr(u32 msr, u64 value) {
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

int mtrr_set_wc(u64 base, u64 size) {
    u64 mtrr_cap = rdmsr(MSR_MTRRCAP);
    u32 vcnt = (u32)(mtrr_cap & 0xFF); // Number of variable MTRRs available

    if (vcnt == 0) {
        return -1; // Not supported
    }

    // Find the size in pages and the mask
    // MTRR size must be a power of 2.
    // Find the largest power of 2 that is <= size.
    u64 actual_size = 1;
    while (actual_size < size) {
        actual_size <<= 1;
    }
    // The mask is the physical address mask for the range
    // It's the inverse of (size - 1), aligned to page size (4K).
    u64 mask = ~(actual_size - 1) & 0xFFFFFFFFFFFFF000ULL;

    // Find a free MTRR register (check if the mask MSR is valid)
    // A mask is valid if bit 11 (MTRR_VALID) is set.
    // Actually, the MTRR_PHYS_MASK has a valid bit at bit 11.
    // Let's just look for the first one where the valid bit is 0.
    int reg = -1;
    for (int i = 0; i < (int)vcnt; i++) {
        u64 mask_msr = rdmsr(MSR_MTRR_PHYS_MASK + (i * 2));
        if ((mask_msr & (1 << 11)) == 0) {
            reg = i;
            break;
        }
    }

    if (reg == -1) {
        return -2; // No free MTRR slots
    }

    // Program the base and mask registers
    // The base register has the type in the lower 8 bits.
    u64 base_msr = (base & 0xFFFFFFFFFFFFF000ULL) | MTRR_TYPE_WC;
    wrmsr(MSR_MTRR_PHYS_BASE + (reg * 2), base_msr);

    // The mask register has the mask plus the valid bit (bit 11).
    u64 mask_msr = mask | (1 << 11);
    wrmsr(MSR_MTRR_PHYS_MASK + (reg * 2), mask_msr);

    // Enable MTRRs globally in the default type register.
    u64 def_type = rdmsr(MSR_MTRR_DEF_TYPE);
    def_type |= (1 << 11); // Set the MTRR enable bit
    wrmsr(MSR_MTRR_DEF_TYPE, def_type);

    return 0;
}