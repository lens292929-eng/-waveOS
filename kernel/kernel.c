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

static void show_welcome(void)
{
    print_set_colors(COLOR_LIGHT_CYAN, COLOR_BLACK);
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
    framebuffer_clear(COLOR_BLACK);

    show_welcome();

    print_set_colors(COLOR_WHITE, COLOR_BLACK);

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

    wfs_init();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Initialized wFs\n");

    wfs_load();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Loaded wFs\n");

    idt_activate();
    print_tagged("OK", COLOR_LIGHT_GREEN, "Activated IDT\n");

    __asm__ volatile ("sti");
    print_tagged("OK", COLOR_LIGHT_GREEN, "STI!\n");

    // desktop_draw();

    shell_init();
    
    shell_prompt();

    for (;;) {
        cursor_tick();
        __asm__ volatile ("hlt");
    }
}