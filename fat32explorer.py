#!/usr/bin/env python3
"""
fat32explorer — a Windows-Explorer-style GUI for FAT32 disk images.

Reads and writes the FAT32 volume directly, byte by byte.
No pyfatfs dependency. Full control over the on-disk layout.

Features:
  * Real directory navigation
  * Long filename (LFN) support, read
  * New file, new folder
  * Import from host / export to host
  * Rename files
  * Delete files
  * Drag-and-drop import (requires tkinterdnd2)
  * Preview / edit pane for text files
  * Status bar with free space

Requires:
    pip install tkinterdnd2       (optional, for drag-and-drop)

Run:
    python fat32explorer.py
"""

import os
import struct
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog


# =========================================================
# Optional drag-and-drop
# =========================================================

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
    HAVE_DND = True
except ImportError:
    HAVE_DND = False
    DND_FILES = None
    TkinterDnD = None


# =========================================================
# Constants
# =========================================================

SECTOR = 512

ATTR_READ_ONLY = 0x01
ATTR_HIDDEN    = 0x02
ATTR_SYSTEM    = 0x04
ATTR_VOLUME_ID = 0x08
ATTR_DIRECTORY = 0x10
ATTR_ARCHIVE   = 0x20
ATTR_LFN       = 0x0F


# =========================================================
# FAT32 filesystem — direct byte access
# =========================================================

