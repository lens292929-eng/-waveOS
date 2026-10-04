// ============================================================
// waveOS UEFI bootloader — waveFirmware edition
// x86-64 / UEFI
// ============================================================

#include "../kernel/seabios_font.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;
typedef u64                usize;

typedef void *EFI_HANDLE;
typedef u64   EFI_STATUS;

#define EFI_SUCCESS            0
#define EFI_BUFFER_TOO_SMALL   0x8000000000000005ULL
#define EFI_LOADER_DATA        4

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_ADDRESS   2

#define EFIAPI __attribute__((ms_abi))

// ============================================================
// EFI structures
// ============================================================

typedef struct {
    u32 Data1;
    u16 Data2;
    u16 Data3;
    u8  Data4[8];
} EFI_GUID;

typedef struct {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 CRC32;
    u32 Reserved;
} EFI_TABLE_HEADER;

// ============================================================
// UEFI input
// ============================================================

typedef void *EFI_EVENT;

typedef struct {
    u16 ScanCode;
    u16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
    void *This,
    EFI_INPUT_KEY *Key
);

typedef struct {
    EFI_EVENT WaitForKey;
    EFI_INPUT_READ_KEY ReadKeyStroke;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

// ============================================================
// File protocol
// ============================================================

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL *This,
    EFI_FILE_PROTOCOL **NewHandle,
    u16 *FileName,
    u64 OpenMode,
    u64 Attributes
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(
    EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    EFI_FILE_PROTOCOL *This,
    usize *BufferSize,
    void *Buffer
);

struct EFI_FILE_PROTOCOL {
    u64 Revision;
    EFI_FILE_OPEN  Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ  Read;
    void *Write;
    void *GetPosition;
    void *SetPosition;
    void *GetInfo;
    void *SetInfo;
    void *Flush;
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
};

typedef struct {
    u64 Revision;

    EFI_STATUS (EFIAPI *OpenVolume)(
        void *This,
        EFI_FILE_PROTOCOL **Root
    );
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

// ============================================================
// GOP
// ============================================================

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat;
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    u32 MaxMode;
    u32 Mode;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    usize SizeOfInfo;

    u64 FrameBufferBase;
    usize FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(
        struct EFI_GRAPHICS_OUTPUT_PROTOCOL *,
        u32,
        usize *,
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **
    );

    EFI_STATUS (EFIAPI *SetMode)(
        struct EFI_GRAPHICS_OUTPUT_PROTOCOL *,
        u32
    );

    void *Blt;

    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// ============================================================
// Boot services
// ============================================================

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    u32 Type,
    u32 MemoryType,
    usize Pages,
    u64 *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol,
    void *Registration,
    void **Interface
);

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    usize *MemoryMapSize,
    void *MemoryMap,
    usize *MapKey,
    usize *DescriptorSize,
    u32 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    usize MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_STALL)(
    usize Microseconds
);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    void *This,
    u16 *String
);

typedef struct {
    EFI_TEXT_STRING OutputString;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    void *RaiseTPL;
    void *RestoreTPL;

    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;

    EFI_GET_MEMORY_MAP GetMemoryMap;

    void *AllocatePool;
    void *FreePool;

    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;

    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;

    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    void *GetNextMonotonicCount;

    EFI_STALL Stall;

    void *SetWatchdogTimer;

    void *ConnectController;
    void *DisconnectController;

    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;

    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;

    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

// ============================================================
// System table
// ============================================================

typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;

    u16 *FirmwareVendor;
    u32 FirmwareRevision;

    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;

    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;

    EFI_HANDLE StandardErrorHandle;
    void *StdErr;

    void *RuntimeServices;

    EFI_BOOT_SERVICES *BootServices;

    usize NumberOfTableEntries;
    void *ConfigurationTable;
};

// ============================================================
// GUIDs
// ============================================================

