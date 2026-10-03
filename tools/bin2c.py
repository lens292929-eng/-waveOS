from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent

font_path = PROJECT_DIR / "fonts" / "seabios_8x16.bin"
output_path = PROJECT_DIR / "seabios_font.h"

data = font_path.read_bytes()

if len(data) != 4096:
    raise SystemExit(f"Expected 4096 bytes, got {len(data)}")

with output_path.open("w") as f:
    f.write("#ifndef SEABIOS_FONT_H\n")
    f.write("#define SEABIOS_FONT_H\n\n")
    f.write("static const unsigned char seabios_font[4096] = {\n")

    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        f.write("    ")
        f.write(", ".join(f"0x{x:02X}" for x in chunk))
        f.write(",\n")

    f.write("};\n\n")
    f.write("#endif\n")

print(f"Generated: {output_path}")