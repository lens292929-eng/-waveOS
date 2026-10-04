#include "edit.h"
#include "keyboard.h"
#include "framebuffer.h"
#include "types.h"
#include "ramdisk.h"
#include "shell.h"
#include "pit.h"
#include "colors.h"

static char editor_filename[32] = "";   /* current file, empty = unnamed */
static int  editor_dirty = 0;           /* unsaved changes */

/*
 * waveOS EDIT
 *
 * Prototype GUI/TUI editor.
 */

/* ---------- theme: One Dark-ish ---------- */

#define EDIT_BG             0x0028282C
#define EDIT_CHROME         0x001E1E22
#define EDIT_BAR            0x00343A46
#define EDIT_BAR_TEXT       0x00ABB2BF
#define EDIT_TEXT           0x00D8DEE9
#define EDIT_CURSOR         0x00528BFF
#define EDIT_GUTTER_TEXT    0x004B5261
#define EDIT_GUTTER_LINE    0x00323844
#define EDIT_SUGGEST        0x00575F6E
#define EDIT_LINENUM_CUR    0x00ABB2BF
#define EDIT_CURRENT_LINE   0x002C323D

#define EDIT_BAR_HEIGHT     24
#define EDIT_MARGIN_X       48
#define EDIT_MARGIN_Y       4
#define EDIT_GUTTER_LEFT    (EDIT_MARGIN_X - 8)

#define EDIT_FONT_WIDTH     8
#define EDIT_FONT_HEIGHT    16

#define EDIT_MAX_LINES      256
#define EDIT_LINE_LENGTH    256

static char editor_buffer[EDIT_MAX_LINES][EDIT_LINE_LENGTH];

static int cursor_line = 0;
static int cursor_col  = 0;

static int scroll_line = 0;

static int editor_active = 0;

static int  editor_tab_size = 4;
static char editor_suggest[EDIT_LINE_LENGTH];
static int  editor_suggest_len = 0;


/* --------------------------------------------------------- */
/* Keyword tables                                            */
/* --------------------------------------------------------- */

static const char *suggest_c[] = {
    "int", "char", "void", "long", "short", "unsigned", "signed",
    "if", "else", "while", "for", "return", "struct", "enum",
    "typedef", "static", "const", "sizeof", "switch", "case",
    "break", "continue", "default", "goto", "do",
    0
};

static const char *suggest_asm[] = {
    "mov", "add", "sub", "mul", "div", "inc", "dec",
    "jmp", "je", "jne", "jg", "jl", "jge", "jle",
    "call", "ret", "push", "pop", "cmp", "test",
    "and", "or", "xor", "not", "shl", "shr",
    "nop", "int", "cli", "sti", "hlt",
    "db", "dw", "dd", "dq",
    "bits", "org", "section", "global", "extern",
    0
};

static const char *suggest_wss[] = {
    "print", "pause",
    0
};


int editor_is_active(void)
{
    return editor_active;
}

/* --------------------------------------------------------- */
/* Helpers                                                   */
/* --------------------------------------------------------- */

static int editor_strlen(const char *str)
{
    int length = 0;

    while (str[length] != '\0')
        length++;

    return length;
}


static int editor_visible_lines(void)
{
    int height = (int)framebuffer_get_height();

    height -= EDIT_BAR_HEIGHT * 2;
    height -= EDIT_MARGIN_Y * 2;

    if (height <= 0)
        return 1;

    return height / EDIT_FONT_HEIGHT;
}


static void editor_clear_buffer(void)
{
    for (int line = 0; line < EDIT_MAX_LINES; line++) {
        for (int col = 0; col < EDIT_LINE_LENGTH; col++) {
            editor_buffer[line][col] = '\0';
        }
    }
}


static void editor_scroll_into_view(void)
{
    int visible = editor_visible_lines();

    if (cursor_line < scroll_line)
        scroll_line = cursor_line;

    if (cursor_line >= scroll_line + visible)
        scroll_line = cursor_line - visible + 1;

    if (scroll_line < 0)
        scroll_line = 0;
}


