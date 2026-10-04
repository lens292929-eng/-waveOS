#include "keyboard.h"
#include "io.h"
#include "edit.h"

/* --------------------------------------------------------- */
/* Keyboard handler                                          */
/* --------------------------------------------------------- */

static keyboard_handler_t keyboard_handler = 0;

/* --------------------------------------------------------- */
/* US QWERTY - Set 1 (unshifted)                             */
/* --------------------------------------------------------- */

static const char keymap[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
    ' ',  0,    0,
};

/* --------------------------------------------------------- */
/* US QWERTY - Set 1 (shifted)                               */
/* --------------------------------------------------------- */

static const char keymap_shift[128] = {
    0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|',  'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0,
    ' ',  0,    0,
};

/* --------------------------------------------------------- */
/* Keyboard state                                             */
/* --------------------------------------------------------- */

static int shift = 0;
static int caps  = 0;
static int ctrl  = 0;

static int extended = 0;

/* --------------------------------------------------------- */
/* Pause mode                                                 */
/* --------------------------------------------------------- */

static volatile int kb_pause_mode = 0;
static volatile int kb_pause_done = 0;

/* keyboard.c, near the top */
static int editor_active = 0;

void keyboard_set_editor_active(int active)
{
    editor_active = active;
}

int keyboard_editor_is_active(void)
{
    return editor_active;
}



void keyboard_pause_begin(void)
{
    kb_pause_mode = 1;
    kb_pause_done = 0;
}

void keyboard_pause_end(void)
{
    kb_pause_mode = 0;
    kb_pause_done = 0;
}

int keyboard_pause_wait(void)
{
    for (;;) {
        u8 status = inb(0x64);
        if (!(status & 0x01))
            continue;

        u8 sc = inb(0x60);

        /* Set 1 Enter make code. */
        if (sc == 0x1C)
            return 0;
    }
}

/* --------------------------------------------------------- */
/* PS/2 controller helpers                                   */
/* --------------------------------------------------------- */

static void ps2_wait_input(void)
{
    while (inb(0x64) & 0x02)
        ;
}

static void ps2_wait_output(void)
{
    while (!(inb(0x64) & 0x01))
        ;
}

/* --------------------------------------------------------- */
/* Initialization                                             */
/* --------------------------------------------------------- */

void keyboard_init(void)
{
    u8 cfg;

    /* 1. Disable the keyboard while we reconfigure. */
    ps2_wait_input();
    outb(0x64, 0xAD);

    /* 2. Flush any pending bytes. */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);

    /* 3. Read controller config byte (command 0x20). */
    ps2_wait_input();
    outb(0x64, 0x20);
    ps2_wait_output();
    cfg = inb(0x60);

    /* 4. Force known-good state. */
    cfg |=  0x40;   /* translation ON: keyboard -> Set 1 for the CPU */
    cfg |=  0x01;   /* enable IRQ1                                   */
    cfg &= ~0x10;   /* enable first PS/2 port clock                  */
    cfg &= ~0x20;   /* enable second PS/2 port clock (mouse)         */

    /* 5. Write config byte back (command 0x60). */
    ps2_wait_input();
    outb(0x64, 0x60);
    ps2_wait_input();
    outb(0x60, cfg);

    /* 6. Re-enable the keyboard. */
    ps2_wait_input();
    outb(0x64, 0xAE);

    /* 7. Reset state. */
    shift    = 0;
    caps     = 0;
    ctrl     = 0;
    extended = 0;

    kb_pause_mode = 0;
    kb_pause_done = 0;

    keyboard_handler = 0;

    /* 8. Flush again — the controller may have queued garbage. */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
}

/* --------------------------------------------------------- */
/* Handler registration                                       */
/* --------------------------------------------------------- */

void keyboard_set_handler(keyboard_handler_t handler)
{
    keyboard_handler = handler;
}

keyboard_handler_t keyboard_get_handler(void)
{
    return keyboard_handler;
}

/* --------------------------------------------------------- */
/* Emit character event                                       */
/* --------------------------------------------------------- */

static void keyboard_emit_char(char c)
{
    if (!c)
        return;

    /* Caps Lock affects letters only. */
    if (c >= 'a' && c <= 'z') {
        if (caps ^ shift)
            c -= 'a' - 'A';
    }
    else if (c >= 'A' && c <= 'Z') {
        if (!(caps ^ shift))
            c += 'a' - 'A';
    }

    keyboard_event_t event;
    event.key       = KEY_NONE;
    event.character = c;
    event.pressed   = 1;
    event.shift     = shift;
    event.ctrl      = ctrl;
    event.caps      = caps;

    if (c == '\n')       event.key = KEY_ENTER;
    else if (c == '\b')  event.key = KEY_BACKSPACE;
    else if (c == '\t')  event.key = KEY_TAB;
    else if (c >= 0x20 && c <= 0x7E)
                         event.key = KEY_CHAR;
    else
        return;

    if (keyboard_handler)
        keyboard_handler(&event);
}

/* --------------------------------------------------------- */
/* Set 1 scancode handler                                    */
/* --------------------------------------------------------- */

static void keyboard_handle_set1(u8 sc)
{
    /*
     * Extended prefix (arrow keys, nav, keypad).
     */
    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    if (extended) {
        extended = 0;

        /* Ignore extended key releases. */
        if (sc & 0x80)
            return;

        keyboard_event_t event;
        event.key       = KEY_NONE;
        event.character = 0;
        event.pressed   = 1;
        event.shift     = shift;
        event.ctrl      = ctrl;
        event.caps      = caps;

        switch (sc) {
            case 0x4B: event.key = KEY_LEFT;   break;
            case 0x4D: event.key = KEY_RIGHT;  break;
            case 0x48: event.key = KEY_UP;     break;
            case 0x50: event.key = KEY_DOWN;   break;
            case 0x47: event.key = KEY_HOME;   break;
            case 0x4F: event.key = KEY_END;    break;
            case 0x53: event.key = KEY_DELETE; break;
            default:   return;
        }

        if (keyboard_handler)
            keyboard_handler(&event);
        return;
    }

    /*
     * Release codes have bit 7 set.
     */
    if (sc & 0x80) {
        u8 make = sc & 0x7F;

        if (make == 0x2A || make == 0x36) shift = 0;
        if (make == 0x1D)                 ctrl  = 0;

        return;
    }

    /*
     * Modifier make codes.
     */
    if (sc == 0x1D) {           /* Ctrl */
        ctrl = 1;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {   /* Shift */
        shift = 1;
        return;
    }

    if (sc == 0x3A) {           /* Caps Lock */
        caps = !caps;
        return;
    }

    if (sc >= 128)
        return;

    keyboard_emit_char(shift ? keymap_shift[sc] : keymap[sc]);
}

/* --------------------------------------------------------- */
/* Main scancode entry point                                 */
/* --------------------------------------------------------- */

void keyboard_handle_scancode(u8 sc)
{
    /*
     * Pause mode: consume everything, wake on Enter.
     */
    if (kb_pause_mode) {
        if (sc == 0x1C)
            kb_pause_done = 1;
        return;
    }

    keyboard_handle_set1(sc);
}
