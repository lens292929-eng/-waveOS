#!/usr/bin/env python3
"""
FAT32 Disk Builder
==================

Create or edit a FAT32 disk image (data.img) with a simple GUI.
Drag-and-drop or browse to add files, right-click to remove or rename,
then Save to write the .img to disk.

Requires:
    pip install pyfatfs

Run:
    python fat32builder.py
"""

import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog

try:
    import fs
    from pyfatfs.PyFat import PyFat
except ImportError:
    raise SystemExit(
        "Missing dependencies. Run: pip install pyfatfs"
    )


# =========================================================
# Configuration
# =========================================================

DEFAULT_SIZE_MB = 16
MIN_SIZE_MB     = 1
MAX_SIZE_MB     = 512


# =========================================================
# FAT32 wrapper
# =========================================================

class FatImage:
    """
    In-memory model of a FAT32 image.

    files: dict mapping normalized path (e.g. '/foo/bar.txt')
           to bytes content.

    Directories are implicit — they exist because some file's path
    contains them. An empty directory is stored as an entry in
    self.dirs (a set of '/path/' strings).
    """

    def __init__(self, size_mb=DEFAULT_SIZE_MB):
        self.size_mb = size_mb
        self.files = {}          # path -> bytes
        self.dirs  = set()       # '/foo/', '/foo/bar/'

    # ---------- load / save ----------

    def load(self, path):
        """Load existing .img into the model."""
        self.files.clear()
        self.dirs.clear()

        with fs.open_fs(f"fat://{path}") as fat:
            self._walk(fat, "/")

        # size from file
        self.size_mb = os.path.getsize(path) // (1024 * 1024)

    def _walk(self, fat, dirpath):
        try:
            entries = fat.listdir(dirpath)
        except Exception:
            return

        for name in entries:
            full = dirpath.rstrip("/") + "/" + name
            try:
                info = fat.getinfo(full)
            except Exception:
                continue

            if info.is_dir:
                self.dirs.add(full + "/")
                self._walk(fat, full)
            else:
                with fat.open(full, "rb") as f:
                    self.files[full] = f.read()

    def save(self, path):
        """Write model to a fresh FAT32 image at `path`."""
        size_bytes = self.size_mb * 1024 * 1024

        # Pre-allocate the file
        with open(path, "wb") as f:
            f.truncate(size_bytes)

        # Format
        fat = PyFat()
        fat.mkfs(path, fat_type=32)
        fat.close()

        # Write files + dirs
        with fs.open_fs(f"fat://{path}") as fat_fs:
            # Directories first, sorted shallow to deep
            for d in sorted(self.dirs):
                try:
                    fat_fs.makedirs(d, recreate=True)
                except Exception as e:
                    print("mkdir failed:", d, e)

            # Then files
            for p, data in self.files.items():
                parent = p.rsplit("/", 1)[0] or "/"
                if parent != "/":
                    try:
                        fat_fs.makedirs(parent, recreate=True)
                    except Exception:
                        pass
                with fat_fs.open(p, "wb") as f:
                    f.write(data)

    # ---------- mutation ----------

    def add_file(self, host_path, name_on_disk=None):
        if name_on_disk is None:
            name_on_disk = "/" + os.path.basename(host_path)

        if not name_on_disk.startswith("/"):
            name_on_disk = "/" + name_on_disk

        # Normalize double slashes
        while "//" in name_on_disk:
            name_on_disk = name_on_disk.replace("//", "/")

        with open(host_path, "rb") as f:
            data = f.read()

        self.files[name_on_disk] = data

    def remove(self, path):
        if path in self.files:
            del self.files[path]
        elif path in self.dirs:
            # Remove dir and everything under it
            prefix = path if path.endswith("/") else path + "/"

            to_del = [p for p in self.files if p.startswith(prefix)]
            for p in to_del:
                del self.files[p]

            to_del = [d for d in self.dirs if d.startswith(prefix) or d == path]
            for d in to_del:
                self.dirs.discard(d)

    def rename(self, old, new):
        if not new.startswith("/"):
            new = "/" + new

        if old in self.files:
            self.files[new] = self.files.pop(old)
        elif old in self.dirs:
            old_p = old if old.endswith("/") else old + "/"
            new_p = new if new.endswith("/") else new + "/"
            self.dirs.discard(old)
            self.dirs.add(new_p)

            # Rename children
            for p in list(self.files):
                if p.startswith(old_p):
                    self.files[new_p + p[len(old_p):]] = self.files.pop(p)

            for d in list(self.dirs):
                if d.startswith(old_p):
                    self.dirs.discard(d)
                    self.dirs.add(new_p + d[len(old_p):])

    def mkdir(self, path):
        if not path.startswith("/"):
            path = "/" + path
        if not path.endswith("/"):
            path += "/"
        self.dirs.add(path)

    # ---------- info ----------

    def total_used(self):
        return sum(len(d) for d in self.files.values())

    def capacity_bytes(self):
        # Approximate: FAT32 overhead is ~1% for a typical layout.
        return self.size_mb * 1024 * 1024