static void editor_draw_number(u32 x, u32 y, int n, u32 fg, u32 bg)
{
    char buf[12];
    int  len = 0;

    if (n == 0) buf[len++] = '0';
    while (n > 0) { buf[len++] = '0' + (n % 10); n /= 10; }

    for (int i = len - 1; i >= 0; i--) {
        framebuffer_draw_char(buf[i], x, y, fg, bg);
        x += EDIT_FONT_WIDTH;
    }
}


static int editor_ext_is(const char *ext, int extlen)
{
    const char *n = editor_filename;
    int len = editor_strlen(n);

    if (len < extlen + 1) return 0;
    if (n[len - extlen - 1] != '.') return 0;

    for (int i = 0; i < extlen; i++) {
        char a = n[len - extlen + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }

    return 1;
}


static const char **editor_suggest_table(void)
{
    if (editor_ext_is("asm", 3)) return suggest_asm;
    if (editor_ext_is("s",   1)) return suggest_asm;
    if (editor_ext_is("wss", 3)) return suggest_wss;
    return suggest_c;
}


static int str_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}


static void editor_update_suggestion(void)
{
    editor_suggest[0] = '\0';
    editor_suggest_len = 0;

    /* Find the current word (letters/digits/underscore). */
    int end = cursor_col;
    int start = end;

    while (start > 0) {
        char c = editor_buffer[cursor_line][start - 1];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_')
            start--;
        else
            break;
    }

    if (start == end) return;   /* empty word */

    char word[64];
    int  wlen = end - start;
    if (wlen >= (int)sizeof(word)) return;

    for (int i = 0; i < wlen; i++)
        word[i] = editor_buffer[cursor_line][start + i];
    word[wlen] = '\0';

    const char **table = editor_suggest_table();

    for (int i = 0; table[i]; i++) {
        if (!str_starts_with(table[i], word)) continue;

        int klen = editor_strlen(table[i]);
        if (klen == wlen) continue;   /* already typed in full */

        /* Only suggest when the cursor is at the end of the line's text. */
        if (editor_buffer[cursor_line][cursor_col] != '\0')
            return;

        const char *rest = table[i] + wlen;
        int rlen = editor_strlen(rest);

        for (int j = 0; j < rlen && j < EDIT_LINE_LENGTH - 1; j++)
            editor_suggest[j] = rest[j];
        editor_suggest[rlen] = '\0';
        editor_suggest_len = rlen;
        return;
    }
}


/* --------------------------------------------------------- */
/* Drawing                                                   */
/* --------------------------------------------------------- */

static void editor_draw_bars(void)
{
    int width  = (int)framebuffer_get_width();
    int height = (int)framebuffer_get_height();

    framebuffer_fill_rect(
        (u32)0,
        (u32)0,
        (u32)width,
        (u32)EDIT_BAR_HEIGHT,
        EDIT_BAR
    );

    framebuffer_fill_rect(
        (u32)0,
        (u32)(height - EDIT_BAR_HEIGHT),
        (u32)width,
        (u32)EDIT_BAR_HEIGHT,
        EDIT_BAR
    );
}


static void editor_draw_menu(void)
{
    u32 y = 4;

    const char *name = editor_filename[0] ? editor_filename : "[untitled]";
    int x = 8;
    for (int i = 0; name[i] && x < 400; i++, x += EDIT_FONT_WIDTH)
        framebuffer_draw_char(name[i], (u32)x, y, EDIT_BAR_TEXT, EDIT_BAR);

    if (editor_dirty)
        framebuffer_draw_char('*', (u32)x, y, EDIT_BAR_TEXT, EDIT_BAR);

    int right = (int)framebuffer_get_width() - 8;

    const char *help = "Help";
    for (int i = 3; i >= 0; i--) {
        right -= EDIT_FONT_WIDTH;
        framebuffer_draw_char(help[i], (u32)right, y, EDIT_BAR_TEXT, EDIT_BAR);
    }

    right -= 16;
    const char *edit = "Edit";
    for (int i = 3; i >= 0; i--) {
        right -= EDIT_FONT_WIDTH;
        framebuffer_draw_char(edit[i], (u32)right, y, EDIT_BAR_TEXT, EDIT_BAR);
    }

    right -= 16;
    const char *file = "File";
    for (int i = 3; i >= 0; i--) {
        right -= EDIT_FONT_WIDTH;
        framebuffer_draw_char(file[i], (u32)right, y, EDIT_BAR_TEXT, EDIT_BAR);
    }
}


