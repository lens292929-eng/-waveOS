#include "framebuffer.h"
#include "shell.h"
#include "types.h"
#include "pit.h"
#include "edit.h"
#include "ramdisk.h"
#include "keyboard.h"
#include "io.h"
#include "ata.h"
#include "wfs.h"
#include "desktop.h"
#include "cursor.h"
#include "keyboard.h"
/* ---------- cwd ---------- */

#define CWD_MAX 128
static char cwd[CWD_MAX] = "/";

#define SHELL_LINE_MAX 256

static char shell_line[SHELL_LINE_MAX];
static int shell_line_len = 0;

static int shell_ui_taken_over = 0;


static void print_uptime(int argc, char **argv);
static void cmd_beep(int argc, char **argv);

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int strlen_simple(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* join cwd + name into out, normalising slashes. */
static void path_join(const char *name, char *out, int max) {
    if (name[0] == '/') {
        str_copy(out, name, max);
        return;
    }

    int n = strlen_simple(cwd);
    int i = 0;
    while (i < n && i < max - 1) { out[i] = cwd[i]; i++; }
    if (i > 0 && out[i - 1] != '/' && i < max - 1) out[i++] = '/';
    int j = 0;
    while (name[j] && i < max - 1) out[i++] = name[j++];
    out[i] = '\0';
}

/* Normalise a path: collapse "//", resolve "." and "..". */
static void path_normalise(const char *in, char *out, int max) {
    char tmp[CWD_MAX];
    str_copy(tmp, in, CWD_MAX);

    char *parts[32];
    int   nparts = 0;

    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';

        if (streq(start, ".")) continue;
        if (streq(start, "..")) { if (nparts > 0) nparts--; continue; }
        if (nparts < 32) parts[nparts++] = start;
    }

    int i = 0;
    if (i < max - 1) out[i++] = '/';
    for (int k = 0; k < nparts; k++) {
        const char *s = parts[k];
        while (*s && i < max - 1) out[i++] = *s++;
        if (k + 1 < nparts && i < max - 1) out[i++] = '/';
    }
    out[i] = '\0';
}

/* --- ramdisk directory helpers ------------------------------- */

static int dir_exists(const char *path) {
    char p[CWD_MAX];
    path_normalise(path, p, CWD_MAX);

    int n = strlen_simple(p);
    if (n == 0) return 0;
    if (n == 1 && p[0] == '/') return 1;

    char probe[CWD_MAX + 2];
    str_copy(probe, p, CWD_MAX);
    if (probe[n - 1] != '/') { probe[n] = '/'; probe[n + 1] = '\0'; }

    return ramdisk_exists(probe);
}

static int make_dir(const char *path) {
    char p[CWD_MAX];
    path_normalise(path, p, CWD_MAX);

    int n = strlen_simple(p);
    if (n == 1 && p[0] == '/') return 0;

    char probe[CWD_MAX + 2];
    str_copy(probe, p, CWD_MAX);
    if (probe[n - 1] != '/') { probe[n] = '/'; probe[n + 1] = '\0'; }

    if (ramdisk_exists(probe)) return -1;
    return ramdisk_write(probe, "", 0) < 0 ? -1 : 0;
}

/* ---------- tree command ------------------------------------ */

static char tree_scan_dir[CWD_MAX];
static int  tree_scan_depth;

static int tree_is_direct_child(const char *name, const char *dir,
                                const char **child_out, int *is_dir_out)
{
    int dir_len = strlen_simple(dir);
    int name_len = strlen_simple(name);

    if (name_len == 0)
        return 0;

    /* Root directory */
    if (streq(dir, "/")) {
        if (name[0] != '/')
            return 0;

        const char *rel = name + 1;
        if (*rel == '\0')
            return 0;

        int slash_count = 0;
        for (int i = 0; rel[i]; i++) {
            if (rel[i] == '/')
                slash_count++;
        }

        /*
         * A directory itself ends in '/', so:
         * /foo/       -> direct child
         * /foo/bar    -> not direct
         * /foo/bar/   -> not direct
         */
        if (slash_count > 1)
            return 0;

        *child_out = rel;
        *is_dir_out = (rel[strlen_simple(rel) - 1] == '/');
        return 1;
    }

    /* Non-root directory */
    if (dir_len <= 1)
        return 0;

    if (name_len <= dir_len)
        return 0;

    for (int i = 0; i < dir_len; i++) {
        if (name[i] != dir[i])
            return 0;
    }

    if (name[dir_len] != '/')
        return 0;

    const char *rel = name + dir_len + 1;

    if (*rel == '\0')
        return 0;

    int slash_count = 0;
    for (int i = 0; rel[i]; i++) {
        if (rel[i] == '/')
            slash_count++;
    }

    if (slash_count > 1)
        return 0;

    *child_out = rel;
    *is_dir_out = (rel[strlen_simple(rel) - 1] == '/');

    return 1;
}


