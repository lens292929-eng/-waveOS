#include "idt.h"
#include "isr.h"   /* must declare: extern void (*isr_stub_table[256])(void); */

static idt_entry_t idt[256];
static idt_ptr_t idtp;

// Provided by idt_load.asm
extern void idt_load(idt_ptr_t *ptr);

/* --- 8259 PIC --- */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT  0x11   /* init + ICW4 needed */
#define ICW4_8086  0x01   /* 8086/88 mode */

static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void pic_remap(void) {
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD,  ICW1_INIT);  /* start init sequence, cascade */
    outb(PIC2_CMD,  ICW1_INIT);
    outb(PIC1_DATA, 0x20);       /* master vector offset = 0x20 */
    outb(PIC2_DATA, 0x28);       /* slave  vector offset = 0x28 */
    outb(PIC1_DATA, 0x04);       /* master: slave on IRQ2 */
    outb(PIC2_DATA, 0x02);       /* slave: cascade identity */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, mask1);      /* restore saved masks */
    outb(PIC2_DATA, mask2);
}

void idt_set_gate(u8 vector, u64 handler, u8 type_attr)
{
    u16 cs;

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(cs)
    );

    idt[vector].offset_low =
        (u16)(handler & 0xFFFF);

    idt[vector].selector = cs;

    idt[vector].ist = 0;
    idt[vector].type_attr = type_attr;

    idt[vector].offset_mid =
        (u16)((handler >> 16) & 0xFFFF);

    idt[vector].offset_high =
        (u32)((handler >> 32) & 0xFFFFFFFF);

    idt[vector].reserved = 0;
}

void idt_init(void)
{
    // Clear all IDT entries.
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low  = 0;
        idt[i].selector    = 0;
        idt[i].ist         = 0;
        idt[i].type_attr   = 0;
        idt[i].offset_mid  = 0;
        idt[i].offset_high = 0;
        idt[i].reserved    = 0;
    }

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u64)idt;

    // Load IDT register.
    idt_load(&idtp);
}

void idt_install_handlers(void) {
    for (int i = 0; i < 256; i++) {
        idt_set_gate((u8)i, (u64)isr_stub_table[i], 0x8E);
    }
}

void idt_activate(void) {
    idt_load(&idtp);
}