#include "keyboard.h"
#include "io.h"

static keyboard_handler_t keyboard_handler = 0;

/* --------------------------------------------------------- */
/* US QWERTY, PS/2 Set 1                                    */
/* --------------------------------------------------------- */

static const char keymap[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
    ' ',  0,    0,
};

static const char keymap_shift[128] = {
    0,    27,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|',  'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0,
    ' ',  0,    0,
};

static int shift = 0;
static int caps  = 0;
static int ctrl  = 0;

/* --------------------------------------------------------- */
/* Pause mode                                                 */
/* --------------------------------------------------------- */

static volatile int kb_pause_mode = 0;
static volatile int kb_pause_done = 0;

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

        /*
         * Enter make code.
         *
         * Pause mode intentionally consumes everything else.
         */
        if (sc == 0x1C)
            return 0;
    }
}

/* --------------------------------------------------------- */
/* Initialization                                             */
/* --------------------------------------------------------- */

void keyboard_init(void)
{
    while (inb(0x64) & 0x02)
        ;

    outb(0x64, 0xAE);

    while (inb(0x64) & 0x02)
        ;

    outb(0x64, 0x20);

    while (!(inb(0x64) & 0x01))
        ;

    u8 cfg = inb(0x60);

    /* Enable keyboard IRQ1. */
    cfg |= 0x01;

    /* Enable first PS/2 clock. */
    cfg &= ~0x10;

    while (inb(0x64) & 0x02)
        ;

    outb(0x64, 0x60);

    while (inb(0x64) & 0x02)
        ;

    outb(0x60, cfg);

    /*
     * Clear pending keyboard bytes.
     */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);

    shift = 0;
    caps  = 0;
    ctrl  = 0;

    keyboard_handler = 0;
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
/* Scancode handling                                          */
/* --------------------------------------------------------- */

void keyboard_handle_scancode(u8 sc)
{
    static int extended = 0;

    /* ----------------------------------------------------- */
    /* Pause mode                                             */
    /* ----------------------------------------------------- */

    if (kb_pause_mode) {
        if (sc == 0x1C)
            kb_pause_done = 1;

        return;
    }

    /* ----------------------------------------------------- */
    /* Extended keys                                          */
    /* ----------------------------------------------------- */

    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    if (extended) {
        extended = 0;

        /*
         * Ignore extended key releases.
         */
        if (sc & 0x80)
            return;

        keyboard_event_t event;

        event.key = KEY_NONE;
        event.character = 0;
        event.pressed = 1;
        event.shift = shift;
        event.ctrl = ctrl;
        event.caps = caps;

        switch (sc) {

            case 0x4B:
                event.key = KEY_LEFT;
                break;

            case 0x4D:
                event.key = KEY_RIGHT;
                break;

            case 0x48:
                event.key = KEY_UP;
                break;

            case 0x50:
                event.key = KEY_DOWN;
                break;

            case 0x47:
                event.key = KEY_HOME;
                break;

            case 0x4F:
                event.key = KEY_END;
                break;

            case 0x53:
                event.key = KEY_DELETE;
                break;

            default:
                return;
        }

        if (keyboard_handler)
            keyboard_handler(&event);

        return;
    }

    /* ----------------------------------------------------- */
    /* Key release                                            */
    /* ----------------------------------------------------- */

    if (sc & 0x80) {
        u8 make = sc & 0x7F;

        if (make == 0x2A || make == 0x36)
            shift = 0;

        if (make == 0x1D)
            ctrl = 0;

        return;
    }

    /* ----------------------------------------------------- */
    /* Modifier keys                                          */
    /* ----------------------------------------------------- */

    if (sc == 0x1D) {
        ctrl = 1;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift = 1;
        return;
    }

    if (sc == 0x3A) {
        caps = !caps;
        return;
    }

    if (sc >= 128)
        return;

    /* ----------------------------------------------------- */
    /* Translate scancode                                    */
    /* ----------------------------------------------------- */

    char c = shift
        ? keymap_shift[sc]
        : keymap[sc];

    if (!c)
        return;

    /*
     * Apply Caps Lock.
     */
    if (c >= 'a' && c <= 'z') {
        if (caps ^ shift)
            c -= 'a' - 'A';
    }
    else if (c >= 'A' && c <= 'Z') {
        if (!(caps ^ shift))
            c += 'a' - 'A';
    }

    keyboard_event_t event;

    event.key = KEY_NONE;
    event.character = c;
    event.pressed = 1;
    event.shift = shift;
    event.ctrl = ctrl;
    event.caps = caps;

    /* ----------------------------------------------------- */
    /* Special keys                                           */
    /* ----------------------------------------------------- */

    if (c == '\n')
        event.key = KEY_ENTER;

    else if (c == '\b')
        event.key = KEY_BACKSPACE;

    else if (c == '\t')
        event.key = KEY_TAB;

    else if (c >= 0x20 && c <= 0x7E)
        event.key = KEY_CHAR;

    else
        return;

    /* ----------------------------------------------------- */
    /* Deliver event                                          */
    /* ----------------------------------------------------- */

    if (keyboard_handler)
        keyboard_handler(&event);
}