void shell_cursor_blink(void)
{
    cursor_tick();
}

static void tree_print_indent(int depth)
{
    for (int i = 0; i < depth; i++)
        print("    ");
}

static void tree_scan_callback(const char *name, int size)
{
    const char *child;
    int is_dir;

    if (!tree_is_direct_child(name, tree_scan_dir,
                              &child, &is_dir))
        return;

    int child_len = strlen_simple(child);

    /*
     * Save the current scan state because tree_scan_callback()
     * may recursively scan another directory.
     */
    char saved_dir[CWD_MAX];
    str_copy(saved_dir, tree_scan_dir, CWD_MAX);
    int saved_depth = tree_scan_depth;

    tree_print_indent(saved_depth);

    if (is_dir) {
        print("|-- ");
        print_set_colors(0x0050A0FF, 0x00000000);

        for (int i = 0; i < child_len - 1; i++)
            printc(child[i]);

        print_set_colors(0x00FFFFFF, 0x00000000);
        print("/\n");

        /* Build the full path of the directory. */
        char next_dir[CWD_MAX];

        if (streq(tree_scan_dir, "/")) {
            next_dir[0] = '/';
            int j = 0;

            while (child[j] && j < CWD_MAX - 2) {
                next_dir[j + 1] = child[j];
                j++;
            }

            next_dir[j + 1] = '\0';
        } else {
            path_join(child, next_dir, CWD_MAX);
        }

        path_normalise(next_dir, next_dir, CWD_MAX);

        str_copy(tree_scan_dir, next_dir, CWD_MAX);
        tree_scan_depth = saved_depth + 1;

        ramdisk_list(tree_scan_callback);

        /* Restore state for the caller. */
        str_copy(tree_scan_dir, saved_dir, CWD_MAX);
        tree_scan_depth = saved_depth;
    } else {
        print("|-- ");
        print_set_colors(0x00FFFFFF, 0x00000000);
        print(child);

        print_set_colors(0x00808080, 0x00000000);
        print(" (");
        print_u64((u64)size);
        print(" bytes)");
        print_set_colors(0x00FFFFFF, 0x00000000);
        printc('\n');
    }
}

void shell_keyboard_handler(const keyboard_event_t *event)
{
    if (!event->pressed)
        return;

    /*
     * Ctrl+Q
     */
    if (event->ctrl && event->key == KEY_CHAR) {
        if (event->character == 'Q' ||
            event->character == 'q') {

            shell_cancel_funny();
            return;
        }
    }

    /*
     * Enter
     */
    if (event->key == KEY_ENTER) {
        printc('\n');

        shell_line[shell_line_len] = '\0';

        shell_submit(shell_line);

        shell_line_len = 0;

        return;
    }

    /*
     * Backspace
     */
    if (event->key == KEY_BACKSPACE) {
        if (shell_line_len > 0) {
            shell_line_len--;

            printc('\b');
        }

        return;
    }

    /*
     * Only accept printable characters.
     */
    if (event->key != KEY_CHAR)
        return;

    if (shell_line_len >= SHELL_LINE_MAX - 1)
        return;

    shell_line[shell_line_len++] = event->character;

    printc(event->character);
}

void shell_init(void)
{
    shell_line_len = 0;

    pit_set_cursor_callback(shell_cursor_blink);

    keyboard_set_handler(shell_keyboard_handler);
}