static void editor_draw_status(void)
{
    int height = (int)framebuffer_get_height();
    u32 y = (u32)(height - EDIT_BAR_HEIGHT + 4);

    framebuffer_draw_char('L',  8, y, EDIT_BAR_TEXT, EDIT_BAR);
    framebuffer_draw_char('n', 16, y, EDIT_BAR_TEXT, EDIT_BAR);
    framebuffer_draw_char(':', 24, y, EDIT_BAR_TEXT, EDIT_BAR);
    editor_draw_number(32, y, cursor_line + 1, EDIT_BAR_TEXT, EDIT_BAR);

    framebuffer_draw_char('C',  72, y, EDIT_BAR_TEXT, EDIT_BAR);
    framebuffer_draw_char('o',  80, y, EDIT_BAR_TEXT, EDIT_BAR);
    framebuffer_draw_char('l',  88, y, EDIT_BAR_TEXT, EDIT_BAR);
    framebuffer_draw_char(':',  96, y, EDIT_BAR_TEXT, EDIT_BAR);
    editor_draw_number(104, y, cursor_col + 1, EDIT_BAR_TEXT, EDIT_BAR);

    int right = (int)framebuffer_get_width() - 8;

    const char *ins = "INS";
    for (int i = 2; i >= 0; i--) {
        right -= EDIT_FONT_WIDTH;
        framebuffer_draw_char(ins[i], (u32)right, y, EDIT_BAR_TEXT, EDIT_BAR);
    }

    if (editor_dirty) {
        right -= 16;
        framebuffer_draw_char('*', (u32)right, y, EDIT_BAR_TEXT, EDIT_BAR);
    }
}


static void editor_draw_line(int line)
{
    int screen_line = line - scroll_line;

    if (screen_line < 0)
        return;

    if (screen_line >= editor_visible_lines())
        return;

    int y = EDIT_BAR_HEIGHT
          + EDIT_MARGIN_Y
          + screen_line * EDIT_FONT_HEIGHT;

    int width = (int)framebuffer_get_width();
    int is_cur = (line == cursor_line);

    if (is_cur) {
        framebuffer_fill_rect(
            (u32)EDIT_GUTTER_LEFT,
            (u32)y,
            (u32)(width - EDIT_GUTTER_LEFT),
            (u32)EDIT_FONT_HEIGHT,
            EDIT_CURRENT_LINE
        );
    }

    {
        int num = line + 1;
        char nbuf[12];
        int  nlen = 0;

        if (num == 0) nbuf[nlen++] = '0';
        while (num > 0) { nbuf[nlen++] = '0' + (num % 10); num /= 10; }

        int nx = EDIT_MARGIN_X - 8 - nlen * EDIT_FONT_WIDTH;
        for (int i = nlen - 1; i >= 0; i--) {
            u32 fg = is_cur ? EDIT_LINENUM_CUR : EDIT_GUTTER_TEXT;
            framebuffer_draw_char(nbuf[i], (u32)nx, (u32)y,
                                  fg, EDIT_CHROME);
            nx += EDIT_FONT_WIDTH;
        }

        framebuffer_fill_rect(
            (u32)(EDIT_MARGIN_X - 4),
            (u32)y,
            1,
            (u32)EDIT_FONT_HEIGHT,
            EDIT_GUTTER_LINE
        );
    }

    u32 text_bg = is_cur ? EDIT_CURRENT_LINE : EDIT_BG;

    int x = EDIT_MARGIN_X;

    for (int col = 0; col < EDIT_LINE_LENGTH; col++) {
        char c = editor_buffer[line][col];

        if (c == '\0')
            break;

        if (x + EDIT_FONT_WIDTH > width)
            break;

        framebuffer_draw_char(c, (u32)x, (u32)y, EDIT_TEXT, text_bg);

        x += EDIT_FONT_WIDTH;
    }

    if (is_cur && editor_suggest_len > 0) {
        int gx = EDIT_MARGIN_X + cursor_col * EDIT_FONT_WIDTH;

        for (int i = 0; i < editor_suggest_len; i++) {
            if (gx + EDIT_FONT_WIDTH > width) break;

            framebuffer_draw_char(editor_suggest[i], (u32)gx, (u32)y,
                                  EDIT_SUGGEST, text_bg);
            gx += EDIT_FONT_WIDTH;
        }
    }
}


