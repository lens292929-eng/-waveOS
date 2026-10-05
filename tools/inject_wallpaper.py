# tools/inject_wallpaper.py
import os, struct
import fs
from pyfatfs.PyFat import PyFat

IMG = "../build/data.img"
WALL = "wallpaper.bmp"

with fs.open_fs(f"fat://{IMG}") as fat:
    with open(WALL, "rb") as f:
        fat.writebytes("/system/WALLPAPER.BMP", f.read())

print("done")