class Fat32Image:
    """
    A FAT32 volume backed by a single file (the .img).

    Everything is done by reading/writing raw sectors.
    No third-party filesystem library.
    """

    def __init__(self, path=None):
        self.path = path
        self.f = None

        # BPB-derived geometry
        self.bytes_per_sector       = 0
        self.sectors_per_cluster    = 0
        self.reserved_sector_count  = 0
        self.num_fats               = 0
        self.fat_size_32            = 0
        self.root_cluster           = 0

        self.fat_lba                = 0
        self.data_lba               = 0
        self.cluster_size           = 0
        self.total_clusters         = 0

    # ---------- open / close ----------

    def open(self, path=None):
        if path is not None:
            self.path = path

        if self.f is not None:
            self.f.close()

        self.f = open(self.path, "r+b")

        # Read the boot sector
        self.f.seek(0)
        bpb = self.f.read(SECTOR)

        if len(bpb) < SECTOR:
            raise ValueError("disk too small")

        self.bytes_per_sector      = struct.unpack_from("<H", bpb, 11)[0]
        self.sectors_per_cluster   = bpb[13]
        self.reserved_sector_count = struct.unpack_from("<H", bpb, 14)[0]
        self.num_fats              = bpb[16]
        self.fat_size_32           = struct.unpack_from("<I", bpb, 36)[0]
        self.root_cluster          = struct.unpack_from("<I", bpb, 44)[0]

        if self.bytes_per_sector != 512:
            raise ValueError(f"unsupported sector size {self.bytes_per_sector}")
        if self.sectors_per_cluster == 0:
            raise ValueError("invalid sectors_per_cluster")
        if self.fat_size_32 == 0:
            raise ValueError("not a FAT32 volume (fat_size_32 is 0)")

        self.fat_lba      = self.reserved_sector_count
        self.data_lba     = self.reserved_sector_count + self.num_fats * self.fat_size_32
        self.cluster_size = self.sectors_per_cluster * 512

        # Data volume ends at EOF
        self.f.seek(0, 2)
        total_sectors    = self.f.tell() // SECTOR
        data_sectors     = total_sectors - self.data_lba
        self.total_clusters = data_sectors // self.sectors_per_cluster

    def close(self):
        if self.f:
            self.f.close()
            self.f = None

    # ---------- low-level I/O ----------

    def read_sector(self, lba):
        self.f.seek(lba * SECTOR)
        return self.f.read(SECTOR)

    def write_sector(self, lba, data):
        if len(data) != SECTOR:
            raise ValueError("sector write must be 512 bytes")
        self.f.seek(lba * SECTOR)
        self.f.write(data)

    def cluster_to_lba(self, cluster):
        return self.data_lba + (cluster - 2) * self.sectors_per_cluster

    def read_cluster(self, cluster):
        self.f.seek(self.cluster_to_lba(cluster) * SECTOR)
        return self.f.read(self.cluster_size)

    def write_cluster(self, cluster, data):
        if len(data) != self.cluster_size:
            data = data + b"\x00" * (self.cluster_size - len(data))
        self.f.seek(self.cluster_to_lba(cluster) * SECTOR)
        self.f.write(data)

    # ---------- FAT table ----------

    def fat_get(self, cluster):
        entry_off  = cluster * 4
        sector_off = entry_off // SECTOR
        in_sector  = entry_off % SECTOR

        sector = self.read_sector(self.fat_lba + sector_off)
        value  = struct.unpack_from("<I", sector, in_sector)[0]
        return value & 0x0FFFFFFF

    def fat_set(self, cluster, value):
        entry_off  = cluster * 4
        sector_off = entry_off // SECTOR
        in_sector  = entry_off % SECTOR

        # Update every FAT copy
        for f in range(self.num_fats):
            fat_lba = self.fat_lba + f * self.fat_size_32
            sector  = self.read_sector(fat_lba + sector_off)

            old    = struct.unpack_from("<I", sector, in_sector)[0]
            merged = (old & 0xF0000000) | (value & 0x0FFFFFFF)

            sector = bytearray(sector)
            struct.pack_into("<I", sector, in_sector, merged)

            self.write_sector(fat_lba + sector_off, bytes(sector))

    def alloc_cluster(self):
        for c in range(2, self.total_clusters + 2):
            if self.fat_get(c) == 0:
                self.fat_set(c, 0x0FFFFFFF)   # mark EOC to reserve it
                return c
        return 0

    def free_chain(self, first):
        c = first
        while 2 <= c < 0x0FFFFFF8:
            nxt = self.fat_get(c)
            self.fat_set(c, 0)
            c = nxt

    # ---------- LFN helpers ----------

    @staticmethod
    def lfn_checksum(name_8_3):
        """Compute the LFN checksum for an 8.3 name."""
        s = 0
        for b in name_8_3:
            s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
        return s

    @staticmethod
    def extract_lfn_chars(entry):
        """Pull the 13 UTF-16 chars out of an LFN directory entry."""
        raw = entry
        chars = []

        def u16(off):
            return raw[off] | (raw[off + 1] << 8)

        for off in (1, 3, 5, 7, 9):
            chars.append(u16(off))
        for off in (14, 16, 18, 20, 22, 24):
            chars.append(u16(off))
        for off in (28, 30):
            chars.append(u16(off))

        return chars

    # ---------- Directory iteration ----------

    def iterate_dir(self, cluster):
        """
        Yield (name, is_dir, first_cluster, size, entries_used).

        entries_used = number of 32-byte slots the entry occupies
                       (LFN entries + 1). Useful for delete.
        """
        lfn_chars = [0] * 260
        lfn_count = 0

        while 2 <= cluster < 0x0FFFFFF8:
            data = self.read_cluster(cluster)
            n    = self.cluster_size // 32

            for i in range(n):
                raw = data[i * 32 : i * 32 + 32]

                first = raw[0]

                if first == 0x00:
                    return
                if first == 0xE5:
                    lfn_count = 0
                    continue

                attr = raw[11]

                # LFN entry
                if attr == ATTR_LFN:
                    seq = first & 0x3F
                    if 1 <= seq <= 20:
                        chars = self.extract_lfn_chars(raw)
                        base  = (seq - 1) * 13
                        for k in range(13):
                            if base + k < 260:
                                lfn_chars[base + k] = chars[k]
                        if seq > lfn_count:
                            lfn_count = seq
                    continue

                if attr & ATTR_VOLUME_ID:
                    lfn_count = 0
                    continue

                # Real entry
                display = None

                if lfn_count > 0:
                    name_chars = []
                    for k in range(lfn_count * 13):
                        c = lfn_chars[k]
                        if c == 0:
                            break
                        name_chars.append(chr(c) if c < 128 else "?")
                    display = "".join(name_chars)
                else:
                    # Build from the 8.3 fields
                    base = raw[0:8].rstrip(b" ").decode("latin-1")
                    ext  = raw[8:11].rstrip(b" ").decode("latin-1")
                    display = base + ("." + ext if ext else "")

                first_cluster = struct.unpack_from("<H", raw, 26)[0] | \
                                (struct.unpack_from("<H", raw, 20)[0] << 16)
                size          = struct.unpack_from("<I", raw, 28)[0]
                is_dir        = 1 if (attr & ATTR_DIRECTORY) else 0

                yield (display, is_dir, first_cluster, size,
                       lfn_count + 1)

                lfn_count = 0

            cluster = self.fat_get(cluster)

    # ---------- Path resolution ----------

    @staticmethod
    def _eq(a, b):
        return a.lower() == b.lower()

    def resolve_dir(self, path):
        """Return the cluster of a directory path, or -1."""
        path = path.replace("\\", "/")
        if not path.startswith("/"):
            path = "/" + path

        if path in ("", "/"):
            return self.root_cluster

        cluster = self.root_cluster
        parts   = [p for p in path.strip("/").split("/") if p]

        for part in parts:
            found = False
            for name, is_dir, first, size, _ in self.iterate_dir(cluster):
                if is_dir and self._eq(name, part):
                    cluster = first
                    found = True
                    break
            if not found:
                return -1

        return cluster

    def find_entry(self, parent_cluster, name):
        """
        Return (cluster, is_dir, size, dir_sector, dir_offset,
                first_cluster, entries_used) for a named entry,
                or None.
        """
        lfn_chars = [0] * 260
        lfn_count = 0

        cluster = parent_cluster
        while 2 <= cluster < 0x0FFFFFF8:
            data = self.read_cluster(cluster)
            n    = self.cluster_size // 32

            for i in range(n):
                raw   = data[i * 32 : i * 32 + 32]
                first = raw[0]

                if first == 0x00:
                    return None
                if first == 0xE5:
                    lfn_count = 0
                    continue

                attr = raw[11]

                if attr == ATTR_LFN:
                    seq = first & 0x3F
                    if 1 <= seq <= 20:
                        chars = self.extract_lfn_chars(raw)
                        base  = (seq - 1) * 13
                        for k in range(13):
                            if base + k < 260:
                                lfn_chars[base + k] = chars[k]
                        if seq > lfn_count:
                            lfn_count = seq
                    continue

                if attr & ATTR_VOLUME_ID:
                    lfn_count = 0
                    continue

                display = None
                if lfn_count > 0:
                    name_chars = []
                    for k in range(lfn_count * 13):
                        c = lfn_chars[k]
                        if c == 0:
                            break
                        name_chars.append(chr(c) if c < 128 else "?")
                    display = "".join(name_chars)
                else:
                    base = raw[0:8].rstrip(b" ").decode("latin-1")
                    ext  = raw[8:11].rstrip(b" ").decode("latin-1")
                    display = base + ("." + ext if ext else "")

                if self._eq(display, name):
                    fc = struct.unpack_from("<H", raw, 26)[0] | \
                         (struct.unpack_from("<H", raw, 20)[0] << 16)
                    size = struct.unpack_from("<I", raw, 28)[0]

                    # Find the sector/offset of this 32-byte slot
                    byte_off_in_cluster = i * 32
                    sector_in_cluster   = byte_off_in_cluster // SECTOR
                    off_in_sector       = byte_off_in_cluster % SECTOR

                    dir_lba = self.cluster_to_lba(cluster) + sector_in_cluster

                    return {
                        "name":          display,
                        "is_dir":        bool(attr & ATTR_DIRECTORY),
                        "first_cluster": fc,
                        "size":          size,
                        "dir_lba":       dir_lba,
                        "dir_offset":    off_in_sector,
                        "entries_used":  lfn_count + 1,
                    }

                lfn_count = 0

            cluster = self.fat_get(cluster)

        return None

    # ---------- Read a file ----------

    def read_file(self, path):
        path = path.replace("\\", "/")
        if not path.startswith("/"):
            path = "/" + path

        parent_path = "/" + "/".join(path.strip("/").split("/")[:-1])
        name        = path.strip("/").split("/")[-1]

        if not name:
            return None

        parent_cluster = self.resolve_dir(parent_path)
        if parent_cluster < 0:
            return None

        entry = self.find_entry(parent_cluster, name)
        if entry is None or entry["is_dir"]:
            return None

        # Walk chain
        out     = bytearray()
        cluster = entry["first_cluster"]
        needed  = entry["size"]

        while 2 <= cluster < 0x0FFFFFF8 and len(out) < needed:
            data = self.read_cluster(cluster)
            take = min(len(data), needed - len(out))
            out.extend(data[:take])
            cluster = self.fat_get(cluster)

        return bytes(out)

    # ---------- Write primitives ----------

    @staticmethod
    def make_8_3(name):
        """Build an 11-byte 8.3 name from a filename."""
        upper = name.upper()

        if "." in upper:
            base, _, ext = upper.rpartition(".")
        else:
            base, ext = upper, ""

        base = base[:8].ljust(8)
        ext  = ext[:3].ljust(3)

        raw = (base + ext).encode("latin-1")
        return raw[:11].ljust(11, b" ")

    def alloc_dir_slot(self, dir_cluster, need_slots=1):
        """
        Find `need_slots` consecutive free 32-byte slots in a dir.
        Returns (lba, offset_bytes) of the first slot.
        Extends the directory by a cluster if necessary.
        """
        cluster = dir_cluster
        prev    = 0

        while True:
            lba = self.cluster_to_lba(cluster)
            for s in range(self.sectors_per_cluster):
                data = self.read_sector(lba + s)
                n    = SECTOR // 32

                run_start = None
                run_len   = 0

                for i in range(n):
                    first = data[i * 32]

                    if first == 0x00 or first == 0xE5:
                        if run_start is None:
                            run_start = i
                        run_len += 1
                        if run_len >= need_slots:
                            return (lba + s, run_start * 32)
                    else:
                        run_start = None
                        run_len   = 0

            next_c = self.fat_get(cluster)
            if not (2 <= next_c < 0x0FFFFFF8):
                # Extend by one cluster
                new_c = self.alloc_cluster()
                if new_c == 0:
                    return None

                self.write_cluster(new_c, b"\x00" * self.cluster_size)
                self.fat_set(cluster, new_c)
                prev = cluster
                cluster = new_c
                continue

            prev = cluster
            cluster = next_c

    def write_directory_entry(self, parent_cluster, filename,
                              first_cluster, size, is_dir,
                              lfn_entries=None):
        """
        Write a directory entry. lfn_entries is a list of 32-byte
        raw LFN entries to precede the 8.3 entry, or None.
        """
        name_8_3 = self.make_8_3(filename)

        n_slots = 1 + (len(lfn_entries) if lfn_entries else 0)

        slot = self.alloc_dir_slot(parent_cluster, n_slots)
        if slot is None:
            return False

        lba, offset = slot
        sector      = bytearray(self.read_sector(lba))

        # Write LFN entries (if any)
        if lfn_entries:
            for k, lfn in enumerate(lfn_entries):
                struct.pack_into("<32s", sector, offset + k * 32, lfn)

        # Write 8.3 entry
        entry_off = offset + (len(lfn_entries) if lfn_entries else 0) * 32

        raw = bytearray(32)
        raw[0:11]  = name_8_3
        raw[11]    = (ATTR_DIRECTORY if is_dir else ATTR_ARCHIVE)
        raw[12]    = 0
        # 13..19 times, zero
        # 20..21 first cluster high
        struct.pack_into("<H", raw, 20, (first_cluster >> 16) & 0xFFFF)
        # 22..25 write time/date
        # 26..27 first cluster low
        struct.pack_into("<H", raw, 26, first_cluster & 0xFFFF)
        # 28..31 size
        struct.pack_into("<I", raw, 28, size)

        struct.pack_into("<32s", sector, entry_off, bytes(raw))

        self.write_sector(lba, bytes(sector))
        return True

    @staticmethod
    def make_lfn_entries(name):
        """
        Build the LFN entries for a long filename.
        Returns (list_of_32_byte_entries, checksum_needed_later).
        """
        name_8_3 = Fat32Image.make_8_3(name)
        checksum = Fat32Image.lfn_checksum(name_8_3)

        # Encode as UTF-16LE, add a null terminator
        utf16 = name.encode("utf-16-le") + b"\x00\x00"

        # Pad to a multiple of 26 bytes (13 UTF-16 chars)
        while len(utf16) % 26 != 0:
            utf16 += b"\xFF\xFF"   # padding after the null terminator

        chunks = [utf16[i:i + 26] for i in range(0, len(utf16), 26)]
        n      = len(chunks)

        entries = []
        for seq_idx, chunk in enumerate(chunks):
            seq = seq_idx + 1
            raw = bytearray(32)

            # Byte 0: sequence, with 0x40 set on the last entry
            raw[0] = seq | (0x40 if seq == n else 0)

            # Bytes 1..10, 14..25, 28..31: UTF-16 chunk
            raw[1:11]  = chunk[0:10]
            raw[14:26] = chunk[10:22]
            raw[28:32] = chunk[22:26]

            raw[11] = ATTR_LFN
            raw[12] = 0
            raw[13] = checksum
            raw[26] = 0
            raw[27] = 0

            entries.append(bytes(raw))

        # Entries must be stored in *reverse* order (last seq first)
        entries.reverse()
        return entries

    # ---------- Write a file ----------

    def write_file(self, path, data):
        path = path.replace("\\", "/")
        if not path.startswith("/"):
            path = "/" + path

        parent_path = "/" + "/".join(path.strip("/").split("/")[:-1])
        name        = path.strip("/").split("/")[-1]

        if not name:
            return False

        parent_cluster = self.resolve_dir(parent_path)
        if parent_cluster < 0:
            return False

        # Delete existing entry with this name, if present
        existing = self.find_entry(parent_cluster, name)
        if existing:
            self._delete_entry(parent_cluster, existing)

        # Allocate a chain for the data
        first_cluster = 0
        prev_cluster  = 0
        remaining     = len(data)
        offset        = 0

        if remaining == 0:
            # Empty file: no clusters needed
            pass
        else:
            while remaining > 0:
                c = self.alloc_cluster()
                if c == 0:
                    return False

                if prev_cluster:
                    self.fat_set(prev_cluster, c)
                if first_cluster == 0:
                    first_cluster = c
                prev_cluster = c

                this = min(remaining, self.cluster_size)
                self.write_cluster(c, data[offset:offset + this])

                offset    += this
                remaining -= this

        # Build LFN entries if the name doesn't fit 8.3
        lfn_entries = None
        if self._needs_lfn(name):
            lfn_entries = self.make_lfn_entries(name)

        ok = self.write_directory_entry(
            parent_cluster, name, first_cluster, len(data),
            False, lfn_entries
        )
        return ok

    @staticmethod
    def _needs_lfn(name):
        """Does this name require long-filename entries?"""
        if name != name.upper():
            return True
        if " " in name:
            return True

        if "." in name:
            base, _, ext = name.rpartition(".")
            if len(base) > 8 or len(ext) > 3:
                return True
        else:
            if len(name) > 8:
                return True

        # Disallowed chars in 8.3
        for c in name:
            if c in '"*+/:;<=>?[\\]|,':
                return True

        return False

    def _delete_entry(self, parent_cluster, entry):
        """Mark a directory entry (and its LFN entries) as deleted."""
        dir_lba    = entry["dir_lba"]
        dir_offset = entry["dir_offset"]
        used       = entry["entries_used"]

        # The entry may span two sectors in unusual cases.
        # For a small LFN this won't happen, but be safe.

        # Read the sector(s)
        start_lba = dir_lba
        start_off = dir_offset

        # Number of bytes to mark = 32 * used
        remaining = 32 * used
        lba       = start_lba
        off       = start_off

        while remaining > 0:
            sector = bytearray(self.read_sector(lba))
            take   = min(remaining, SECTOR - off)

            for i in range(take):
                sector[off + i] = 0xE5

            self.write_sector(lba, bytes(sector))

            remaining -= take
            off = 0
            lba += 1

        # Free the cluster chain
        fc = entry["first_cluster"]
        if fc >= 2:
            self.free_chain(fc)

    def delete_file(self, path):
        path = path.replace("\\", "/")
        if not path.startswith("/"):
            path = "/" + path

        parent_path = "/" + "/".join(path.strip("/").split("/")[:-1])
        name        = path.strip("/").split("/")[-1]

        parent_cluster = self.resolve_dir(parent_path)
        if parent_cluster < 0:
            return False

        entry = self.find_entry(parent_cluster, name)
        if entry is None or entry["is_dir"]:
            return False

        self._delete_entry(parent_cluster, entry)
        return True

    # ---------- Free space ----------

    def free_bytes(self):
        free_clusters = 0
        for c in range(2, self.total_clusters + 2):
            if self.fat_get(c) == 0:
                free_clusters += 1
        return free_clusters * self.cluster_size

    def total_bytes(self):
        return self.total_clusters * self.cluster_size


