#ifndef IDT_H
#define IDT_H

#include "types.h"

/*
 * 64-bit Interrupt Descriptor Table entry.
 */
typedef struct {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed)) idt_entry_t;

/*
 * IDTR structure used by lidt.
 */
typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idt_ptr_t;

/*
 * IDT initialization.
 */
void idt_init(void);

/*
 * Install a single interrupt gate.
 */
void idt_set_gate(u8 vector, u64 handler, u8 type_attr);

/*
 * Install all 256 ISR/IRQ handlers.
 */
void idt_install_handlers(void);

/*
 * PIC initialization/remapping.
 */
void pic_remap(void);
void idt_load(idt_ptr_t *ptr);
void idt_activate(void);

#endif