#ifndef ISR_H
#define ISR_H

#include "types.h"

typedef struct {
    // Saved by isr_common, in reverse stack order
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rbp;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;

    // Pushed by the ISR stub
    u64 vector;
    u64 error_code;

    // Pushed automatically by the CPU
    u64 rip;
    u64 cs;
    u64 rflags;
} registers_t;

__attribute__((ms_abi))
void isr_dispatch(registers_t *regs);
extern void (*isr_stub_table[256])(void);

#endif