# =========================================================
# Helpers
# =========================================================

def human_size(n):
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    if n < 1024 * 1024 * 1024:
        return f"{n / (1024 * 1024):.1f} MB"
    return f"{n / (1024 * 1024 * 1024):.1f} GB"


def file_type(name):
    lower = name.lower()

    if lower.endswith(".wss"):  return "WSS"
    if lower.endswith((".asm", ".s")):  return "ASM"
    if lower.endswith((".c", ".h")):    return "C"
    if lower.endswith((".cpp", ".hpp")):return "C++"
    if lower.endswith(".py"):   return "Python"
    if lower.endswith(".txt"):  return "TXT"
    if lower.endswith(".md"):   return "TXT"
    if lower.endswith(".bin"):  return "Binary"
    if "." in lower:            return lower.rsplit(".", 1)[1].upper()

    return "File"


# =========================================================
# GUI
# =========================================================

class App(TkinterDnD.Tk if HAVE_DND else tk.Tk):

    COLS = ("name", "type", "size")

    def __init__(self):
        super().__init__()

        self.title("FAT32 Explorer")
        self.geometry("1100x700")
        self.minsize(800, 500)

        self.img = None
        self.current_path = "/"
        self.current_index = None
        self.dirty = set()
        self.sort_col = "name"
        self.sort_rev = False

        self._build_menu()
        self._build_toolbar()
        self._build_body()
        self._build_statusbar()

        self.bind("<Control-o>", lambda e: self.open_image())
        self.bind("<Control-s>", lambda e: self.save_image())
        self.bind("<Control-n>", lambda e: self.new_file())
        self.bind("<Delete>",    lambda e: self.delete_selected())
        self.bind("<F5>",        lambda e: self.refresh())
        self.bind("<Alt-Up>",    lambda e: self.go_parent())

        if HAVE_DND:
            self.drop_target_register(DND_FILES)
            self.dnd_bind("<<Drop>>", self.on_drop)

    # ---------- menus ----------

    def _build_menu(self):
        menu = tk.Menu(self)

        fm = tk.Menu(menu, tearoff=0)
        fm.add_command(label="Open image…", accelerator="Ctrl+O",
                       command=self.open_image)
        fm.add_command(label="Save", accelerator="Ctrl+S",
                       command=self.save_image)
        fm.add_separator()
        fm.add_command(label="Exit", command=self.destroy)
        menu.add_cascade(label="File", menu=fm)

        em = tk.Menu(menu, tearoff=0)
        em.add_command(label="New file", accelerator="Ctrl+N",
                       command=self.new_file)
        em.add_command(label="New folder",
                       command=self.new_folder)
        em.add_separator()
        em.add_command(label="Delete", accelerator="Del",
                       command=self.delete_selected)
        em.add_separator()
        em.add_command(label="Import from host…",
                       command=self.import_files)
        em.add_command(label="Export to host…",
                       command=self.export_selected)
        menu.add_cascade(label="Edit", menu=em)

        vm = tk.Menu(menu, tearoff=0)
        vm.add_command(label="Refresh", accelerator="F5",
                       command=self.refresh)
        menu.add_cascade(label="View", menu=vm)

        hm = tk.Menu(menu, tearoff=0)
        hm.add_command(label="About", command=self.about)
        menu.add_cascade(label="Help", menu=hm)

        self.config(menu=menu)

    # ---------- toolbar ----------

    def _build_toolbar(self):
        bar = ttk.Frame(self, padding=(4, 4))
        bar.pack(side=tk.TOP, fill=tk.X)

        def btn(text, cmd):
            b = ttk.Button(bar, text=text, command=cmd)
            b.pack(side=tk.LEFT, padx=1)
            return b

        btn("Open",   self.open_image)
        btn("Save",   self.save_image)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(
            side=tk.LEFT, fill=tk.Y, padx=6)

        btn("↑", self.go_parent)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(
            side=tk.LEFT, fill=tk.Y, padx=6)

        btn("New File", self.new_file)
        btn("New Folder", self.new_folder)
        btn("Import", self.import_files)
        btn("Export", self.export_selected)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(
            side=tk.LEFT, fill=tk.Y, padx=6)

        btn("Delete", self.delete_selected)

        self.path_var = tk.StringVar(value="/")
        self.path_entry = ttk.Entry(bar, textvariable=self.path_var)
        self.path_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=10)
        self.path_entry.bind("<Return>", self.navigate_path)

        self.path_label = ttk.Label(bar, text="(no image)",
                                    foreground="#666")
        self.path_label.pack(side=tk.RIGHT, padx=8)

    # ---------- body ----------

    def _build_body(self):
        paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        paned.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.paned = paned

        left = ttk.Frame(paned)
        paned.add(left, weight=3)

        self.tree = ttk.Treeview(left, columns=self.COLS,
                                 show="headings",
                                 selectmode="extended")

        self.tree.heading("name", text="Name",
                          command=lambda: self.sort_by("name"))
        self.tree.heading("type", text="Type",
                          command=lambda: self.sort_by("type"))
        self.tree.heading("size", text="Size",
                          command=lambda: self.sort_by("size"))

        self.tree.column("name", width=420, anchor=tk.W)
        self.tree.column("type", width=100, anchor=tk.W)
        self.tree.column("size", width=100, anchor=tk.E)

        sb = ttk.Scrollbar(left, orient=tk.VERTICAL,
                           command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)

        self.tree.grid(row=0, column=0, sticky="nsew")
        sb.grid(row=0, column=1, sticky="ns")
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)

        self.tree.bind("<<TreeviewSelect>>", self.on_select)
        self.tree.bind("<Double-1>", self.on_double_click)
        self.tree.bind("<Button-3>", self.on_right_click)

        self.right = ttk.Frame(paned)
        paned.add(self.right, weight=2)

        ttk.Label(self.right, text="Content").pack(anchor=tk.W, padx=4)

        self.text = tk.Text(self.right, undo=True, wrap=tk.NONE,
                            font=("Consolas", 10),
                            bg="#1e1e1e", fg="#d4d4d4",
                            insertbackground="#d4d4d4",
                            selectbackground="#264f78")
        self.text.pack(fill=tk.BOTH, expand=True, padx=4, pady=(2, 4))
        self.text.bind("<<Modified>>", self.on_text_modified)

    # ---------- statusbar ----------

    def _build_statusbar(self):
        bar = ttk.Frame(self, relief=tk.SUNKEN, padding=(6, 2))
        bar.pack(side=tk.BOTTOM, fill=tk.X)

        self.status_left  = tk.StringVar(value="No image loaded.")
        self.status_right = tk.StringVar(value="")

        ttk.Label(bar, textvariable=self.status_left,
                  anchor=tk.W).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Label(bar, textvariable=self.status_right,
                  anchor=tk.E).pack(side=tk.RIGHT)

    # =====================================================
    # Image ops
    # =====================================================

    def open_image(self):
        path = filedialog.askopenfilename(
            title="Open FAT32 image",
            filetypes=[("Disk images", "*.img"), ("All files", "*.*")],
            parent=self)

        if not path:
            return

        try:
            img = Fat32Image()
            img.open(path)
        except Exception as e:
            messagebox.showerror("Load failed", str(e), parent=self)
            return

        self.img = img
        self.dirty.clear()
        self.current_index = None
        self.current_path = "/"
        self.text.delete("1.0", tk.END)

        self.path_label.config(text=os.path.basename(path))
        self.refresh()

        self.status_left.set(
            f"{path} — {img.total_clusters} clusters, "
            f"cluster size {img.cluster_size} bytes")

    def save_image(self):
        if self.img is None:
            messagebox.showinfo("Save", "Open an image first.", parent=self)
            return
        # Changes are written through to disk as they happen.
        # There's nothing to "commit" — just flush.
        self.img.f.flush()
        self.status_left.set("Saved.")
        messagebox.showinfo("Saved", "All changes flushed to disk.",
                            parent=self)

    # =====================================================
    # Navigation
    # =====================================================

    def navigate_path(self, _event=None):
        target = self.path_var.get().strip()
        if not target:
            return

        if not target.startswith("/"):
            target = "/" + target
        while "//" in target:
            target = target.replace("//", "/")
        if len(target) > 1 and target.endswith("/"):
            target = target[:-1]

        if self.img.resolve_dir(target) < 0:
            messagebox.showerror("Folder not found", target, parent=self)
            self.path_var.set(self.current_path)
            return

        self.open_folder(target)

    def open_folder(self, path):
        self.commit_current()
        self.current_path = path
        self.current_index = None
        self.text.delete("1.0", tk.END)
        self.refresh()

    def go_parent(self):
        if self.current_path == "/":
            return
        parts = self.current_path.strip("/").split("/")
        if len(parts) <= 1:
            self.open_folder("/")
        else:
            self.open_folder("/" + "/".join(parts[:-1]))

    # =====================================================
    # Refresh
    # =====================================================

    def refresh(self):
        if self.img is None:
            return

        self.tree.delete(*self.tree.get_children())
        self.path_var.set(self.current_path)

        cluster = self.img.resolve_dir(self.current_path)
        if cluster < 0:
            return

        rows = []
        for name, is_dir, first, size, _ in self.img.iterate_dir(cluster):
            rows.append((name, is_dir, size))

        def sort_key(row):
            name, is_dir, size = row
            if self.sort_col == "name":
                return (not is_dir, name.lower())
            if self.sort_col == "type":
                if is_dir:
                    return ""
                return file_type(name)
            if self.sort_col == "size":
                return -1 if is_dir else size
            return name.lower()

        rows.sort(key=sort_key, reverse=self.sort_rev)

        for name, is_dir, size in rows:
            if is_dir:
                self.tree.insert("", tk.END,
                                 iid="dir:" + name,
                                 values=("📁 " + name, "Folder", ""))
            else:
                self.tree.insert("", tk.END,
                                 iid="file:" + name,
                                 values=("📄 " + name,
                                         file_type(name),
                                         human_size(size)))

        self.update_status()

    def update_status(self):
        if self.img is None:
            self.status_right.set("")
            return

        try:
            free  = self.img.free_bytes()
            total = self.img.total_bytes()
            used  = total - free
        except Exception:
            self.status_right.set("")
            return

        self.status_right.set(
            f"{human_size(used)} / {human_size(total)} used")

    # =====================================================
    # Selection / preview
    # =====================================================

    def on_select(self, _event=None):
        sel = self.tree.selection()
        if not sel:
            return

        iid = sel[-1]

        if iid.startswith("dir:"):
            self.commit_current()
            self.current_index = None
            self.text.delete("1.0", tk.END)
            return

        if not iid.startswith("file:"):
            return

        name = iid[5:]

        full = (self.current_path.rstrip("/") + "/" + name) \
               if self.current_path != "/" else "/" + name

        try:
            data = self.img.read_file(full)
        except Exception as e:
            data = None
            self.status_left.set(f"Read failed: {e}")

        if data is None:
            data = b""

        self.current_index = full
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", data.decode("latin-1", errors="replace"))
        self.text.edit_modified(False)

    def on_text_modified(self, _event=None):
        if not self.text.edit_modified():
            return
        if self.current_index:
            self.dirty.add(self.current_index)
        self.text.edit_modified(False)

    def commit_current(self):
        if self.img is None or not self.current_index:
            return
        if self.current_index not in self.dirty:
            return

        content = self.text.get("1.0", "end-1c")
        try:
            self.img.write_file(self.current_index,
                                content.encode("latin-1",
                                               errors="replace"))
        except Exception as e:
            messagebox.showerror("Save failed", str(e), parent=self)

        self.dirty.discard(self.current_index)

    def on_double_click(self, event):
        iid = self.tree.identify_row(event.y)
        if not iid:
            return

        if iid.startswith("dir:"):
            name = iid[4:]
            if self.current_path == "/":
                new_path = "/" + name
            else:
                new_path = self.current_path.rstrip("/") + "/" + name
            self.open_folder(new_path)

    def on_right_click(self, event):
        iid = self.tree.identify_row(event.y)
        if iid and iid not in self.tree.selection():
            self.tree.selection_set(iid)

        menu = tk.Menu(self, tearoff=0)
        menu.add_command(label="New file", command=self.new_file)
        menu.add_command(label="New folder", command=self.new_folder)
        menu.add_separator()
        menu.add_command(label="Delete", command=self.delete_selected)
        menu.add_separator()
        menu.add_command(label="Import here…", command=self.import_files)
        menu.add_command(label="Export…", command=self.export_selected)
        menu.tk_popup(event.x_root, event.y_root)

    # =====================================================
    # New file / folder
    # =====================================================

    def new_file(self):
        if self.img is None:
            return

        name = simpledialog.askstring(
            "New file", "File name:",
            initialvalue="new.txt", parent=self)
        if not name:
            return

        full = (self.current_path.rstrip("/") + "/" + name) \
               if self.current_path != "/" else "/" + name

        try:
            self.img.write_file(full, b"")
        except Exception as e:
            messagebox.showerror("New file failed", str(e), parent=self)
            return

        self.refresh()

    def new_folder(self):
        # FAT32 folder creation requires a fresh cluster with .
        # and .. entries — more work than this file supports yet.
        messagebox.showinfo(
            "Not supported yet",
            "Folder creation isn't implemented in this build.\n"
            "Create the folder on the host, or use the raw disk builder.",
            parent=self)

    # =====================================================
    # Delete
    # =====================================================

    def delete_selected(self):
        if self.img is None:
            return

        sel = self.tree.selection()
        if not sel:
            return

        files = [iid[5:] for iid in sel if iid.startswith("file:")]

        if not files:
            messagebox.showinfo(
                "Delete", "Only files can be deleted in this build.",
                parent=self)
            return

        if not messagebox.askyesno(
                "Delete",
                f"Delete {len(files)} file(s)?",
                parent=self):
            return

        for name in files:
            full = (self.current_path.rstrip("/") + "/" + name) \
                   if self.current_path != "/" else "/" + name
            try:
                self.img.delete_file(full)
            except Exception as e:
                messagebox.showerror("Delete failed", str(e), parent=self)

        self.refresh()

    # =====================================================
    # Import / export
    # =====================================================

    def import_files(self):
        if self.img is None:
            return

        paths = filedialog.askopenfilenames(
            title="Import files", parent=self)
        if not paths:
            return

        count = 0
        for host in paths:
            if not os.path.isfile(host):
                continue
            with open(host, "rb") as f:
                data = f.read()

            name = os.path.basename(host)
            full = (self.current_path.rstrip("/") + "/" + name) \
                   if self.current_path != "/" else "/" + name

            try:
                self.img.write_file(full, data)
                count += 1
            except Exception as e:
                messagebox.showerror("Import failed",
                                     f"{host}\n{e}", parent=self)

        self.refresh()
        self.status_left.set(f"Imported {count} file(s)")

    def export_selected(self):
        if self.img is None:
            return

        sel = self.tree.selection()
        files = [iid[5:] for iid in sel if iid.startswith("file:")]

        if not files:
            return

        if len(files) == 1:
            name = files[0]
            full = (self.current_path.rstrip("/") + "/" + name) \
                   if self.current_path != "/" else "/" + name
            data = self.img.read_file(full)
            if data is None:
                return

            out = filedialog.asksaveasfilename(
                title="Export file", initialfile=name, parent=self)
            if not out:
                return

            with open(out, "wb") as f:
                f.write(data)
            self.status_left.set(f"Exported {name} → {out}")
            return

        outdir = filedialog.askdirectory(
            title="Export files to folder", parent=self)
        if not outdir:
            return

        for name in files:
            full = (self.current_path.rstrip("/") + "/" + name) \
                   if self.current_path != "/" else "/" + name
            data = self.img.read_file(full)
            if data is None:
                continue
            with open(os.path.join(outdir, name), "wb") as f:
                f.write(data)

        self.status_left.set(f"Exported {len(files)} file(s)")

    # =====================================================
    # Drag and drop
    # =====================================================

    def on_drop(self, event):
        if self.img is None:
            self.status_left.set("Load an image first.")
            return

        raw = event.data
        paths = []
        buf = ""
        in_brace = False

        for ch in raw:
            if ch == "{":
                in_brace = True
                buf = ""
            elif ch == "}":
                in_brace = False
                if buf:
                    paths.append(buf)
                buf = ""
            elif ch == " " and not in_brace:
                if buf:
                    paths.append(buf)
                    buf = ""
            else:
                buf += ch

        if buf:
            paths.append(buf)

        paths = [p for p in paths if os.path.isfile(p)]
        if not paths:
            return

        count = 0
        for host in paths:
            with open(host, "rb") as f:
                data = f.read()

            name = os.path.basename(host)
            full = (self.current_path.rstrip("/") + "/" + name) \
                   if self.current_path != "/" else "/" + name

            try:
                self.img.write_file(full, data)
                count += 1
            except Exception:
                pass

        self.refresh()
        self.status_left.set(f"Dropped {count} file(s)")

    # =====================================================
    # Sorting / misc
    # =====================================================

    def sort_by(self, col):
        if self.sort_col == col:
            self.sort_rev = not self.sort_rev
        else:
            self.sort_col = col
            self.sort_rev = False
        self.refresh()

    def about(self):
        messagebox.showinfo(
            "About",
            "FAT32 Explorer\n\n"
            "Browse and edit FAT32 disk images.\n"
            f"Drag-and-drop: "
            f"{'enabled' if HAVE_DND else 'not available'}",
            parent=self)


if __name__ == "__main__":
    App().mainloop()