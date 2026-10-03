#include "isr.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "framebuffer.h"
#include "pit.h"

__attribute__((ms_abi))
void isr_dispatch(registers_t *regs) {
    if (regs->vector == 0x20) {
        pit_handler();
        outb(0x20, 0x20);        /* EOI to master PIC */
        return;
    }

    if (regs->vector == 0x21) {
        u8 status = inb(0x64);
        if (status & 0x01) {
            u8 byte = inb(0x60);
            if (!(status & 0x20))
                keyboard_handle_scancode(byte);
        }
        outb(0x20, 0x20);
        return;
    }

    if (regs->vector == 0x2C) {           /* IRQ12 -> vector 44 (0x2C) */
        u8 status = inb(0x64);
        if (status & 0x01) {
            u8 byte = inb(0x60);
            if (status & 0x20)
                mouse_handle_byte(byte);
        }
        outb(0xA0, 0x20);                 /* EOI to slave PIC */
        outb(0x20, 0x20);                 /* EOI to master PIC */
        return;
    }
}