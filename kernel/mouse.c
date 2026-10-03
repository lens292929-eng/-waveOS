#include "mouse.h"
#include "io.h"
#include "framebuffer.h"

/*
 * PS/2 mouse driver.
 *
 * The mouse sends 3-byte packets:
 *   byte 0: flags
 *     bit 0: left button
 *     bit 1: right button
 *     bit 2: middle button
 *     bit 3: always 1
 *     bit 4: X sign
 *     bit 5: Y sign
 *     bit 6: X overflow
 *     bit 7: Y overflow
 *   byte 1: X movement (signed, or use sign bit from byte 0)
 *   byte 2: Y movement (same)
 */

static u8  packet[3];
static int packet_index = 0;

static int mouse_x = 400;
static int mouse_y = 300;

static int bounds_w = 1024;
static int bounds_h = 768;

static u8 buttons = 0;

/* --- low-level PS/2 helpers --- */

static void mouse_wait_write(void)
{
    /* Wait until the controller's input buffer is empty. */
    for (u32 i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02))
            return;
    }
}

static void mouse_wait_read(void)
{
    /* Wait until the controller's output buffer has data. */
    for (u32 i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01)
            return;
    }
}

static void mouse_write(u8 value)
{
    /* Tell the controller we're talking to the auxiliary device. */
    mouse_wait_write();
    outb(0x64, 0xD4);

    /* Send the byte to the mouse. */
    mouse_wait_write();
    outb(0x60, value);
}

static u8 mouse_read(void)
{
    mouse_wait_read();
    return inb(0x60);
}

/* --- init --- */

void mouse_set_bounds(int w, int h)
{
    bounds_w = w;
    bounds_h = h;

    if (mouse_x >= w) mouse_x = w - 1;
    if (mouse_y >= h) mouse_y = h - 1;
}

void mouse_init(void)
{
    /* Enable the auxiliary (mouse) device. */
    mouse_wait_write();
    outb(0x64, 0xA8);

    /* Enable IRQ12 by clearing bit 5 of the config byte. */
    mouse_wait_write();
    outb(0x64, 0x20);
    u8 status = mouse_read();

    status &= ~0x20;    /* clear bit 5 -> enable IRQ12 */
    status |=  0x02;    /* set bit 1 -> enable mouse clock */

    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    /* Set defaults. */
    mouse_write(0xF6);
    mouse_read();       /* ACK (0xFA) */

    /* Enable data reporting. */
    mouse_write(0xF4);
    mouse_read();       /* ACK (0xFA) */

    /* Drain any pending bytes. */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);

    packet_index = 0;
    buttons = 0;
}

/* --- packet handling --- */

void mouse_handle_byte(u8 byte)
{
    /*
     * Resynchronize: the first byte of every packet has bit 3 set.
     * If we're expecting byte 0 and this isn't a valid first byte,
     * ignore it. Otherwise accept in order.
     */
    if (packet_index == 0 && !(byte & 0x08))
        return;

    packet[packet_index++] = byte;

    if (packet_index < 3)
        return;

    packet_index = 0;

    u8 flags = packet[0];
    u8 dx    = packet[1];
    u8 dy    = packet[2];

    /* Ignore packets with overflow. */
    if (flags & 0x40) return;
    if (flags & 0x80) return;

    /* Reconstruct signed deltas. */
    int sx = dx;
    int sy = dy;

    if (flags & 0x10) sx |= 0xFFFFFF00;   /* X sign extend */
    if (flags & 0x20) sy |= 0xFFFFFF00;   /* Y sign extend */

    /* Apply. Y is inverted: mouse up = lower y in screen coords. */
    mouse_x += sx;
    mouse_y -= sy;

    /* Clamp to bounds. */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= bounds_w) mouse_x = bounds_w - 1;
    if (mouse_y >= bounds_h) mouse_y = bounds_h - 1;

    buttons = flags & 0x07;
}

/* --- accessors --- */

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }

int mouse_left_down(void)   { return (buttons & 1) != 0; }
int mouse_right_down(void)  { return (buttons & 2) != 0; }
int mouse_middle_down(void) { return (buttons & 4) != 0; }