static void editor_draw_cursor(void)
{
    int screen_line = cursor_line - scroll_line;

    if (screen_line < 0)
        return;

    if (screen_line >= editor_visible_lines())
        return;

    int x = EDIT_MARGIN_X + cursor_col * EDIT_FONT_WIDTH;
    int y = EDIT_BAR_HEIGHT
          + EDIT_MARGIN_Y
          + screen_line * EDIT_FONT_HEIGHT;

    char c = editor_buffer[cursor_line][cursor_col];
    if (c == '\0') c = ' ';

    framebuffer_draw_char(c, (u32)x, (u32)y, EDIT_TEXT, EDIT_CURRENT_LINE);

    framebuffer_fill_rect((u32)x,
                          (u32)y,
                          (u32)EDIT_FONT_WIDTH, 1,
                          EDIT_CURSOR);
    framebuffer_fill_rect((u32)x,
                          (u32)(y + EDIT_FONT_HEIGHT - 1),
                          (u32)EDIT_FONT_WIDTH, 1,
                          EDIT_CURSOR);
    framebuffer_fill_rect((u32)x,
                          (u32)y,
                          1, (u32)EDIT_FONT_HEIGHT,
                          EDIT_CURSOR);
    framebuffer_fill_rect((u32)(x + EDIT_FONT_WIDTH - 1),
                          (u32)y,
                          1, (u32)EDIT_FONT_HEIGHT,
                          EDIT_CURSOR);
}


static void editor_redraw(void)
{
    int width  = (int)framebuffer_get_width();
    int height = (int)framebuffer_get_height();

    framebuffer_clear(EDIT_CHROME);

    framebuffer_fill_rect(
        (u32)0,
        (u32)EDIT_BAR_HEIGHT,
        (u32)EDIT_GUTTER_LEFT,
        (u32)(height - EDIT_BAR_HEIGHT * 2),
        EDIT_CHROME
    );

    framebuffer_fill_rect(
        (u32)EDIT_GUTTER_LEFT,
        (u32)EDIT_BAR_HEIGHT,
        (u32)(width - EDIT_GUTTER_LEFT),
        (u32)(height - EDIT_BAR_HEIGHT * 2),
        EDIT_BG
    );

    editor_draw_bars();
    editor_draw_menu();

    int visible = editor_visible_lines();
    for (int line = scroll_line;
         line < scroll_line + visible && line < EDIT_MAX_LINES;
         line++) {
        editor_draw_line(line);
    }

    editor_draw_status();
    editor_draw_cursor();
}


/* --------------------------------------------------------- */
/* Text editing                                              */
/* --------------------------------------------------------- */

static void editor_insert_char(char c)
{
    if (cursor_line < 0 || cursor_line >= EDIT_MAX_LINES)
        return;

    if (cursor_col < 0 || cursor_col >= EDIT_LINE_LENGTH - 1)
        return;

    int length = editor_strlen(editor_buffer[cursor_line]);

    if (length >= EDIT_LINE_LENGTH - 1)
        return;

    for (int i = length; i >= cursor_col; i--) {
        editor_buffer[cursor_line][i + 1] =
            editor_buffer[cursor_line][i];
    }

    editor_buffer[cursor_line][cursor_col] = c;
    cursor_col++;
}


static void editor_insert_tab(void)
{
    int pad = editor_tab_size - (cursor_col % editor_tab_size);
    for (int i = 0; i < pad; i++)
        editor_insert_char(' ');
}