def human_size(n):
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.1f} MB"


# =========================================================
# GUI
# =========================================================

class App(tk.Tk):

    COLS = ("name", "type", "size", "path")

    def __init__(self):
        super().__init__()

        self.title("FAT32 Disk Builder")
        self.geometry("900x600")
        self.minsize(700, 400)

        self.fat = FatImage(DEFAULT_SIZE_MB)
        self.image_path = None

        self._build_menu()
        self._build_toolbar()
        self._build_body()
        self._build_statusbar()

        self.refresh()

        self.bind("<Control-o>", lambda e: self.open_image())
        self.bind("<Control-s>", lambda e: self.save_image())
        self.bind("<Delete>",    lambda e: self.delete_selected())
        self.bind("<F2>",        lambda e: self.rename_selected())
        self.bind("<F5>",        lambda e: self.refresh())

    # ---------- menu ----------

    def _build_menu(self):
        m = tk.Menu(self)

        fm = tk.Menu(m, tearoff=0)
        fm.add_command(label="New image…",
                       command=self.new_image)
        fm.add_command(label="Open image…", accelerator="Ctrl+O",
                       command=self.open_image)
        fm.add_command(label="Save image as…", accelerator="Ctrl+S",
                       command=self.save_image)
        fm.add_separator()
        fm.add_command(label="Exit", command=self.destroy)
        m.add_cascade(label="File", menu=fm)

        em = tk.Menu(m, tearoff=0)
        em.add_command(label="Add files…",
                       command=self.add_files)
        em.add_command(label="New folder…",
                       command=self.new_folder)
        em.add_separator()
        em.add_command(label="Rename", accelerator="F2",
                       command=self.rename_selected)
        em.add_command(label="Delete", accelerator="Del",
                       command=self.delete_selected)
        m.add_cascade(label="Edit", menu=em)

        hm = tk.Menu(m, tearoff=0)
        hm.add_command(label="About", command=self.about)
        m.add_cascade(label="Help", menu=hm)

        self.config(menu=m)

    # ---------- toolbar ----------

    def _build_toolbar(self):
        bar = ttk.Frame(self, padding=(4, 4))
        bar.pack(side=tk.TOP, fill=tk.X)

        def add(text, cmd):
            b = ttk.Button(bar, text=text, command=cmd)
            b.pack(side=tk.LEFT, padx=1)

        add("New",       self.new_image)
        add("Open",      self.open_image)
        add("Save as…",  self.save_image)
        ttk.Separator(bar, orient=tk.VERTICAL).pack(
            side=tk.LEFT, fill=tk.Y, padx=6)
        add("Add files", self.add_files)
        add("New folder", self.new_folder)
        ttk.Separator(bar, orient=tk.VERTICAL).pack(
            side=tk.LEFT, fill=tk.Y, padx=6)
        add("Delete", self.delete_selected)

        self.path_label = ttk.Label(bar, text="(unsaved)",
                                    foreground="#666")
        self.path_label.pack(side=tk.RIGHT, padx=8)

    # ---------- body ----------

    def _build_body(self):
        f = ttk.Frame(self)
        f.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=4, pady=4)

        cols = self.COLS
        self.tree = ttk.Treeview(f, columns=cols, show="headings",
                                 selectmode="extended")

        self.tree.heading("name", text="Name")
        self.tree.heading("type", text="Type")
        self.tree.heading("size", text="Size")
        self.tree.heading("path", text="Path")

        self.tree.column("name", width=240, anchor=tk.W)
        self.tree.column("type", width=80,  anchor=tk.W)
        self.tree.column("size", width=100, anchor=tk.E)
        self.tree.column("path", width=380, anchor=tk.W)

        vsb = ttk.Scrollbar(f, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)

        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        f.rowconfigure(0, weight=1)
        f.columnconfigure(0, weight=1)

        self.tree.bind("<Double-1>", self.on_double_click)
        self.tree.bind("<Button-3>", self.on_right_click)

    # ---------- statusbar ----------

    def _build_statusbar(self):
        bar = ttk.Frame(self, relief=tk.SUNKEN, padding=(6, 2))
        bar.pack(side=tk.BOTTOM, fill=tk.X)

        self.status_left = tk.StringVar(value="Ready.")
        self.status_right = tk.StringVar(value="")

        ttk.Label(bar, textvariable=self.status_left,
                  anchor=tk.W).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Label(bar, textvariable=self.status_right,
                  anchor=tk.E).pack(side=tk.RIGHT)

    # =====================================================
    # Image ops
    # =====================================================

    def new_image(self):
        size = simpledialog.askinteger(
            "New image",
            f"Size in MB ({MIN_SIZE_MB}–{MAX_SIZE_MB}):",
            initialvalue=DEFAULT_SIZE_MB,
            minvalue=MIN_SIZE_MB,
            maxvalue=MAX_SIZE_MB,
            parent=self)

        if not size:
            return

        self.fat = FatImage(size)
        self.image_path = None
        self.path_label.config(text="(unsaved)")
        self.refresh()
        self.status_left.set(f"New {size} MB image. Add files, then Save as…")

    def open_image(self):
        path = filedialog.askopenfilename(
            title="Open FAT32 image",
            filetypes=[("Disk images", "*.img"), ("All files", "*.*")],
            parent=self)

        if not path:
            return

        try:
            self.fat.load(path)
        except Exception as e:
            messagebox.showerror("Load failed", str(e), parent=self)
            return

        self.image_path = path
        self.path_label.config(text=os.path.basename(path))
        self.refresh()
        self.status_left.set(f"Opened {path}")

    def save_image(self):
        initial = self.image_path or "data.img"
        path = filedialog.asksaveasfilename(
            title="Save FAT32 image",
            initialfile=initial,
            defaultextension=".img",
            filetypes=[("Disk images", "*.img"), ("All files", "*.*")],
            parent=self)

        if not path:
            return

        try:
            self.fat.save(path)
        except Exception as e:
            messagebox.showerror("Save failed", str(e), parent=self)
            return

        self.image_path = path
        self.path_label.config(text=os.path.basename(path))
        self.status_left.set(f"Saved {path} ({self.fat.size_mb} MB)")
        messagebox.showinfo(
            "Saved",
            f"Wrote {len(self.fat.files)} file(s) to {path}.",
            parent=self)

    # =====================================================
    # File ops
    # =====================================================

    def add_files(self):
        paths = filedialog.askopenfilenames(
            title="Add files",
            parent=self)

        if not paths:
            return

        added = 0
        for p in paths:
            if not os.path.isfile(p):
                continue
            try:
                self.fat.add_file(p)
                added += 1
            except Exception as e:
                messagebox.showerror("Add failed", f"{p}\n{e}", parent=self)

        self.refresh()
        self.status_left.set(f"Added {added} file(s)")

    def new_folder(self):
        name = simpledialog.askstring(
            "New folder",
            "Folder name:",
            initialvalue="folder",
            parent=self)

        if not name:
            return

        if not name.startswith("/"):
            name = "/" + name
        if not name.endswith("/"):
            name += "/"

        self.fat.mkdir(name)
        self.refresh()

    def rename_selected(self):
        sel = self.tree.selection()
        if not sel:
            return

        path = sel[0]
        old = path  # iid is path

        new = simpledialog.askstring(
            "Rename",
            "New path:",
            initialvalue=old,
            parent=self)

        if not new or new == old:
            return

        self.fat.rename(old, new)
        self.refresh()

    def delete_selected(self):
        sel = self.tree.selection()
        if not sel:
            return

        if not messagebox.askyesno(
                "Delete",
                f"Delete {len(sel)} item(s)?",
                parent=self):
            return

        for path in sel:
            self.fat.remove(path)

        self.refresh()

    def on_double_click(self, event):
        # Rename on double-click, like Windows Explorer
        self.rename_selected()

    def on_right_click(self, event):
        item = self.tree.identify_row(event.y)
        if item:
            if item not in self.tree.selection():
                self.tree.selection_set(item)

        m = tk.Menu(self, tearoff=0)
        m.add_command(label="Rename", command=self.rename_selected)
        m.add_command(label="Delete", command=self.delete_selected)
        m.tk_popup(event.x_root, event.y_root)

    # =====================================================
    # Refresh
    # =====================================================

    def refresh(self):
        self.tree.delete(*self.tree.get_children())

        # Directories first
        for d in sorted(self.fat.dirs):
            name = d.rstrip("/").rsplit("/", 1)[-1] or "/"
            self.tree.insert("", tk.END, iid=d,
                             values=(name + "/", "Folder", "", d))

        # Then files
        for p in sorted(self.fat.files):
            name = p.rsplit("/", 1)[-1]
            data = self.fat.files[p]
            ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
            kind = {
                "txt": "TXT", "md": "TXT", "log": "TXT",
                "c": "C", "h": "C",
                "wss": "WSS",
                "asm": "ASM", "s": "ASM",
                "bin": "Binary",
                "json": "JSON",
                "py": "Python",
            }.get(ext, "File")

            self.tree.insert("", tk.END, iid=p,
                             values=(name, kind, human_size(len(data)), p))

        used  = self.fat.total_used()
        total = self.fat.capacity_bytes()
        pct   = (used / total * 100.0) if total else 0.0

        self.status_right.set(
            f"{len(self.fat.files)} file(s), {len(self.fat.dirs)} folder(s)"
            f"  |  {human_size(used)} / {human_size(total)}"
            f" ({pct:.1f}%)")

    # =====================================================
    # Misc
    # =====================================================

    def about(self):
        messagebox.showinfo(
            "About",
            "FAT32 Disk Builder\n\n"
            "Build a FAT32 .img with files and folders.\n"
            "Save it as data.img and attach it to QEMU as a second disk.",
            parent=self)


if __name__ == "__main__":
    App().mainloop()