#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

/* Initialize the PS/2 mouse. Enables IRQ12, sets defaults,
 * enables data reporting. */
void mouse_init(void);

/* Called from the IRQ12 handler with the byte just read from port 0x60. */
void mouse_handle_byte(u8 byte);

/* Current cursor position and button state. */
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_left_down(void);
int  mouse_right_down(void);
int  mouse_middle_down(void);

/* Set screen dimensions so movement can be clamped. */
void mouse_set_bounds(int w, int h);

#endif