static void editor_delete_char(void)
{
    if (cursor_line < 0 || cursor_line >= EDIT_MAX_LINES)
        return;

    int length = editor_strlen(editor_buffer[cursor_line]);

    if (cursor_col >= length)
        return;

    for (int i = cursor_col; i < length; i++) {
        editor_buffer[cursor_line][i] =
            editor_buffer[cursor_line][i + 1];
    }
}


static void editor_backspace(void)
{
    if (cursor_col > 0) {
        int length = editor_strlen(editor_buffer[cursor_line]);

        for (int i = cursor_col - 1; i < length; i++) {
            editor_buffer[cursor_line][i] =
                editor_buffer[cursor_line][i + 1];
        }

        cursor_col--;
        return;
    }

    if (cursor_line == 0)
        return;

    int previous_length =
        editor_strlen(editor_buffer[cursor_line - 1]);

    int current_length =
        editor_strlen(editor_buffer[cursor_line]);

    if (previous_length + current_length >= EDIT_LINE_LENGTH)
        return;

    for (int i = 0; i <= current_length; i++) {
        editor_buffer[cursor_line - 1][previous_length + i] =
            editor_buffer[cursor_line][i];
    }

    for (int line = cursor_line;
         line < EDIT_MAX_LINES - 1;
         line++) {

        for (int col = 0; col < EDIT_LINE_LENGTH; col++) {
            editor_buffer[line][col] =
                editor_buffer[line + 1][col];
        }
    }

    for (int col = 0; col < EDIT_LINE_LENGTH; col++) {
        editor_buffer[EDIT_MAX_LINES - 1][col] = '\0';
    }

    cursor_line--;
    cursor_col = previous_length;

    editor_scroll_into_view();
}


static void editor_newline(void)
{
    if (cursor_line >= EDIT_MAX_LINES - 1)
        return;

    /* Figure out the indent of the current line up to the cursor. */
    int indent = 0;
    while (indent < cursor_col &&
           editor_buffer[cursor_line][indent] == ' ')
        indent++;

    /* If the char just before the cursor is `{`, add one tab-size. */
    if (cursor_col > 0 &&
        editor_buffer[cursor_line][cursor_col - 1] == '{')
        indent += editor_tab_size;

    /* Cap it so we don't overflow the line buffer. */
    if (indent > EDIT_LINE_LENGTH - 2)
        indent = EDIT_LINE_LENGTH - 2;

    /* Move lines downward to make room. */
    for (int line = EDIT_MAX_LINES - 1;
         line > cursor_line + 1;
         line--) {

        for (int col = 0; col < EDIT_LINE_LENGTH; col++) {
            editor_buffer[line][col] =
                editor_buffer[line - 1][col];
        }
    }

    /* Split current line at cursor_col. */
    for (int col = cursor_col; col < EDIT_LINE_LENGTH; col++) {
        editor_buffer[cursor_line + 1][col - cursor_col] =
            editor_buffer[cursor_line][col];

        editor_buffer[cursor_line][col] = '\0';
    }

    cursor_line++;
    cursor_col = 0;

    /* Insert the leading spaces on the new line. */
    for (int i = 0; i < indent; i++)
        editor_insert_char(' ');

    editor_scroll_into_view();
}


static void editor_save(void)
{
    int last = -1;
    for (int line = 0; line < EDIT_MAX_LINES; line++) {
        if (editor_strlen(editor_buffer[line]) > 0)
            last = line;
    }

    char blob[RAMDISK_DATA_MAX];
    int  n = 0;

    for (int line = 0; line <= last && n < RAMDISK_DATA_MAX - 1; line++) {
        int len = editor_strlen(editor_buffer[line]);
        for (int col = 0; col < len && n < RAMDISK_DATA_MAX - 1; col++)
            blob[n++] = editor_buffer[line][col];
        if (n < RAMDISK_DATA_MAX - 1)
            blob[n++] = '\n';
    }

    const char *name;

    if (editor_filename[0]) {
        name = editor_filename;
    } else {
        editor_filename[0] = '/';
        const char *u = "untitled";
        int i = 1;
        while (u[i - 1] && i < 31) {
            editor_filename[i] = u[i - 1];
            i++;
        }
        editor_filename[i] = '\0';
        name = editor_filename;
    }

    if (ramdisk_write(name, blob, n) < 0)
        return;

    editor_dirty = 0;
}