void shell_shutdown(void)
{
    /*
     * Only remove our handler if we're currently registered.
     */
    if (keyboard_get_handler() == shell_keyboard_handler)
        keyboard_set_handler(0);
}

static void cmd_tree(int argc, char **argv)
{
    char target[CWD_MAX];

    if (argc >= 2) {
        char joined[CWD_MAX];

        path_join(argv[1], joined, CWD_MAX);
        path_normalise(joined, target, CWD_MAX);

        if (!dir_exists(target)) {
            print("tree: no such directory: ");
            print(argv[1]);
            printc('\n');
            return;
        }
    } else {
        str_copy(target, cwd, CWD_MAX);
    }

    print_set_colors(0x0050A0FF, 0x00000000);
    print(target);
    print_set_colors(0x00FFFFFF, 0x00000000);
    printc('\n');

    str_copy(tree_scan_dir, target, CWD_MAX);
    tree_scan_depth = 0;

    ramdisk_list(tree_scan_callback);

    print_set_colors(0x00FFFFFF, 0x00000000);
}

static void ls_print(const char *name, int size)
{
    const char *rel = name;
    int cwd_len = strlen_simple(cwd);

    if (cwd_len > 1) {
        for (int i = 0; i < cwd_len; i++)
            if (name[i] != cwd[i]) return;
        if (name[cwd_len] != '/') return;
        rel = name + cwd_len + 1;
    } else {
        if (name[0] == '/') rel = name + 1;
        else return;
    }

    int rlen = strlen_simple(rel);
    if (rlen == 0) return;

    int is_dir = (rel[rlen - 1] == '/');

    int last_ok = is_dir ? rlen - 1 : rlen;
    for (int i = 0; i < last_ok; i++)
        if (rel[i] == '/') return;

    if (is_dir) {
        print_set_colors(0x0050A0FF, 0x00000000);
        for (int i = 0; i < rlen - 1; i++)
            printc(rel[i]);
        printc('/');
        printc('\n');
        print_set_colors(0x00FFFFFF, 0x00000000);
    } else {
        print_set_colors(0x00FFFFFF, 0x00000000);
        print(rel);

        int pad = 24 - rlen;
        for (int i = 0; i < pad; i++)
            printc(' ');

        print_set_colors(0x00808080, 0x00000000);
        print_u64((u64)size);
        print(" bytes");
        printc('\n');

        print_set_colors(0x00FFFFFF, 0x00000000);
    }
}

/* --------------------------------------------------------- */
/* WSS — wave shell script                                   */
/*                                                           */
/*   print("Hello World!\n");                                */
/*                                                           */
/* Supported escapes: \n \t \\ \"                            */
/* --------------------------------------------------------- */

static void wss_skip_ws(const char *src, int n, int *i)
{
    while (*i < n && (src[*i] == ' ' || src[*i] == '\t' ||
                      src[*i] == '\n' || src[*i] == '\r'))
        (*i)++;
}

static int wss_match_word(const char *src, int n, int *i, const char *word)
{
    int k = 0;
    while (word[k]) {
        if (*i + k >= n || src[*i + k] != word[k])
            return 0;
        k++;
    }
    *i += k;
    return 1;
}

