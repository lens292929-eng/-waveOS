#include "framebuffer.h"
#include "colors.h"
#include "keyboard.h"
#include "mouse.h"
#include "cursor.h"
#include "idt.h"
#include "io.h"
#include "pit.h"
#include "shell.h"
#include "ata.h"
#include "wfs.h"
#include "desktop.h"
#include "fat32.h"
#include "fatimg.h"
#include "font_ttf.h"
#include "types.h"
#include "thread.h"


static void show_welcome(void)
{
    print_set_colors(COLOR_LIGHT_CYAN, COLOR_WAVE_BG);
    print(
        "\n"
        "==========================================\n"
        " waveOS\n"
        "==========================================\n"
        "\n");
    print(
        "Welcome to waveOS! waveOS is a 64-bit Operating System "
        "made by lens24!\n"
        "\n");
}

static void show_logs(void)
{
    print_tagged("OK", COLOR_LIGHT_GREEN, "Kernel Reached\n");
}

__attribute__((ms_abi))
void kmain(BootInfo *boot_info)
{
    framebuffer_init(boot_info);
    framebuffer_clear(COLOR_WAVE_BG);

    show_welcome();

    print_set_colors(COLOR_WHITE, COLOR_WAVE_BG);

    show_logs();

    idt_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized IDT\n");
    idt_install_handlers();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Installed IDT Handlers\n");

    pic_remap();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Remapped PIC\n");

    outb(0x21, 0xF8);   /* now: IRQ0 + IRQ1 + IRQ2 */
    outb(0xA1, 0xEF);   /* now: IRQ12 */
    print_tagged("OK", COLOR_LIGHT_GREEN, "Unmasked IRQ0, IRQ1, IRQ2, IRQ12\n");

    pit_init(100);
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized PIT\n");

    keyboard_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized Keyboard\n");

    mouse_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized Mouse\n");

    ata_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized ATA\n");

    u8 probe[512];
    int probe_result = ata_read_sector_drive(1, 0, probe);

    print("[probe] drive 1 read: ");
    print_u64((u64)probe_result);
    print("\n");

    print("[probe] first bytes: ");
    for (int i = 0; i < 8; i++) {
        u8 b = probe[i];
        printc("0123456789ABCDEF"[(b >> 4) & 0xF]);
        printc("0123456789ABCDEF"[b & 0xF]);
        printc(' ');
    }
    printc('\n');

    if (fat32_init(1) == 0) {
        print_tagged("OK", COLOR_LIGHT_GREEN, "Mounted FAT32 (data disk)\n");
    } else {
        print_tagged("!!", COLOR_LIGHT_RED, "FAT32 mount failed\n");
    }

    wfs_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized wFs\n");

    wfs_load();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Loaded wFs\n");

    idt_activate();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Activated IDT\n");

    thread_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized threads\n");

    __asm__ volatile ("sti");
    print_tagged("OK", COLOR_LIGHT_GREEN, "STI!\n");

    // desktop_draw();

    shell_init();
    
    shell_prompt();

    for (;;) {
            if (shell_wants_desktop()) {
                shell_set_desktop_active(1);
                desktop_enter();
                shell_set_desktop_active(0);
                framebuffer_clear(COLOR_WAVE_BG);
                shell_prompt();
            }
        __asm__ volatile ("hlt");
    }
}