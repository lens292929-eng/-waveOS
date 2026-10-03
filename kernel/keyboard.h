#pragma once

#include "types.h"

typedef enum {
    KEY_NONE = 0,

    KEY_CHAR,

    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,

    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,

    KEY_HOME,
    KEY_END,
    KEY_DELETE
} keyboard_key_t;

typedef struct {
    keyboard_key_t key;
    char character;

    int pressed;

    int shift;
    int ctrl;
    int caps;
} keyboard_event_t;

typedef void (*keyboard_handler_t)(const keyboard_event_t *event);

void keyboard_init(void);
void keyboard_handle_scancode(u8 sc);

void keyboard_set_handler(keyboard_handler_t handler);
keyboard_handler_t keyboard_get_handler(void);

void keyboard_pause_begin(void);
void keyboard_pause_end(void);
int keyboard_pause_wait(void);