static void wss_run(const char *path)
{
    char blob[RAMDISK_DATA_MAX];
    int n = ramdisk_read(path, blob, RAMDISK_DATA_MAX);
    if (n < 0) {
        print("wss: not found: ");
        print(path);
        printc('\n');
        return;
    }

    int i = 0;
    while (i < n) {
        wss_skip_ws(blob, n, &i);
        if (i >= n) break;

        /* comment */
        if (blob[i] == '#') {
            while (i < n && blob[i] != '\n') i++;
            continue;
        }

        /* ---- print("...") ---- */
        if (wss_match_word(blob, n, &i, "print")) {
            wss_skip_ws(blob, n, &i);

            if (i < n && blob[i] == '(') {
                i++;
                wss_skip_ws(blob, n, &i);

                if (i < n && blob[i] == '"') {
                    i++;
                    while (i < n && blob[i] != '"') {
                        if (blob[i] == '\\' && i + 1 < n) {
                            i++;
                            switch (blob[i]) {
                                case 'n':  printc('\n'); break;
                                case 't':  printc('\t'); break;
                                case '\\': printc('\\'); break;
                                case '"':  printc('"');  break;
                                default:   printc(blob[i]); break;
                            }
                        } else {
                            printc(blob[i]);
                        }
                        i++;
                    }
                    if (i < n && blob[i] == '"') i++;
                }
            }
        }

        /* ---- pause();  |  pause(>nul); ---- */
        else if (wss_match_word(blob, n, &i, "pause")) {
            wss_skip_ws(blob, n, &i);

            int silent = 0;

            if (i < n && blob[i] == '(') {
                i++;
                wss_skip_ws(blob, n, &i);

                /* recognize `>nul` as the silent flag */
                if (wss_match_word(blob, n, &i, ">nul")) {
                    silent = 1;
                    wss_skip_ws(blob, n, &i);
                }

                if (i < n && blob[i] == ')') i++;
            }

            if (!silent)
            print("Press Enter to continue...");

            keyboard_pause_begin();                /* <-- proves begin() ran */
            keyboard_pause_wait();               /* <-- proves wait() returned */
            keyboard_pause_end();

            if (!silent)
                printc('\n');
        }

        /* skip to end of statement (or line) */
        while (i < n && blob[i] != ';' && blob[i] != '\n') i++;
        if (i < n) i++;
    }
}

static void cmd_wss(int argc, char **argv)
{
    if (argc < 2) {
        print("usage: wss <script.wss>\n");
        return;
    }

    char joined[CWD_MAX];
    path_join(argv[1], joined, CWD_MAX);

    char normal[CWD_MAX];
    path_normalise(joined, normal, CWD_MAX);

    wss_run(normal);
}

/* --- commands ------------------------------------------------ */

static void cmd_help(int argc, char **argv);

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    framebuffer_clear(0x00000000);
}

static volatile int funny_cancel = 0;

void shell_cancel_funny(void)
{
    funny_cancel = 1;
}

static void cmd_funny(int argc, char **argv)
{
    if (argc >= 2 && streq(argv[1], "--i-am-absolutely-sure-i-wanna-run-this")) {
        print("ok, it's your funeral.\n");
        print("press Ctrl+Q to cancel. you have 5 seconds.\n");

        funny_cancel = 0;

        for (int i = 5; i > 0; i--) {
            print("commencing in ");
            print_u64((u64)i);
            print("...\n");

            for (int t = 0; t < 20; t++) {
                if (funny_cancel) {
                    print("cancelled. coward.\n");
                    return;
                }
                pit_sleep_ms(50);
            }
        }

        if (funny_cancel) {
            print("cancelled. coward.\n");
            return;
        }

        print("too late.\n");

        for (;;) {
            __asm__ volatile ("nop\n");
            print("nop");
        }
    }

    print("ARE YOU ABSOLUTELY SURE YOU WANT TO RUN THIS?\n");
    print("THIS WILL SHIT YOUR COMPUTER!\n");
    print("if you're absolutely sure, run:\n");
    print("funny --i-am-absolutely-sure-i-wanna-run-this\n");
    print("you have been warned\n");
}

static void cmd_echo(int argc, char **argv)
{
    int redir = -1;
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], ">")) {
            redir = i;
            break;
        }
    }

    char buf[RAMDISK_DATA_MAX];
    int  n = 0;

    int end = (redir >= 0) ? redir : argc;
    for (int i = 1; i < end; i++) {
        for (const char *p = argv[i]; *p && n < RAMDISK_DATA_MAX - 1; p++)
            buf[n++] = *p;
        if (i + 1 < end && n < RAMDISK_DATA_MAX - 1)
            buf[n++] = ' ';
    }
    if (n < RAMDISK_DATA_MAX - 1)
        buf[n++] = '\n';

    if (redir < 0) {
        for (int i = 0; i < n; i++)
            printc(buf[i]);
        return;
    }

    if (redir + 1 >= argc) {
        print("echo: expected filename after >\n");
        return;
    }

    char joined[CWD_MAX];
    path_join(argv[redir + 1], joined, CWD_MAX);

    char normal[CWD_MAX];
    path_normalise(joined, normal, CWD_MAX);

    if (ramdisk_write(normal, buf, n) < 0)
        print("echo: write failed\n");
}