static void editor_load(const char *name)
{
    char blob[RAMDISK_DATA_MAX];
    int  n = ramdisk_read(name, blob, RAMDISK_DATA_MAX);
    if (n < 0) return;

    editor_clear_buffer();

    int line = 0, col = 0;
    for (int i = 0; i < n && line < EDIT_MAX_LINES; i++) {
        if (blob[i] == '\n') { line++; col = 0; continue; }
        if (col < EDIT_LINE_LENGTH - 1)
            editor_buffer[line][col++] = blob[i];
    }

    for (int i = 0; i < 32 && name[i]; i++) editor_filename[i] = name[i];
    cursor_line = cursor_col = scroll_line = 0;
    editor_dirty = 0;
    editor_suggest[0] = '\0';
    editor_suggest_len = 0;
}

/* --------------------------------------------------------- */
/* Keyboard input                                            */
/* --------------------------------------------------------- */


static void edit_close(void)
{
    editor_active = 0;

    keyboard_set_handler(shell_keyboard_handler);

    framebuffer_clear(COLOR_WAVE_BG);

    print_set_colors(0x00FFFFFF, COLOR_WAVE_BG);

    shell_prompt();
}

void edit_handle_key(int key)
{
    if (!editor_active)
        return;

    /* Ctrl+Q — quit editor */
    if (key == 17) {
        edit_close();
        return;
    }

    /* Ctrl+S — save */
    if (key == 19) {
        editor_save();
        editor_redraw();
        return;
    }

    /* Ctrl+O — load */
    if (key == 15) {
        editor_load(editor_filename[0] ? editor_filename : "untitled");
        editor_redraw();
        return;
    }

    switch (key) {

        case EDIT_KEY_LEFT:
            if (cursor_col > 0) {
                cursor_col--;
            }
            else if (cursor_line > 0) {
                cursor_line--;
                cursor_col =
                    editor_strlen(editor_buffer[cursor_line]);
            }
            break;


        case EDIT_KEY_RIGHT:
        {
            int length =
                editor_strlen(editor_buffer[cursor_line]);

            if (cursor_col < length)
                cursor_col++;
            break;
        }


        case EDIT_KEY_UP:
            if (cursor_line > 0) {
                cursor_line--;

                int length =
                    editor_strlen(editor_buffer[cursor_line]);

                if (cursor_col > length)
                    cursor_col = length;
            }

            editor_scroll_into_view();
            break;


        case EDIT_KEY_DOWN:
            if (cursor_line < EDIT_MAX_LINES - 1) {
                cursor_line++;

                int length =
                    editor_strlen(editor_buffer[cursor_line]);

                if (cursor_col > length)
                    cursor_col = length;
            }

            editor_scroll_into_view();
            break;


        case EDIT_KEY_HOME:
            cursor_col = 0;
            break;


        case EDIT_KEY_END:
            cursor_col =
                editor_strlen(editor_buffer[cursor_line]);
            break;


        case EDIT_KEY_BACKSPACE:
            editor_backspace();
            editor_dirty = 1;
            break;


        case EDIT_KEY_DELETE:
            editor_delete_char();
            editor_dirty = 1;
            break;


        case EDIT_KEY_ENTER:
            editor_newline();
            editor_dirty = 1;
            break;


        case '\t':
            if (editor_suggest_len > 0) {
                for (int i = 0; i < editor_suggest_len; i++)
                    editor_insert_char(editor_suggest[i]);
                editor_suggest[0] = '\0';
                editor_suggest_len = 0;
            } else {
                editor_insert_tab();
            }
            editor_dirty = 1;
            break;


        default:
            if (key >= 32 && key <= 126) {
                char c = (char)key;

                /* If the next char is the same closing bracket, skip over it. */
                if ((c == ')' || c == ']' || c == '}') &&
                    editor_buffer[cursor_line][cursor_col] == c) {
                    cursor_col++;
                    editor_dirty = 1;
                    break;
                }

                char closer = 0;
                if (c == '(') closer = ')';
                else if (c == '[') closer = ']';
                else if (c == '{') closer = '}';
                else if (c == '"')  closer = '"';
                else if (c == '\'') closer = '\'';

                if (closer != 0) {
                    editor_insert_char(c);
                    editor_insert_char(closer);
                    cursor_col--;
                    editor_dirty = 1;
                } else {
                    editor_insert_char(c);
                    editor_dirty = 1;
                }
            }
            else {
                return;
            }

            break;
    }

    editor_update_suggestion();
    editor_redraw();
}


