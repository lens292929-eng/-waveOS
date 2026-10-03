#!/usr/bin/env python3
import sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
img = Image.open(src).convert("RGBA")
w, h = img.size
px = img.load()

with open(dst, "w") as f:
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"#define LOGO_W {w}\n")
    f.write(f"#define LOGO_H {h}\n\n")
    f.write(f"static const uint32_t logo[{w * h}] = {{\n")
    for y in range(h):
        f.write("    ")
        for x in range(w):
            r, g, b, a = px[x, y]
            # Pack RGBA: 0xAARRGGBB, so alpha lives in the top byte
            f.write(f"0x{a:02X}{r:02X}{g:02X}{b:02X},")
        f.write("\n")
    f.write("};\n")