static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    print("waveOS 0.2.1 kernel version 0.6.1\n");
}

static void cmd_beep(int argc, char **argv)
{
    (void)argc; (void)argv;

    u32 div = 1193182 / 1000;

    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));

    u8 tmp = inb(0x61);
    if ((tmp & 3) != 3) outb(0x61, tmp | 3);

    pit_sleep_ms(200);

    outb(0x61, inb(0x61) & 0xFC);
}

static void cmd_about(int argc, char **argv) {
    (void)argc; (void)argv;
    print("##:::::'##::::'###::::'##::::'##:'########::'#######:::'######::\n");
    print(" ##:'##: ##:::'## ##::: ##:::: ##: ##.....::'##.... ##:'##... ##:\n");
    print(" ##: ##: ##::'##:. ##:: ##:::: ##: ##::::::: ##:::: ##: ##:::..::\n");
    print(" ##: ##: ##:'##:::. ##: ##:::: ##: ######::: ##:::: ##:. ######::\n");
    print(" ##: ##: ##::#########:. ##:: ##:: ##...:::: ##:::: ##::..... ##:\n");
    print(" ##: ##: ##::##.... ##::. ## ##::: ##::::::: ##:::: ##:'##::: ##:\n");
    print(". ###. ###:: ##:::: ##:::. ###:::: ########:. #######::. ######::\n");
    print(":...::...:::..:::::..:::::...:::::........:::.......::::......:::\n");
    print("\n");
    print("waveOS - a 64-bit hobby operating system\n");
    print("written from scratch by lens24.\n");
    print("\n");
    print("Boots via UEFI, has a framebuffer, IDT, PS/2 keyboard,\n");
    print("and this shell. No userspace yet. Pizza included.\n");
}

static void cmd_pizza(int argc, char **argv) {
    (void)argc; (void)argv;
    print("            88                                 \n");
    print("            \"\"                                 \n");
    print("                                                \n");
    print("8b,dPPYba,  88 888888888 888888888 ,adPPYYba,  \n");
    print("88P'    \"8a 88      a8P\"      a8P\" \"\"     `Y8  \n");
    print("88       d8 88   ,d8P'     ,d8P'   ,adPPPPP88  \n");
    print("88b,   ,a8\" 88 ,d8\"      ,d8\"      88,    ,88  \n");
    print("88`YbbdP\"'  88 888888888 888888888 `\"8bbdP\"Y8  \n");
    print("88                                              \n");
    print("88         \n");
}

static void cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    ramdisk_list(ls_print);
}

static void cmd_cd(int argc, char **argv) {
    if (argc < 2) {
        print(cwd);
        printc('\n');
        return;
    }

    char target[CWD_MAX];
    if (argv[1][0] == '/')
        path_normalise(argv[1], target, CWD_MAX);
    else {
        char joined[CWD_MAX];
        path_join(argv[1], joined, CWD_MAX);
        path_normalise(joined, target, CWD_MAX);
    }

    if (!dir_exists(target)) {
        print("cd: no such directory: ");
        print(argv[1]);
        printc('\n');
        return;
    }

    str_copy(cwd, target, CWD_MAX);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { print("usage: mkdir <name>\n"); return; }

    char joined[CWD_MAX];
    path_join(argv[1], joined, CWD_MAX);

    if (dir_exists(joined)) {
        print("mkdir: already exists: ");
        print(argv[1]);
        printc('\n');
        return;
    }

    if (make_dir(joined) < 0) {
        print("mkdir: failed\n");
        return;
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { print("usage: rm <name>\n"); return; }

    char joined[CWD_MAX];
    path_join(argv[1], joined, CWD_MAX);

    if (ramdisk_delete(joined) < 0) {
        char probe[CWD_MAX + 2];
        str_copy(probe, joined, CWD_MAX);
        int n = strlen_simple(probe);
        if (n > 0 && probe[n - 1] != '/') { probe[n] = '/'; probe[n + 1] = '\0'; }
        if (ramdisk_delete(probe) < 0) {
            print("rm: not found: ");
            print(argv[1]);
            printc('\n');
        }
    }
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { print("usage: cat <name>\n"); return; }

    char joined[CWD_MAX];
    path_join(argv[1], joined, CWD_MAX);

    char buf[RAMDISK_DATA_MAX];
    int n = ramdisk_read(joined, buf, RAMDISK_DATA_MAX);
    if (n < 0) {
        print("cat: not found: ");
        print(argv[1]);
        printc('\n');
        return;
    }

    for (int i = 0; i < n; i++)
        printc(buf[i]);
    if (n == 0 || buf[n - 1] != '\n')
        printc('\n');
}

static void cmd_edit(int argc, char **argv)
{
    char joined[CWD_MAX];
    char normal[CWD_MAX];

    if (argc >= 2) {
        path_join(argv[1], joined, CWD_MAX);
        path_normalise(joined, normal, CWD_MAX);
    } else {
        normal[0] = '\0';
    }

    shell_ui_taken_over = 1;

    edit_open(normal);
    launch_edit();
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    print(cwd);
    printc('\n');
}

static void cmd_waveey(int argc, char **argv) {
    (void)argc; (void)argv;
    print("waveey\n");
}

static void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    wfs_save();
    print("Bye.\n");
    outw(0x604, 0x2000);
}