static void edit_keyboard_handler(const keyboard_event_t *event)
{
    if (!event->pressed)
        return;

    if (event->ctrl) {
        if (event->character == 'q' ||
            event->character == 'Q') {
            edit_handle_key(17);   /* Ctrl+Q */
            return;
        }

        if (event->character == 's' ||
            event->character == 'S') {
            edit_handle_key(19);   /* Ctrl+S */
            return;
        }

        if (event->character == 'o' ||
            event->character == 'O') {
            edit_handle_key(15);   /* Ctrl+O */
            return;
        }
    }

    switch (event->key) {
        case KEY_LEFT:
            edit_handle_key(EDIT_KEY_LEFT);
            break;

        case KEY_RIGHT:
            edit_handle_key(EDIT_KEY_RIGHT);
            break;

        case KEY_UP:
            edit_handle_key(EDIT_KEY_UP);
            break;

        case KEY_DOWN:
            edit_handle_key(EDIT_KEY_DOWN);
            break;

        case KEY_HOME:
            edit_handle_key(EDIT_KEY_HOME);
            break;

        case KEY_END:
            edit_handle_key(EDIT_KEY_END);
            break;

        case KEY_DELETE:
            edit_handle_key(EDIT_KEY_DELETE);
            break;

        case KEY_BACKSPACE:
            edit_handle_key(EDIT_KEY_BACKSPACE);
            break;

        case KEY_ENTER:
            edit_handle_key(EDIT_KEY_ENTER);
            break;

        case KEY_TAB:
            edit_handle_key('\t');
            break;

        case KEY_CHAR:
            edit_handle_key((int)event->character);
            break;

        default:
            break;
    }
}

void edit_open(const char *name)
{
    /* Always start clean. */
    editor_clear_buffer();
    editor_suggest[0] = '\0';
    editor_suggest_len = 0;
    editor_dirty = 0;
    cursor_line = 0;
    cursor_col  = 0;
    scroll_line = 0;

    if (name && name[0]) {
        char blob[RAMDISK_DATA_MAX];
        int n = ramdisk_read(name, blob, RAMDISK_DATA_MAX);

        if (n >= 0) {
            /* File exists — load it. */
            int line = 0, col = 0;
            for (int i = 0; i < n && line < EDIT_MAX_LINES; i++) {
                if (blob[i] == '\n') { line++; col = 0; continue; }
                if (col < EDIT_LINE_LENGTH - 1)
                    editor_buffer[line][col++] = blob[i];
            }
        }
        /* else: new file, leave buffer empty. */

        int i = 0;
        while (name[i] && i < 31) { editor_filename[i] = name[i]; i++; }
        editor_filename[i] = '\0';
    } else {
        editor_filename[0] = '\0';
    }
}


/* --------------------------------------------------------- */
/* Launch                                                    */
/* --------------------------------------------------------- */

void launch_edit(void)
{
    cursor_line = 0;
    cursor_col  = 0;
    scroll_line = 0;

    editor_active = 1;

    keyboard_set_handler(edit_keyboard_handler);

    framebuffer_clear(EDIT_BG);

    editor_suggest[0] = '\0';
    editor_suggest_len = 0;

    editor_redraw();
}