static EFI_GUID GOP_GUID = {
    0x9042a9de,
    0x23dc,
    0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

static EFI_GUID SIMPLE_FS_GUID = {
    0x964e5b22,
    0x6459,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

// ============================================================
// BootInfo passed to kernel
// ============================================================

typedef struct {
    u64 framebuffer;

    u32 width;
    u32 height;
    u32 pitch;

    u32 pad;

    u64 memory_map;
    usize memory_map_size;
    usize memory_descriptor_size;
} BootInfo;

static BootInfo boot_info;

// ============================================================
// Helpers
// ============================================================

void *memcpy(void *dest, const void *src, usize n)
{
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;

    while (n--)
        *d++ = *s++;

    return dest;
}

void *memset(void *dest, int c, usize n)
{
    u8 *d = (u8 *)dest;

    while (n--)
        *d++ = (u8)c;

    return dest;
}

// ============================================================
// Early UEFI console
// ============================================================

static void console(
    EFI_SYSTEM_TABLE *ST,
    const char *s
)
{
    u16 buffer[256];
    usize i = 0;

    while (s[i] && i < 255) {
        buffer[i] = (u16)(u8)s[i];
        i++;
    }

    buffer[i] = 0;

    ST->ConOut->OutputString(
        ST->ConOut,
        buffer
    );
}

// ============================================================
// GOP framebuffer console
// ============================================================

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

static u32 *fb;

static u32 fb_width;
static u32 fb_height;
static u32 fb_pitch;

static u32 cursor_x;
static u32 cursor_y;

#define FONT_WIDTH       8
#define FONT_HEIGHT      16
#define FONT_GAP         1
#define LINE_GAP         2

#define MARGIN_X         16
#define MARGIN_Y         16

#define FG_COLOR         0x00FFFFFF
#define BG_COLOR         0x00000000

static void draw_pixel(
    u32 x,
    u32 y,
    u32 color
)
{
    if (x >= fb_width || y >= fb_height)
        return;

    fb[y * fb_pitch + x] = color;
}

static void draw_char(
    u32 x,
    u32 y,
    u8 c,
    u32 color
)
{
    const u8 *glyph =
        &seabios_font[(u32)c * FONT_HEIGHT];

    for (u32 row = 0; row < FONT_HEIGHT; row++) {

        u8 bits = glyph[row];

        for (u32 col = 0; col < FONT_WIDTH; col++) {

            if (bits & (0x80 >> col)) {

                draw_pixel(
                    x + col,
                    y + row,
                    color
                );
            }
        }
    }
}

static void fb_clear(u32 color)
{
    for (u32 y = 0; y < fb_height; y++) {

        u32 *row =
            fb + y * fb_pitch;

        for (u32 x = 0; x < fb_width; x++)
            row[x] = color;
    }
}

static void scroll(void)
{
    u32 line_h =
        FONT_HEIGHT + LINE_GAP;

    if (cursor_y + line_h <
        fb_height - MARGIN_Y)
        return;

    for (
        u32 y = MARGIN_Y;
        y + line_h < fb_height;
        y++
    ) {

        u32 *dst =
            fb + y * fb_pitch;

        u32 *src =
            fb + (y + line_h) * fb_pitch;

        for (u32 x = 0; x < fb_width; x++)
            dst[x] = src[x];
    }

    u32 start =
        (fb_height > line_h)
        ? fb_height - line_h
        : 0;

    for (
        u32 y = start;
        y < fb_height;
        y++
    ) {

        u32 *row =
            fb + y * fb_pitch;

        for (u32 x = 0; x < fb_width; x++)
            row[x] = BG_COLOR;
    }

    if (cursor_y >= line_h)
        cursor_y -= line_h;
}

static void newline(void)
{
    cursor_x = MARGIN_X;

    cursor_y +=
        FONT_HEIGHT + LINE_GAP;

    scroll();
}

static void putc(char c)
{
    if (c == '\r') {
        cursor_x = MARGIN_X;
        return;
    }

    if (c == '\n') {
        newline();
        return;
    }

    if (
        cursor_x +
        FONT_WIDTH +
        FONT_GAP >=
        fb_width - MARGIN_X
    ) {
        newline();
    }

    draw_char(
        cursor_x,
        cursor_y,
        (u8)c,
        FG_COLOR
    );

    cursor_x +=
        FONT_WIDTH + FONT_GAP;
}

static void boot_log(const char *s)
{
    while (*s)
        putc(*s++);
}

static void refresh_framebuffer(void)
{
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return;

    fb =
        (u32 *)(usize)
        gop->Mode->FrameBufferBase;

    fb_width =
        gop->Mode->Info->HorizontalResolution;

    fb_height =
        gop->Mode->Info->VerticalResolution;

    fb_pitch =
        gop->Mode->Info->PixelsPerScanLine;

    cursor_x = MARGIN_X;
    cursor_y = MARGIN_Y;
}

// ============================================================
// Input
// ============================================================
#define SCAN_UP       0x0001
#define SCAN_DOWN     0x0002
#define SCAN_RIGHT    0x0003
#define SCAN_LEFT     0x0004

#define SCAN_F6       0x0010
#define SCAN_F8       0x0012
#define SCAN_ESC      0x0017

static int read_key(
    EFI_SYSTEM_TABLE *ST,
    EFI_INPUT_KEY *key
)
{
    EFI_STATUS status;

    status = ST->ConIn->ReadKeyStroke(
        ST->ConIn,
        key
    );

    return status == EFI_SUCCESS;
}

// ============================================================
// Integer output
// ============================================================

static void boot_print_u32(u32 value)
{
    char buffer[16];
    int count = 0;

    if (value == 0) {
        putc('0');
        return;
    }

    while (value > 0) {

        buffer[count++] =
            (char)('0' + (value % 10));

        value /= 10;
    }

    while (count > 0)
        putc(buffer[--count]);
}

static void resolution_switcher(
    EFI_SYSTEM_TABLE *ST
)
{
    if (!gop || !gop->Mode)
        return;

    u32 selected = gop->Mode->Mode;

    for (;;) {

        fb_clear(BG_COLOR);

        cursor_x = MARGIN_X;
        cursor_y = MARGIN_Y;

        boot_log("waveFirmware Resolution Switcher\r\n");
        boot_log("==================================\r\n");
        boot_log("\r\n");

        boot_log("Select a display mode:\r\n");
        boot_log("\r\n");

        u32 max_display = gop->Mode->MaxMode;

        u32 first_mode = 0;

        if (selected >= 8)
            first_mode = selected - 7;

        u32 last_mode = first_mode + 8;

        if (last_mode > max_display)
            last_mode = max_display;

        for (u32 mode = first_mode;
             mode < last_mode;
             mode++) {

            usize info_size = 0;

            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;

            EFI_STATUS status =
                gop->QueryMode(
                    gop,
                    mode,
                    &info_size,
                    &info
                );

            if (status != EFI_SUCCESS || !info)
                continue;

            if (mode == selected)
                boot_log("> ");
            else
                boot_log("  ");

            boot_print_u32(info->HorizontalResolution);

            boot_log(" x ");

            boot_print_u32(info->VerticalResolution);

            boot_log("  [mode ");

            boot_print_u32(mode);

            boot_log("]\r\n");
        }

        boot_log("\r\n");
        boot_log("UP/DOWN: Select\r\n");
        boot_log("ENTER:   Apply\r\n");
        boot_log("ESC:     Cancel\r\n");

        for (;;) {

            EFI_INPUT_KEY key;

            if (!read_key(ST, &key)) {
                ST->BootServices->Stall(10000);
                continue;
            }

            if (key.ScanCode == SCAN_UP) {

                if (selected > 0)
                    selected--;

                break;
            }

            if (key.ScanCode == SCAN_DOWN) {

                if (
                    selected + 1 <
                    gop->Mode->MaxMode
                ) {
                    selected++;
                }

                break;
            }

            if (key.ScanCode == SCAN_ESC)
                return;

            if (key.UnicodeChar == '\r') {

                EFI_STATUS status =
                    gop->SetMode(
                        gop,
                        selected
                    );

                if (status == EFI_SUCCESS) {

                    refresh_framebuffer();

                    fb_clear(BG_COLOR);

                    cursor_x = MARGIN_X;
                    cursor_y = MARGIN_Y;

                    boot_log(
                        "Resolution changed successfully.\r\n"
                    );

                    boot_log("New resolution: ");

                    boot_print_u32(fb_width);

                    boot_log(" x ");

                    boot_print_u32(fb_height);

                    boot_log("\r\n\r\n");

                    boot_log(
                        "Press ENTER to return.\r\n"
                    );

                    for (;;) {

                        EFI_INPUT_KEY wait_key;

                        if (!read_key(ST, &wait_key)) {
                            ST->BootServices->Stall(10000);
                            continue;
                        }

                        if (
                            wait_key.UnicodeChar == '\r' ||
                            wait_key.ScanCode == SCAN_ESC
                        ) {
                            return;
                        }
                    }
                }

                fb_clear(BG_COLOR);

                cursor_x = MARGIN_X;
                cursor_y = MARGIN_Y;

                boot_log(
                    "ERROR: Could not change resolution.\r\n"
                );

                boot_log("\r\n");
                boot_log("Press ENTER to return.\r\n");

                for (;;) {

                    EFI_INPUT_KEY wait_key;

                    if (!read_key(ST, &wait_key)) {
                        ST->BootServices->Stall(10000);
                        continue;
                    }

                    if (
                        wait_key.UnicodeChar == '\r' ||
                        wait_key.ScanCode == SCAN_ESC
                    ) {
                        return;
                    }
                }
            }
        }
    }
}

static void firmware_options(
    EFI_SYSTEM_TABLE *ST
)
{
    int selected = 0;

    for (;;) {

        fb_clear(BG_COLOR);

        cursor_x = MARGIN_X;
        cursor_y = MARGIN_Y;

        boot_log("waveFirmware Options\r\n");
        boot_log("====================\r\n");
        boot_log("\r\n");

        if (selected == 0)
            boot_log("> ");
        else
            boot_log("  ");

        boot_log("Boot waveOS\r\n");

        if (selected == 1)
            boot_log("> ");
        else
            boot_log("  ");

        boot_log("System Information\r\n");

        if (selected == 2)
            boot_log("> ");
        else
            boot_log("  ");

        boot_log("Display Information\r\n");

        if (selected == 3)
            boot_log("> ");
        else
            boot_log("  ");

        boot_log("Resolution Switcher\r\n");

        if (selected == 4)
            boot_log("> ");
        else
            boot_log("  ");

        boot_log("Boot Options\r\n");

        boot_log("\r\n");
        boot_log("----------------------------------------\r\n");
        boot_log("\r\n");

        boot_log("waveFirmware 0.1\r\n");

        boot_log("Current display: ");

        boot_print_u32(fb_width);

        boot_log(" x ");

        boot_print_u32(fb_height);

        boot_log("\r\n");

        boot_log("\r\n");
        boot_log("UP/DOWN: Navigate\r\n");
        boot_log("ENTER:   Select\r\n");
        boot_log("ESC:     Return\r\n");

        for (;;) {

            EFI_INPUT_KEY key;

            if (!read_key(ST, &key)) {
                ST->BootServices->Stall(10000);
                continue;
            }

            if (key.ScanCode == SCAN_UP) {

                if (selected > 0)
                    selected--;

                break;
            }

            if (key.ScanCode == SCAN_DOWN) {

                if (selected < 4)
                    selected++;

                break;
            }

            if (key.ScanCode == SCAN_ESC)
                return;

            if (key.UnicodeChar == '\r') {

                if (selected == 0)
                    return;

                if (selected == 1) {

                    fb_clear(BG_COLOR);

                    cursor_x = MARGIN_X;
                    cursor_y = MARGIN_Y;

                    boot_log("System Information\r\n");
                    boot_log("==================\r\n");
                    boot_log("\r\n");

                    boot_log("Firmware: waveFirmware 0.1\r\n");
                    boot_log("Architecture: x86-64\r\n");
                    boot_log("Boot mode: UEFI\r\n");

                    boot_log("\r\n");
                    boot_log("Press ENTER or ESC to return.\r\n");

                    for (;;) {

                        EFI_INPUT_KEY info_key;

                        if (!read_key(ST, &info_key)) {
                            ST->BootServices->Stall(10000);
                            continue;
                        }

                        if (
                            info_key.UnicodeChar == '\r' ||
                            info_key.ScanCode == SCAN_ESC
                        ) {
                            break;
                        }
                    }

                    break;
                }

                if (selected == 2) {

                    fb_clear(BG_COLOR);

                    cursor_x = MARGIN_X;
                    cursor_y = MARGIN_Y;

                    boot_log("Display Information\r\n");
                    boot_log("===================\r\n");
                    boot_log("\r\n");

                    boot_log("Resolution: ");
                    boot_print_u32(fb_width);
                    boot_log(" x ");
                    boot_print_u32(fb_height);
                    boot_log("\r\n");

                    boot_log("Pitch: ");
                    boot_print_u32(fb_pitch);
                    boot_log("\r\n");

                    boot_log("GOP modes: ");
                    boot_print_u32(gop->Mode->MaxMode);
                    boot_log("\r\n");

                    boot_log("\r\n");
                    boot_log("Press ENTER or ESC to return.\r\n");

                    for (;;) {

                        EFI_INPUT_KEY display_key;

                        if (!read_key(ST, &display_key)) {
                            ST->BootServices->Stall(10000);
                            continue;
                        }

                        if (
                            display_key.UnicodeChar == '\r' ||
                            display_key.ScanCode == SCAN_ESC
                        ) {
                            break;
                        }
                    }

                    break;
                }

                if (selected == 3) {

                    resolution_switcher(ST);

                    break;
                }

                if (selected == 4) {

                    fb_clear(BG_COLOR);

                    cursor_x = MARGIN_X;
                    cursor_y = MARGIN_Y;

                    boot_log("Boot Options\r\n");
                    boot_log("============\r\n");
                    boot_log("\r\n");

                    boot_log("Boot target: waveOS\r\n");
                    boot_log("Kernel: \\EFI\\BOOT\\KERNEL.BIN\r\n");
                    boot_log("Kernel address: 0x100000\r\n");

                    boot_log("\r\n");
                    boot_log("Press ENTER or ESC to return.\r\n");

                    for (;;) {

                        EFI_INPUT_KEY boot_key;

                        if (!read_key(ST, &boot_key)) {
                            ST->BootServices->Stall(10000);
                            continue;
                        }

                        if (
                            boot_key.UnicodeChar == '\r' ||
                            boot_key.ScanCode == SCAN_ESC
                        ) {
                            break;
                        }
                    }

                    break;
                }
            }
        }
    }
}

// ============================================================
// Boot countdown
// ============================================================

static int firmware_countdown(
    EFI_SYSTEM_TABLE *ST
)
{
    for (int seconds = 5;
         seconds > 0;
         seconds--) {

        fb_clear(BG_COLOR);

        cursor_x = MARGIN_X;
        cursor_y = MARGIN_Y;

        boot_log("waveOS\r\n");
        boot_log("====================\r\n");
        boot_log("\r\n");

        boot_log("Starting waveOS...\r\n");
        boot_log("\r\n");

        boot_log("Starting in ");

        putc((char)('0' + seconds));

        boot_log(" second");

        if (seconds != 1)
            putc('s');

        boot_log("\r\n");
        boot_log("\r\n");

        boot_log("Press F8 for waveFirmware Options\r\n");
        boot_log("Press F6 to switch resolution\r\n");

        for (int i = 0; i < 100; i++) {

            EFI_INPUT_KEY key;

            if (read_key(ST, &key)) {

                if (key.ScanCode == SCAN_F8) {

                    firmware_options(ST);

                    return 1;
                }

                if (key.ScanCode == SCAN_F6) {

                    resolution_switcher(ST);

                    break;
                }

                if (
                    key.ScanCode == 0x12 &&
                    key.UnicodeChar == 0
                ) {

                    firmware_options(ST);
                    return 1;
                }
            }

            ST->BootServices->Stall(10000);
        }
    }

    return 0;
}

// ============================================================
// Entry
// ============================================================

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *ST
)
{
    EFI_BOOT_SERVICES *BS =
        ST->BootServices;

    console(
        ST,
        "waveOS UEFI bootloader\r\n"
    );

    // --------------------------------------------------------
    // 1. GOP
    // --------------------------------------------------------

    EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP = 0;

    EFI_STATUS status =
        BS->LocateProtocol(
            &GOP_GUID,
            0,
            (void **)&GOP
        );

    if (
        status != EFI_SUCCESS ||
        !GOP ||
        !GOP->Mode ||
        !GOP->Mode->Info
    ) {

        console(
            ST,
            "ERROR: GOP unavailable\r\n"
        );

        return status;
    }

    gop = GOP;

    fb =
        (u32 *)(usize)
        GOP->Mode->FrameBufferBase;

    fb_width =
        GOP->Mode->Info->HorizontalResolution;

    fb_height =
        GOP->Mode->Info->VerticalResolution;

    fb_pitch =
        GOP->Mode->Info->PixelsPerScanLine;

    cursor_x = MARGIN_X;
    cursor_y = MARGIN_Y;

    fb_clear(BG_COLOR);

    // --------------------------------------------------------
    // waveFirmware startup screen
    // --------------------------------------------------------

    int firmware_menu_used =
        firmware_countdown(ST);

    (void)firmware_menu_used;

    fb_clear(BG_COLOR);

    cursor_x = MARGIN_X;
    cursor_y = MARGIN_Y;

    boot_log("waveOS UEFI BOOTLOADER\r\n");
    boot_log("----------------------------------------\r\n");

    // --------------------------------------------------------
    // 2. Filesystem
    // --------------------------------------------------------

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FS = 0;

    status =
        BS->LocateProtocol(
            &SIMPLE_FS_GUID,
            0,
            (void **)&FS
        );

    if (
        status != EFI_SUCCESS ||
        !FS
    ) {

        boot_log(
            "ERROR: FILESYSTEM UNAVAILABLE\r\n"
        );

        return status;
    }

    EFI_FILE_PROTOCOL *Root = 0;

    status =
        FS->OpenVolume(
            FS,
            &Root
        );

    if (
        status != EFI_SUCCESS ||
        !Root
    ) {

        boot_log(
            "ERROR: OPENVOLUME FAILED\r\n"
        );

        return status;
    }

    EFI_FILE_PROTOCOL *KernelFile = 0;

    u16 kernel_path[] = {
        '\\',
        'E','F','I',
        '\\',
        'B','O','O','T',
        '\\',
        'K','E','R','N','E','L',
        '.',
        'B','I','N',
        0
    };

    status =
        Root->Open(
            Root,
            &KernelFile,
            kernel_path,
            1,
            0
        );

    if (
        status != EFI_SUCCESS ||
        !KernelFile
    ) {

        boot_log(
            "ERROR: KERNEL.BIN NOT FOUND\r\n"
        );

        return status;
    }

    // --------------------------------------------------------
    // 3. Allocate kernel at fixed address
    // --------------------------------------------------------

    #define KERNEL_BASE  0x100000ULL
    #define KERNEL_PAGES 512

    u64 kernel_address =
        KERNEL_BASE;

    status =
        BS->AllocatePages(
            EFI_ALLOCATE_ADDRESS,
            EFI_LOADER_DATA,
            KERNEL_PAGES,
            &kernel_address
        );

    if (status != EFI_SUCCESS) {

        boot_log(
            "ERROR: KERNEL MEMORY ALLOCATION "
            "FAILED (0x100000 busy)\r\n"
        );

        return status;
    }

    // --------------------------------------------------------
    // 4. Load kernel.bin
    // --------------------------------------------------------

    usize kernel_size =
        KERNEL_PAGES * 4096;

    status =
        KernelFile->Read(
            KernelFile,
            &kernel_size,
            (void *)(usize)kernel_address
        );

    KernelFile->Close(KernelFile);

    if (status != EFI_SUCCESS) {

        boot_log(
            "ERROR: KERNEL READ FAILED\r\n"
        );

        return status;
    }

    boot_log(
        "[ OK ] KERNEL LOADED AT 0x100000\r\n"
    );

    u8 *kbase =
        (u8 *)(usize)kernel_address;

    for (
        usize i = kernel_size;
        i < KERNEL_PAGES * 4096;
        i++
    ) {
        kbase[i] = 0;
    }

    // --------------------------------------------------------
    // 5. Allocate memory map buffer
    // --------------------------------------------------------

    usize map_size = 0;
    usize map_key = 0;
    usize desc_size = 0;
    u32 desc_ver = 0;

    status =
        BS->GetMemoryMap(
            &map_size,
            0,
            &map_key,
            &desc_size,
            &desc_ver
        );

    if (status != EFI_BUFFER_TOO_SMALL) {

        boot_log(
            "ERROR: MEMORY MAP QUERY FAILED\r\n"
        );

        return status;
    }

    map_size += desc_size * 8;

    u64 map_address = 0;

    status =
        BS->AllocatePages(
            EFI_ALLOCATE_ANY_PAGES,
            EFI_LOADER_DATA,
            (map_size + 4095) / 4096,
            &map_address
        );

    if (status != EFI_SUCCESS) {

        boot_log(
            "ERROR: MEMORY MAP BUFFER "
            "ALLOC FAILED\r\n"
        );

        return status;
    }

    // --------------------------------------------------------
    // 6. Get FINAL memory map
    // --------------------------------------------------------

    status =
        BS->GetMemoryMap(
            &map_size,
            (void *)(usize)map_address,
            &map_key,
            &desc_size,
            &desc_ver
        );

    if (status != EFI_SUCCESS) {

        boot_log(
            "ERROR: MEMORY MAP ACQUISITION "
            "FAILED\r\n"
        );

        return status;
    }

    // --------------------------------------------------------
    // 7. Fill BootInfo
    // --------------------------------------------------------

    boot_info.framebuffer = (u64)(usize)fb;
    boot_info.width       = fb_width;
    boot_info.height      = fb_height;
    boot_info.pitch       = fb_pitch;
    boot_info.pad         = 0;

    boot_info.memory_map             = map_address;
    boot_info.memory_map_size        = map_size;
    boot_info.memory_descriptor_size = desc_size;

    // --------------------------------------------------------
    // 8. Exit boot services
    // --------------------------------------------------------

    status =
        BS->ExitBootServices(
            ImageHandle,
            map_key
        );

    if (status == 0x8000000000000002ULL) {
        /*
         * EFI_INVALID_PARAMETER — the map changed.
         * Re-fetch and retry.
         */

        map_size =
            ((map_size + 4095) / 4096) * 4096;

        status =
            BS->GetMemoryMap(
                &map_size,
                (void *)(usize)map_address,
                &map_key,
                &desc_size,
                &desc_ver
            );

        if (status != EFI_SUCCESS) {

            return status;
        }

        boot_info.memory_map             = map_address;
        boot_info.memory_map_size        = map_size;
        boot_info.memory_descriptor_size = desc_size;

        status =
            BS->ExitBootServices(
                ImageHandle,
                map_key
            );
    }

    if (status != EFI_SUCCESS) {

        return status;
    }

    // --------------------------------------------------------
    // 9. Jump to kernel
    // --------------------------------------------------------

    typedef void (EFIAPI *KernelEntry)(
        BootInfo *
    );

    KernelEntry kernel =
        (KernelEntry)(usize)kernel_address;

    kernel(&boot_info);

    for (;;)
        __asm__ volatile ("hlt");
}