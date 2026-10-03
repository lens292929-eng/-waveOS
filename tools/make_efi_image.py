import os
import struct
import fs
from pyfatfs.PyFat import PyFat


IMG_PATH     = "build/waveOS.img"
EFI_PATH     = "build/BOOTX64.EFI"
KERNEL_PATH  = "build/KERNEL.BIN"

SECTOR       = 512

# Geometry
FAT_START    = 2048         # 1 MiB in, aligned
FAT_SECTORS  = (64 * 1024 * 1024) // SECTOR   # 64 MiB FAT32
WFS_START    = FAT_START + FAT_SECTORS
WFS_SECTORS  = (2 * 1024 * 1024) // SECTOR    # 2 MiB wFs
TOTAL_SECTORS = WFS_START + WFS_SECTORS

IMG_BYTES    = TOTAL_SECTORS * SECTOR


def make_mbr():
    """Build a 512-byte MBR with two primary partitions."""
    mbr = bytearray(SECTOR)

    # --- Partition 1: FAT32, type 0x0C (FAT32 LBA) ---
    p1 = bytearray(16)
    p1[0]  = 0x00                              # not bootable
    p1[1:4] = b'\x00\x00\x00'                  # CHS first (unused)
    p1[4]  = 0x0C                              # FAT32 LBA
    p1[5:8] = b'\x00\x00\x00'                  # CHS last (unused)
    struct.pack_into('<I', p1, 8, FAT_START)
    struct.pack_into('<I', p1, 12, FAT_SECTORS)
    mbr[446:462] = p1

    # --- Partition 2: wFs, type 0x7F (arbitrary / unknown) ---
    p2 = bytearray(16)
    p2[0]  = 0x00
    p2[1:4] = b'\x00\x00\x00'
    p2[4]  = 0x7F
    p2[5:8] = b'\x00\x00\x00'
    struct.pack_into('<I', p2, 8, WFS_START)
    struct.pack_into('<I', p2, 12, WFS_SECTORS)
    mbr[462:478] = p2

    # --- Signature ---
    mbr[510] = 0x55
    mbr[511] = 0xAA

    return bytes(mbr)


def main():
    print("[1/5] Allocating image...")
    os.makedirs("build", exist_ok=True)

    with open(IMG_PATH, "wb") as f:
        f.truncate(IMG_BYTES)

    print("[2/5] Writing MBR...")
    with open(IMG_PATH, "r+b") as f:
        f.seek(0)
        f.write(make_mbr())

    print("[3/5] Formatting FAT32 in partition 1...")

    # PyFat wants to write the whole image. We give it a *slice* by
    # making a temporary file that is just the FAT32 partition, then
    # copying it back at FAT_START.
    tmp = "build/_fat32.tmp"
    with open(tmp, "wb") as f:
        f.truncate(FAT_SECTORS * SECTOR)

    fat = PyFat()
    fat.mkfs(tmp, fat_type=32)
    fat.close()

    print("[4/5] Copying files into FAT32...")

    with fs.open_fs(f"fat://{tmp}") as fat_fs:
        fat_fs.makedirs("/EFI/BOOT", recreate=True)

        with open(EFI_PATH, "rb") as f:
            fat_fs.writebytes("/EFI/BOOT/BOOTX64.EFI", f.read())

        with open(KERNEL_PATH, "rb") as f:
            fat_fs.writebytes("/EFI/BOOT/KERNEL.BIN", f.read())

    # Splice the FAT32 partition into the image.
    with open(tmp, "rb") as src, open(IMG_PATH, "r+b") as dst:
        dst.seek(FAT_START * SECTOR)
        while True:
            chunk = src.read(1 << 20)
            if not chunk:
                break
            dst.write(chunk)

    os.remove(tmp)

    print("[5/5] Zero-filling wFs partition...")
    # The image is already zero from truncate, but be explicit.
    with open(IMG_PATH, "r+b") as f:
        f.seek(WFS_START * SECTOR)
        f.write(b"\x00" * SECTOR)   # sector 0 of wFs: no magic yet

    print()
    print("========================================")
    print("        EFI IMAGE CREATED")
    print("========================================")
    print()
    print("  " + IMG_PATH)
    print(f"  FAT32: LBA {FAT_START} .. {FAT_START + FAT_SECTORS - 1}")
    print(f"  wFs  : LBA {WFS_START} .. {WFS_START + WFS_SECTORS - 1}")


if __name__ == "__main__":
    main()