static void cmd_save(int argc, char **argv) {
    (void)argc; (void)argv;
    wfs_save();
}

static u64 read_tsc(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static void cmd_nopometer(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* How long to measure, in milliseconds. */
    u64 duration_ms = 1000;
    if (argc >= 2) {
        /* optional: parse first arg as seconds */
        u64 secs = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9')
            secs = secs * 10 + (u64)(*p++ - '0');
        if (secs > 0 && secs <= 10)
            duration_ms = secs * 1000;
    }

    print("nopometer: spinning for ");
    print_u64(duration_ms);
    print(" ms...\n");

    /*
     * We can't use rdtsc to derive wall-clock time — TSC frequency
     * is CPU-specific. Instead, we use the PIT as the clock and rdtsc
     * as the fast counter. But actually, we don't need wall time at
     * all: we just count nops until the PIT has ticked long enough.
     *
     * Simpler: spin until pit_get_uptime_seconds advances, counting nops.
     */

    u64 start_secs = pit_get_uptime_seconds();
    u64 target_secs = start_secs + (duration_ms / 1000);

    /* nops counter — volatile so the compiler doesn't optimize the loop away */
    volatile u64 nops = 0;

    while (pit_get_uptime_seconds() < target_secs) {
        /* Inner loop of 1000 nops. Unrolled-ish via pragma-free repetition. */
        __asm__ volatile (
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            "nop\nnop\nnop\nnop\nnop\n"
            ::: "memory"
        );
        nops += 50;
    }

    print("nops executed: ");
    print_u64(nops);
    print("\n");

    u64 elapsed_ms = (pit_get_uptime_seconds() - start_secs) * 1000;

    print("elapsed: ");
    print_u64(elapsed_ms);
    print(" ms\n");

    /* nops per second */
    u64 nps = nops;
    if (elapsed_ms > 0)
        nps = (nops * 1000) / elapsed_ms;

    print("≈ ");
    print_u64(nps);
    print(" nops/sec\n");
}

static void cmd_desktop(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    shell_ui_taken_over = 1;

    desktop_enter();
}

/* ---------- command table ---------- */

struct command {
    const char *name;
    void (*fn)(int argc, char **argv);
    const char *help;
};

static const struct command commands[] = {

    { "about",    cmd_about,     "about this os" },
    { "cat",      cmd_cat,       "print file contents" },
    { "cd",       cmd_cd,        "change directory" },
    { "clear",    cmd_clear,     "clear screen" },
    { "desktop",  cmd_desktop,   "show the desktop" },
    { "echo",     cmd_echo,      "echo arguments" },
    { "edit",     cmd_edit,      "simple text editor" },
    { "help",     cmd_help,      "this list" },
    { "ls",       cmd_ls,        "list files" },
    { "mkdir",    cmd_mkdir,     "create directory" },
    { "nopometer",cmd_nopometer, "measure nop throughput" },
    { "pizza",    cmd_pizza,     "do not run" },
    { "pwd",      cmd_pwd,       "print working directory" },
    { "rm",       cmd_rm,        "remove file or empty dir" },
    { "save",     cmd_save,      "flush ramdisk to disk" },
    { "shutdown", cmd_shutdown,  "shuts down computer (QEMU-only compatibility, will add ACPI later..)" },
    { "tree",     cmd_tree,      "show directory tree" },
    { "uptime",   print_uptime,  "shows how long the computer has been running for" },
    { "version",  cmd_version,   "kernel version" },
    { "waveey",   cmd_waveey,    "waveey" },
    { "wss",      cmd_wss,       "run a .wss script" },

    { NULL, NULL, NULL },
};

/* Not shown in help. */
static const struct command hidden_commands[] = {
    { "beep", cmd_beep, "" },
    { "funny",   cmd_funny,   "run this at your own risk"},
    { NULL, NULL, NULL },
};

/* ---------- help ---------- */

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    print("commands:\n");
    for (int i = 0; commands[i].name; i++) {
        print("  ");
        print(commands[i].name);
        for (int n = 10 - strlen_simple(commands[i].name); n > 0; n--)
            printc(' ');
        print("- ");
        print(commands[i].help);
        printc('\n');
    }
}

/* ---------- prompt ---------- */

void shell_prompt(void) {
    print_set_colors(0x00A0A0A0, 0x00000000);
    print("wave");
    print_set_colors(0x00606060, 0x00000000);
    print("@");

    print_set_colors(0x00FFD070, 0x00000000);
    print(cwd);

    print_set_colors(0x00A0FFA0, 0x00000000);
    print("> ");
    print_set_colors(0x00FFFFFF, 0x00000000);
}

/* ---------- tokenizer ---------- */

#define ARGV_MAX 16

static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc >= ARGV_MAX) break;

        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ---------- uptime ---------- */

static void print_uptime(int argc, char **argv) {
    (void)argc; (void)argv;

    u64 total = pit_get_uptime_seconds();
    u64 hours   = total / 3600;
    u64 minutes = (total % 3600) / 60;
    u64 seconds = total % 60;

    print("Uptime: ");
    if (hours   < 10) printc('0'); print_u64(hours);
    printc(':');
    if (minutes < 10) printc('0'); print_u64(minutes);
    printc(':');
    if (seconds < 10) printc('0'); print_u64(seconds);
    printc('\n');
}

/* ---------- entry point ---------- */

void shell_submit(char *line) {
    char *argv[ARGV_MAX];
    int argc = tokenize(line, argv);

    if (argc == 0) {
        shell_prompt();
        return;
    }

    for (int i = 0; commands[i].name; i++) {
        if (streq(argv[0], commands[i].name)) {
            commands[i].fn(argc, argv);

            if (!shell_ui_taken_over)
                shell_prompt();

            shell_ui_taken_over = 0;

            return;
        }
    }

    for (int i = 0; hidden_commands[i].name; i++) {
        if (streq(argv[0], hidden_commands[i].name)) {
            hidden_commands[i].fn(argc, argv);
            shell_prompt();
            return;
        }
    }

        {
        int len = strlen_simple(argv[0]);
        if (len > 4 &&
            argv[0][len - 4] == '.' &&
            argv[0][len - 3] == 'w' &&
            argv[0][len - 2] == 's' &&
            argv[0][len - 1] == 's') {

            /* Strip leading .\ or ./ */
            const char *name = argv[0];
            if (name[0] == '.' && (name[1] == '\\' || name[1] == '/'))
                name += 2;

            char joined[CWD_MAX];
            path_join(name, joined, CWD_MAX);

            char normal[CWD_MAX];
            path_normalise(joined, normal, CWD_MAX);

            if (ramdisk_exists(normal)) {
                wss_run(normal);
                shell_prompt();
                return;
            }
        }
    }

    print("unknown command: ");
    print(argv[0]);
    printc('\n');
    shell_prompt();
}

/* shell.c */
static int desktop_mode = 0;

void shell_set_desktop_mode(int on) { desktop_mode = on; }
int  shell_in_desktop_mode(void)    { return desktop_mode; }