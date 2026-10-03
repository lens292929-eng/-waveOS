#!/usr/bin/env python3
"""
wFs Explorer — Windows-Explorer-style GUI for waveOS disk images.

Features:
  * Real directory-style navigation
  * Subdirectories inferred from file paths
  * Double-click folders to enter them
  * Alt+Up / Up button to go to parent
  * Breadcrumb/path entry
  * New File creates files in the current directory
  * New Folder creates directories
  * Import puts files into the current directory
  * Rename files and folders
  * Renaming folders moves the whole subtree
  * Delete files or complete directories
  * Sortable Name / Type / Size columns
  * Drag-and-drop import from host
  * Preview/editor pane
  * Keyboard shortcuts
  * Unsaved-change tracking
  * Status bar with file/folder/disk usage

Requires:
    pip install tkinterdnd2

Run:
    python wfs-explorer.py
"""

import os
import struct
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog


# =========================================================
# Optional drag-and-drop support
# =========================================================

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD

    HAVE_DND = True

except ImportError:
    HAVE_DND = False
    DND_FILES = None
    TkinterDnD = None


# =========================================================
# wFs constants
# =========================================================

SECTOR = 512
WFS_MAGIC = 0x31534657
WFS_VERSION = 1


# =========================================================
# wFs image format
# =========================================================

class WfsImage:

    def __init__(self, path=None):
        self.path = path
        self.lba = 0
        self.sectors = 0

        # Each entry:
        #
        # [
        #     "/path/file.txt",
        #     bytearray(...)
        # ]
        #
        self.entries = []

    # -----------------------------------------------------
    # Load image
    # -----------------------------------------------------

    def load(self, path=None):

        if path is not None:
            self.path = path

        if not self.path:
            raise ValueError("No image path specified")

        with open(self.path, "rb") as img:

            # -------------------------------------------------
            # MBR
            # -------------------------------------------------

            img.seek(0)

            mbr = img.read(SECTOR)

            if (
                len(mbr) < SECTOR
                or mbr[510:512] != b"\x55\xAA"
            ):
                raise ValueError(
                    "not an MBR-partitioned image"
                )

            # First partition entry
            entry = mbr[462:478]

            self.lba = struct.unpack_from(
                "<I",
                entry,
                8
            )[0]

            self.sectors = struct.unpack_from(
                "<I",
                entry,
                12
            )[0]

            if self.lba == 0 or self.sectors == 0:
                raise ValueError(
                    "no wFs partition in MBR"
                )

            # -------------------------------------------------
            # wFs header
            # -------------------------------------------------

            img.seek(
                self.lba * SECTOR
            )

            header = img.read(SECTOR)

            if len(header) < SECTOR:
                raise ValueError(
                    "wFs header is incomplete"
                )

            magic, version, count = struct.unpack_from(
                "<III",
                header,
                0
            )

            if magic != WFS_MAGIC:

                self.entries = []

                return

            if version != WFS_VERSION:
                raise ValueError(
                    f"wFs version {version} not supported"
                )

            # -------------------------------------------------
            # Payload
            # -------------------------------------------------

            payload_bytes = (
                self.sectors * SECTOR
                - SECTOR
            )

            img.seek(
                (self.lba + 1) * SECTOR
            )

            payload = img.read(
                payload_bytes
            )

            self.entries = []

            off = 0

            while off + 2 <= len(payload):

                used = payload[off]

                if used == 0:
                    break

                name_len = payload[off + 1]

                if (
                    name_len == 0
                    or name_len > 255
                ):
                    break

                name_start = off + 2
                name_end = name_start + name_len

                if name_end + 4 > len(payload):
                    break

                name = payload[
                    name_start:name_end
                ].decode(
                    "latin-1"
                )

                data_len = struct.unpack_from(
                    "<I",
                    payload,
                    name_end
                )[0]

                data_start = name_end + 4
                data_end = data_start + data_len

                if data_end > len(payload):
                    break

                data = payload[
                    data_start:data_end
                ]

                self.entries.append(
                    [
                        name,
                        bytearray(data)
                    ]
                )

                off = data_end

    # -----------------------------------------------------
    # Inject image
    # -----------------------------------------------------

    def inject(self):

        if not self.path:
            raise ValueError(
                "No image path specified"
            )

        header = struct.pack(
            "<III",
            WFS_MAGIC,
            WFS_VERSION,
            len(self.entries)
        )

        header += b"\x00" * (
            SECTOR - 12
        )

        payload = bytearray()

        for name, data in self.entries:

            if not name:
                raise ValueError(
                    "Empty file name"
                )

            name_bytes = name.encode(
                "latin-1"
            )

            nl = len(name_bytes)

            if nl == 0 or nl > 255:
                raise ValueError(
                    f"file name too long: {name}"
                )

            payload.append(1)

            payload.append(nl)

            payload.extend(
                name_bytes
            )

            payload.extend(
                struct.pack(
                    "<I",
                    len(data)
                )
            )

            payload.extend(data)

        while len(payload) % SECTOR:
            payload.append(0)

        capacity = (
            self.sectors * SECTOR
            - SECTOR
        )

        if len(payload) > capacity:
            raise ValueError(
                "payload exceeds wFs partition size"
            )

        payload.extend(
            b"\x00" * (
                capacity - len(payload)
            )
        )

        with open(
            self.path,
            "r+b"
        ) as img:

            img.seek(
                self.lba * SECTOR
            )

            img.write(header)

            img.write(payload)

    # -----------------------------------------------------
    # Find entry
    # -----------------------------------------------------

    def find(self, name):

        for i, (entry_name, _) in enumerate(
            self.entries
        ):

            if entry_name == name:
                return i

        return -1


# =========================================================
# Path helpers
# =========================================================

def normalize_path(path):
    """
    Normalize wFs paths.

    Examples:

        foo.txt       -> /foo.txt
        /foo/bar.txt  -> /foo/bar.txt
        /foo/bar/     -> /foo/bar
        \\foo\\bar    -> /foo/bar
    """

    if not path:
        return "/"

    path = path.replace(
        "\\",
        "/"
    )

    if not path.startswith("/"):
        path = "/" + path

    while "//" in path:
        path = path.replace(
            "//",
            "/"
        )

    if len(path) > 1 and path.endswith("/"):
        path = path[:-1]

    return path


def parent_path(path):
    """
    Return parent directory.

    /foo/bar.txt -> /foo
    /foo         -> /
    /            -> /
    """

    path = normalize_path(path)

    if path == "/":
        return "/"

    parts = path.strip("/").split("/")

    if len(parts) <= 1:
        return "/"

    return "/" + "/".join(
        parts[:-1]
    )


def base_name(path):
    """
    Return final path component.

    /foo/bar.txt -> bar.txt
    /foo         -> foo
    """

    path = normalize_path(path)

    if path == "/":
        return "/"

    return path.rsplit(
        "/",
        1
    )[-1]


def human_size(size):
    if size < 1024:
        return f"{size} B"

    if size < 1024 * 1024:
        return f"{size / 1024:.1f} KB"

    if size < 1024 * 1024 * 1024:
        return f"{size / (1024 * 1024):.1f} MB"

    return f"{size / (1024 * 1024 * 1024):.1f} GB"


def file_type(name):

    lower = name.lower()

    if lower.endswith(".wss"):
        return "WSS"

    if lower.endswith(
        (".asm", ".s")
    ):
        return "ASM"

    if lower.endswith(
        (".c", ".h")
    ):
        return "C"

    if lower.endswith(
        (".cpp", ".hpp")
    ):
        return "C++"

    if lower.endswith(".py"):
        return "Python"

    if lower.endswith(".txt"):
        return "TXT"

    if lower.endswith(".bin"):
        return "Binary"

    if "." in lower:
        return lower.rsplit(
            ".",
            1
        )[1].upper()

    return "File"


# =========================================================
# Explorer application
# =========================================================

class App(
    TkinterDnD.Tk if HAVE_DND else tk.Tk
):

    COLS = (
        "name",
        "type",
        "size"
    )

    # =====================================================
    # Init
    # =====================================================

    def __init__(self):

        super().__init__()

        self.title(
            "wFs Explorer"
        )

        self.geometry(
            "1100x700"
        )

        self.minsize(
            800,
            500
        )

        # -------------------------------------------------
        # State
        # -------------------------------------------------

        self.wfs = None

        self.current_path = "/"

        self.current_index = None

        self.dirty = set()

        self.sort_col = "name"

        self.sort_rev = False

        # -------------------------------------------------
        # Build UI
        # -------------------------------------------------

        self._build_menu()

        self._build_toolbar()

        self._build_body()

        self._build_statusbar()

        # -------------------------------------------------
        # Keyboard
        # -------------------------------------------------

        self.bind(
            "<Control-o>",
            lambda e: self.open_image()
        )

        self.bind(
            "<Control-s>",
            lambda e: self.inject()
        )

        self.bind(
            "<Control-n>",
            lambda e: self.new_file()
        )

        self.bind(
            "<Delete>",
            lambda e: self.delete_selected()
        )

        self.bind(
            "<F2>",
            lambda e: self.rename_selected()
        )

        self.bind(
            "<F5>",
            lambda e: self.refresh()
        )

        self.bind(
            "<Alt-Up>",
            lambda e: self.go_parent()
        )

        # -------------------------------------------------
        # Drag & drop
        # -------------------------------------------------

        if HAVE_DND:

            self.drop_target_register(
                DND_FILES
            )

            self.dnd_bind(
                "<<Drop>>",
                self.on_drop
            )

    # =====================================================
    # Menu
    # =====================================================

    def _build_menu(self):

        menu = tk.Menu(
            self
        )

        # -------------------------------------------------
        # File
        # -------------------------------------------------

        file_menu = tk.Menu(
            menu,
            tearoff=0
        )

        file_menu.add_command(
            label="Open image…",
            accelerator="Ctrl+O",
            command=self.open_image
        )

        file_menu.add_command(
            label="Inject / Save",
            accelerator="Ctrl+S",
            command=self.inject
        )

        file_menu.add_separator()

        file_menu.add_command(
            label="Exit",
            command=self.destroy
        )

        menu.add_cascade(
            label="File",
            menu=file_menu
        )

        # -------------------------------------------------
        # Edit
        # -------------------------------------------------

        edit_menu = tk.Menu(
            menu,
            tearoff=0
        )

        edit_menu.add_command(
            label="New file",
            accelerator="Ctrl+N",
            command=self.new_file
        )

        edit_menu.add_command(
            label="New folder",
            command=self.new_folder
        )

        edit_menu.add_separator()

        edit_menu.add_command(
            label="Rename",
            accelerator="F2",
            command=self.rename_selected
        )

        edit_menu.add_command(
            label="Delete",
            accelerator="Del",
            command=self.delete_selected
        )

        edit_menu.add_separator()

        edit_menu.add_command(
            label="Import from host…",
            command=self.import_files
        )

        edit_menu.add_command(
            label="Export to host…",
            command=self.export_selected
        )

        menu.add_cascade(
            label="Edit",
            menu=edit_menu
        )

        # -------------------------------------------------
        # View
        # -------------------------------------------------

        view_menu = tk.Menu(
            menu,
            tearoff=0
        )

        view_menu.add_command(
            label="Refresh",
            accelerator="F5",
            command=self.refresh
        )

        view_menu.add_separator()

        self.preview_var = tk.BooleanVar(
            value=True
        )

        view_menu.add_checkbutton(
            label="Preview pane",
            variable=self.preview_var,
            command=self.toggle_preview
        )

        menu.add_cascade(
            label="View",
            menu=view_menu
        )

        # -------------------------------------------------
        # Help
        # -------------------------------------------------

        help_menu = tk.Menu(
            menu,
            tearoff=0
        )

        help_menu.add_command(
            label="About",
            command=self.about
        )

        menu.add_cascade(
            label="Help",
            menu=help_menu
        )

        self.config(
            menu=menu
        )

    # =====================================================
    # Toolbar
    # =====================================================

    def _build_toolbar(self):

        bar = ttk.Frame(
            self,
            padding=(4, 4)
        )

        bar.pack(
            side=tk.TOP,
            fill=tk.X
        )

        def button(
            text,
            command
        ):

            b = ttk.Button(
                bar,
                text=text,
                command=command
            )

            b.pack(
                side=tk.LEFT,
                padx=1
            )

            return b

        button(
            "Open",
            self.open_image
        )

        button(
            "Save",
            self.inject
        )

        ttk.Separator(
            bar,
            orient=tk.VERTICAL
        ).pack(
            side=tk.LEFT,
            fill=tk.Y,
            padx=6
        )

        button(
            "←",
            self.go_parent
        )

        button(
            "↑",
            self.go_parent
        )

        ttk.Separator(
            bar,
            orient=tk.VERTICAL
        ).pack(
            side=tk.LEFT,
            fill=tk.Y,
            padx=6
        )

        button(
            "New File",
            self.new_file
        )

        button(
            "New Folder",
            self.new_folder
        )

        button(
            "Import",
            self.import_files
        )

        button(
            "Export",
            self.export_selected
        )

        ttk.Separator(
            bar,
            orient=tk.VERTICAL
        ).pack(
            side=tk.LEFT,
            fill=tk.Y,
            padx=6
        )

        button(
            "Delete",
            self.delete_selected
        )

        # -------------------------------------------------
        # Path
        # -------------------------------------------------

        self.path_var = tk.StringVar(
            value="/"
        )

        self.path_entry = ttk.Entry(
            bar,
            textvariable=self.path_var
        )

        self.path_entry.pack(
            side=tk.LEFT,
            fill=tk.X,
            expand=True,
            padx=10
        )

        self.path_entry.bind(
            "<Return>",
            self.navigate_path
        )

        # -------------------------------------------------
        # Image name
        # -------------------------------------------------

        self.path_label = ttk.Label(
            bar,
            text="(no image loaded)",
            foreground="#666"
        )

        self.path_label.pack(
            side=tk.RIGHT,
            padx=8
        )

    # =====================================================
    # Main body
    # =====================================================

    def _build_body(self):

        paned = ttk.PanedWindow(
            self,
            orient=tk.HORIZONTAL
        )

        paned.pack(
            side=tk.TOP,
            fill=tk.BOTH,
            expand=True,
            padx=4,
            pady=4
        )

        self.paned = paned

        # -------------------------------------------------
        # Browser
        # -------------------------------------------------

        left = ttk.Frame(
            paned
        )

        paned.add(
            left,
            weight=3
        )

        self.tree = ttk.Treeview(
            left,
            columns=self.COLS,
            show="headings",
            selectmode="extended"
        )

        self.tree.heading(
            "name",
            text="Name",
            command=lambda:
                self.sort_by("name")
        )

        self.tree.heading(
            "type",
            text="Type",
            command=lambda:
                self.sort_by("type")
        )

        self.tree.heading(
            "size",
            text="Size",
            command=lambda:
                self.sort_by("size")
        )

        self.tree.column(
            "name",
            width=420,
            anchor=tk.W
        )

        self.tree.column(
            "type",
            width=100,
            anchor=tk.W
        )

        self.tree.column(
            "size",
            width=100,
            anchor=tk.E
        )

        scrollbar = ttk.Scrollbar(
            left,
            orient=tk.VERTICAL,
            command=self.tree.yview
        )

        self.tree.configure(
            yscrollcommand=scrollbar.set
        )

        self.tree.grid(
            row=0,
            column=0,
            sticky="nsew"
        )

        scrollbar.grid(
            row=0,
            column=1,
            sticky="ns"
        )

        left.rowconfigure(
            0,
            weight=1
        )

        left.columnconfigure(
            0,
            weight=1
        )

        self.tree.bind(
            "<<TreeviewSelect>>",
            self.on_select
        )

        self.tree.bind(
            "<Double-1>",
            self.on_double_click
        )

        self.tree.bind(
            "<Button-3>",
            self.on_right_click
        )

        # -------------------------------------------------
        # Preview/editor
        # -------------------------------------------------

        self.right = ttk.Frame(
            paned
        )

        paned.add(
            self.right,
            weight=2
        )

        name_row = ttk.Frame(
            self.right
        )

        name_row.pack(
            fill=tk.X,
            padx=4,
            pady=(0, 4)
        )

        ttk.Label(
            name_row,
            text="Name:"
        ).pack(
            side=tk.LEFT
        )

        self.name_var = tk.StringVar()

        self.name_entry = ttk.Entry(
            name_row,
            textvariable=self.name_var
        )

        self.name_entry.pack(
            side=tk.LEFT,
            fill=tk.X,
            expand=True,
            padx=(6, 0)
        )

        self.name_entry.bind(
            "<FocusOut>",
            self.on_name_changed
        )

        self.name_entry.bind(
            "<Return>",
            self.on_name_changed
        )

        ttk.Label(
            self.right,
            text="Content"
        ).pack(
            anchor=tk.W,
            padx=4
        )

        self.text = tk.Text(
            self.right,
            undo=True,
            wrap=tk.NONE,
            font=("Consolas", 10),
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#d4d4d4",
            selectbackground="#264f78"
        )

        self.text.pack(
            fill=tk.BOTH,
            expand=True,
            padx=4,
            pady=(2, 4)
        )

        self.text.bind(
            "<<Modified>>",
            self.on_text_modified
        )

    # =====================================================
    # Status bar
    # =====================================================

    def _build_statusbar(self):

        bar = ttk.Frame(
            self,
            relief=tk.SUNKEN,
            padding=(6, 2)
        )

        bar.pack(
            side=tk.BOTTOM,
            fill=tk.X
        )

        self.status_left = tk.StringVar(
            value="No image loaded."
        )

        self.status_right = tk.StringVar(
            value=""
        )

        ttk.Label(
            bar,
            textvariable=self.status_left,
            anchor=tk.W
        ).pack(
            side=tk.LEFT,
            fill=tk.X,
            expand=True
        )

        ttk.Label(
            bar,
            textvariable=self.status_right,
            anchor=tk.E
        ).pack(
            side=tk.RIGHT
        )

    # =====================================================
    # Image operations
    # =====================================================

    def open_image(self):

        path = filedialog.askopenfilename(
            title="Open disk image",
            filetypes=[
                ("Disk images", "*.img"),
                ("All files", "*.*")
            ]
        )

        if not path:
            return

        try:

            image = WfsImage(
                path
            )

            image.load()

        except Exception as e:

            messagebox.showerror(
                "Failed to load",
                str(e),
                parent=self
            )

            return

        self.wfs = image

        self.dirty.clear()

        self.current_index = None

        self.current_path = "/"

        self.name_var.set("")

        self.text.delete(
            "1.0",
            tk.END
        )

        self.path_label.config(
            text=os.path.basename(path)
        )

        self.refresh()

        self.status_left.set(
            f"{path} — "
            f"wFs at LBA {image.lba}, "
            f"{image.sectors} sectors"
        )

    # =====================================================
    # Directory discovery
    # =====================================================

    def directory_exists(self, path):

        path = normalize_path(
            path
        )

        if path == "/":
            return True

        prefix = (
            path.rstrip("/")
            + "/"
        )

        for name, _ in self.wfs.entries:

            name = normalize_path(
                name
            )

            if name.startswith(prefix):
                return True

        return False

    # -----------------------------------------------------
    # Immediate children
    # -----------------------------------------------------

    def children_of(self, directory):

        directory = normalize_path(
            directory
        )

        prefix = directory

        if prefix != "/":
            prefix += "/"

        found = {}

        for index, (
            path,
            data
        ) in enumerate(
            self.wfs.entries
        ):

            path = path.replace(
                "\\",
                "/"
            )

            if not path.startswith(prefix):
                continue

            remainder = path[
                len(prefix):
            ]

            if not remainder:
                continue

            parts = remainder.split(
                "/"
            )

            child_name = parts[0]

            if directory == "/":
                child_path = (
                    "/"
                    + child_name
                )
            else:
                child_path = (
                    directory.rstrip("/")
                    + "/"
                    + child_name
                )

            # ---------------------------------------------
            # Nested path = implicit folder
            # ---------------------------------------------

            if len(parts) > 1:

                found[child_path] = {
                    "path": child_path,
                    "folder": True,
                    "index": None,
                    "name": child_name
                }

            # ---------------------------------------------
            # Explicit directory entry
            # ---------------------------------------------

            elif path.endswith("/"):

                found[child_path] = {
                    "path": child_path,
                    "folder": True,
                    "index": index,
                    "name": child_name
                }

            # ---------------------------------------------
            # Normal file
            # ---------------------------------------------

            else:

                found[child_path] = {
                    "path": child_path,
                    "folder": False,
                    "index": index,
                    "name": child_name
                }

        return list(
            found.values()
        )

    # =====================================================
    # Refresh browser
    # =====================================================

    def refresh(self):

        if self.wfs is None:
            return

        self.tree.delete(
            *self.tree.get_children()
        )

        self.path_var.set(
            self.current_path
        )

        rows = self.children_of(
            self.current_path
        )

        # -------------------------------------------------
        # Sorting
        # -------------------------------------------------

        def sort_key(row):

            if self.sort_col == "name":
                return row["name"].lower()

            if self.sort_col == "type":

                if row["folder"]:
                    return "folder"

                return file_type(
                    row["name"]
                )

            if self.sort_col == "size":

                if row["folder"]:
                    return -1

                idx = row["index"]

                return len(
                    self.wfs.entries[idx][1]
                )

            return row["name"].lower()

        rows.sort(
            key=sort_key,
            reverse=self.sort_rev
        )

        # Folders first when sorting by name
        if self.sort_col == "name":

            rows.sort(
                key=lambda row: (
                    not row["folder"],
                    row["name"].lower()
                )
            )

        # -------------------------------------------------
        # Add rows
        # -------------------------------------------------

        for row in rows:

            if row["folder"]:

                iid = (
                    "folder:"
                    + row["path"]
                )

                self.tree.insert(
                    "",
                    tk.END,
                    iid=iid,
                    values=(
                        "📁 "
                        + row["name"],
                        "Folder",
                        ""
                    )
                )

            else:

                idx = row["index"]

                name, data = (
                    self.wfs.entries[idx]
                )

                marker = (
                    "* "
                    if idx in self.dirty
                    else ""
                )

                iid = (
                    "file:"
                    + str(idx)
                )

                self.tree.insert(
                    "",
                    tk.END,
                    iid=iid,
                    values=(
                        marker
                        + "📄 "
                        + row["name"],
                        file_type(name),
                        human_size(
                            len(data)
                        )
                    )
                )

        self.update_status()

    # =====================================================
    # Navigation
    # =====================================================

    def navigate_path(
        self,
        _event=None
    ):

        path = normalize_path(
            self.path_var.get()
        )

        if path == "/":

            self.open_folder("/")

            return

        if not self.directory_exists(path):

            messagebox.showerror(
                "Folder not found",
                f"The folder does not exist:\n\n{path}",
                parent=self
            )

            self.path_var.set(
                self.current_path
            )

            return

        self.open_folder(
            path
        )

    def open_folder(self, path):

        self.commit_current()

        self.current_path = normalize_path(
            path
        )

        self.current_index = None

        self.name_var.set("")

        self.text.delete(
            "1.0",
            tk.END
        )

        self.refresh()

    def go_parent(self):

        if self.current_path == "/":
            return

        self.open_folder(
            parent_path(
                self.current_path
            )
        )

    # =====================================================
    # Selection
    # =====================================================

    def selected_indices(self):

        result = []

        for iid in self.tree.selection():

            if iid.startswith("file:"):

                result.append(
                    int(iid[5:])
                )

        return result

    def on_select(
        self,
        _event=None
    ):

        selection = self.tree.selection()

        if not selection:
            return

        iid = selection[-1]

        # Folder selected
        if iid.startswith(
            "folder:"
        ):

            self.commit_current()

            self.current_index = None

            self.name_var.set("")

            self.text.delete(
                "1.0",
                tk.END
            )

            return

        if not iid.startswith(
            "file:"
        ):
            return

        index = int(
            iid[5:]
        )

        if index == self.current_index:
            return

        self.commit_current()

        self.current_index = index

        name, data = (
            self.wfs.entries[index]
        )

        self.name_var.set(
            base_name(name)
        )

        self.text.delete(
            "1.0",
            tk.END
        )

        self.text.insert(
            "1.0",
            data.decode(
                "latin-1",
                errors="replace"
            )
        )

        self.text.edit_modified(
            False
        )

    # =====================================================
    # Double click
    # =====================================================

    def on_double_click(
        self,
        event
    ):

        iid = self.tree.identify_row(
            event.y
        )

        if not iid:
            return

        # -------------------------------------------------
        # Folder
        # -------------------------------------------------

        if iid.startswith(
            "folder:"
        ):

            path = iid[
                len("folder:"):
            ]

            self.open_folder(
                path
            )

            return

        # -------------------------------------------------
        # File
        # -------------------------------------------------

        if iid.startswith(
            "file:"
        ):

            self.tree.selection_set(
                iid
            )

            self.on_select()

            self.text.focus_set()

    # =====================================================
    # Context menu
    # =====================================================

    def on_right_click(
        self,
        event
    ):

        iid = self.tree.identify_row(
            event.y
        )

        menu = tk.Menu(
            self,
            tearoff=0
        )

        if iid:

            if iid not in self.tree.selection():

                self.tree.selection_set(
                    iid
                )

            if iid.startswith(
                "folder:"
            ):

                folder = iid[
                    len("folder:"):
                ]

                menu.add_command(
                    label="Open",
                    command=lambda:
                        self.open_folder(
                            folder
                        )
                )

            else:

                menu.add_command(
                    label="Open",
                    command=lambda:
                        self.focus_selected_file()
                )

            menu.add_command(
                label="Rename",
                command=self.rename_selected
            )

            menu.add_command(
                label="Delete",
                command=self.delete_selected
            )

            menu.add_separator()

            menu.add_command(
                label="Export to host…",
                command=self.export_selected
            )

            menu.add_command(
                label="Import here…",
                command=self.import_files
            )

        else:

            menu.add_command(
                label="New file",
                command=self.new_file
            )

            menu.add_command(
                label="New folder",
                command=self.new_folder
            )

            menu.add_separator()

            menu.add_command(
                label="Import here…",
                command=self.import_files
            )

        menu.tk_popup(
            event.x_root,
            event.y_root
        )

    def focus_selected_file(self):

        selection = self.tree.selection()

        if not selection:
            return

        iid = selection[-1]

        if iid.startswith(
            "file:"
        ):

            self.on_select()

            self.text.focus_set()

    # =====================================================
    # Text editing
    # =====================================================

    def on_text_modified(
        self,
        _event=None
    ):

        if not self.text.edit_modified():
            return

        if self.current_index is not None:

            self.dirty.add(
                self.current_index
            )

        self.text.edit_modified(
            False
        )

    def commit_current(self):

        if (
            self.wfs is None
            or self.current_index is None
        ):
            return

        content = self.text.get(
            "1.0",
            "end-1c"
        )

        self.wfs.entries[
            self.current_index
        ][1] = bytearray(
            content.encode(
                "latin-1",
                errors="replace"
            )
        )

    # =====================================================
    # Rename
    # =====================================================

    def on_name_changed(
        self,
        _event=None
    ):

        if self.current_index is None:
            return

        index = self.current_index

        old_path = self.wfs.entries[
            index
        ][0]

        new_name = self.name_var.get().strip()

        if not new_name:
            return

        if "/" in new_name or "\\" in new_name:

            messagebox.showerror(
                "Rename",
                "Enter only the file name.",
                parent=self
            )

            self.name_var.set(
                base_name(old_path)
            )

            return

        if self.current_path == "/":

            new_path = (
                "/"
                + new_name
            )

        else:

            new_path = (
                self.current_path.rstrip("/")
                + "/"
                + new_name
            )

        new_path = normalize_path(
            new_path
        )

        if new_path == old_path:
            return

        if self.wfs.find(
            new_path
        ) >= 0:

            messagebox.showerror(
                "Rename",
                "A file with that name already exists.",
                parent=self
            )

            self.name_var.set(
                base_name(old_path)
            )

            return

        self.wfs.entries[
            index
        ][0] = new_path

        self.dirty.add(
            index
        )

        self.refresh()

    def rename_selected(self):

        selection = self.tree.selection()

        if not selection:
            return

        iid = selection[0]

        # -------------------------------------------------
        # Folder
        # -------------------------------------------------

        if iid.startswith(
            "folder:"
        ):

            old_folder = normalize_path(
                iid[len("folder:"):]
            )

            old_name = base_name(
                old_folder
            )

            new_name = simpledialog.askstring(
                "Rename folder",
                "New folder name:",
                initialvalue=old_name,
                parent=self
            )

            if not new_name:
                return

            new_name = new_name.strip()

            if (
                not new_name
                or "/" in new_name
                or "\\" in new_name
            ):

                messagebox.showerror(
                    "Rename folder",
                    "Invalid folder name.",
                    parent=self
                )

                return

            parent = parent_path(
                old_folder
            )

            if parent == "/":

                new_folder = (
                    "/"
                    + new_name
                )

            else:

                new_folder = (
                    parent.rstrip("/")
                    + "/"
                    + new_name
                )

            old_prefix = (
                old_folder.rstrip("/")
                + "/"
            )

            new_prefix = (
                new_folder.rstrip("/")
                + "/"
            )

            # ---------------------------------------------
            # Move entire subtree
            # ---------------------------------------------

            changes = []

            for index, (
                path,
                data
            ) in enumerate(
                self.wfs.entries
            ):

                normalized = normalize_path(
                    path
                )

                if normalized == old_folder:

                    new_path = (
                        new_folder
                        + "/"
                    )

                    changes.append(
                        (
                            index,
                            new_path
                        )
                    )

                elif normalized.startswith(
                    old_prefix
                ):

                    suffix = normalized[
                        len(old_prefix):
                    ]

                    new_path = (
                        new_prefix
                        + suffix
                    )

                    changes.append(
                        (
                            index,
                            new_path
                        )
                    )

            for index, new_path in changes:

                self.wfs.entries[
                    index
                ][0] = new_path

                self.dirty.add(
                    index
                )

            self.refresh()

            return

        # -------------------------------------------------
        # File
        # -------------------------------------------------

        if iid.startswith(
            "file:"
        ):

            index = int(
                iid[5:]
            )

            old_path = self.wfs.entries[
                index
            ][0]

            new_name = simpledialog.askstring(
                "Rename",
                "New file name:",
                initialvalue=base_name(
                    old_path
                ),
                parent=self
            )

            if not new_name:
                return

            new_name = new_name.strip()

            if (
                "/" in new_name
                or "\\" in new_name
            ):

                messagebox.showerror(
                    "Rename",
                    "Invalid file name.",
                    parent=self
                )

                return

            if self.current_path == "/":

                new_path = (
                    "/"
                    + new_name
                )

            else:

                new_path = (
                    self.current_path.rstrip("/")
                    + "/"
                    + new_name
                )

            if self.wfs.find(
                new_path
            ) >= 0:

                messagebox.showerror(
                    "Rename",
                    "A file with that name already exists.",
                    parent=self
                )

                return

            self.wfs.entries[
                index
            ][0] = new_path

            self.dirty.add(
                index
            )

            self.name_var.set(
                new_name
            )

            self.refresh()

    # =====================================================
    # New file
    # =====================================================

    def new_file(self):

        if self.wfs is None:

            messagebox.showinfo(
                "New file",
                "Load an image first.",
                parent=self
            )

            return

        name = simpledialog.askstring(
            "New file",
            "File name:",
            initialvalue="new.txt",
            parent=self
        )

        if not name:
            return

        name = name.strip()

        if (
            "/" in name
            or "\\" in name
        ):

            messagebox.showerror(
                "New file",
                "Enter only the file name.",
                parent=self
            )

            return

        if self.current_path == "/":

            path = (
                "/"
                + name
            )

        else:

            path = (
                self.current_path.rstrip("/")
                + "/"
                + name
            )

        path = normalize_path(
            path
        )

        if self.wfs.find(
            path
        ) >= 0:

            messagebox.showerror(
                "New file",
                "That file already exists.",
                parent=self
            )

            return

        self.wfs.entries.append(
            [
                path,
                bytearray()
            ]
        )

        index = (
            len(self.wfs.entries)
            - 1
        )

        self.dirty.add(
            index
        )

        self.refresh()

        iid = (
            "file:"
            + str(index)
        )

        if self.tree.exists(iid):

            self.tree.selection_set(
                iid
            )

            self.tree.focus(
                iid
            )

            self.on_select()

    # =====================================================
    # New folder
    # =====================================================

    def new_folder(self):

        if self.wfs is None:

            messagebox.showinfo(
                "New folder",
                "Load an image first.",
                parent=self
            )

            return

        name = simpledialog.askstring(
            "New folder",
            "Folder name:",
            initialvalue="New Folder",
            parent=self
        )

        if not name:
            return

        name = name.strip()

        if (
            "/" in name
            or "\\" in name
        ):

            messagebox.showerror(
                "New folder",
                "Enter only the folder name.",
                parent=self
            )

            return

        if self.current_path == "/":

            path = (
                "/"
                + name
                + "/"
            )

        else:

            path = (
                self.current_path.rstrip("/")
                + "/"
                + name
                + "/"
            )

        normalized = normalize_path(
            path
        )

        # Check both explicit folder entries
        # and directories implied by files.
        if self.directory_exists(
            normalized
        ):

            messagebox.showerror(
                "New folder",
                "That folder already exists.",
                parent=self
            )

            return

        self.wfs.entries.append(
            [
                path,
                bytearray()
            ]
        )

        self.dirty.add(
            len(self.wfs.entries) - 1
        )

        self.refresh()

    # =====================================================
    # Delete
    # =====================================================

    def delete_selected(self):

        if self.wfs is None:
            return

        selection = self.tree.selection()

        if not selection:
            return

        targets = []

        for iid in selection:

            if iid.startswith(
                "file:"
            ):

                index = int(
                    iid[5:]
                )

                targets.append(
                    (
                        "file",
                        index
                    )
                )

            elif iid.startswith(
                "folder:"
            ):

                folder = normalize_path(
                    iid[len("folder:"):]
                )

                targets.append(
                    (
                        "folder",
                        folder
                    )
                )

        names = []

        for kind, target in targets:

            if kind == "file":

                names.append(
                    self.wfs.entries[
                        target
                    ][0]
                )

            else:

                names.append(
                    target + "/"
                )

        message = (
            "Delete the following?\n\n"
            + "\n".join(
                names[:15]
            )
        )

        if len(names) > 15:
            message += "\n…"

        if not messagebox.askyesno(
            "Delete",
            message,
            parent=self
        ):
            return

        delete_indices = set()

        for kind, target in targets:

            if kind == "file":

                delete_indices.add(
                    target
                )

            else:

                prefix = (
                    target.rstrip("/")
                    + "/"
                )

                for index, (
                    path,
                    data
                ) in enumerate(
                    self.wfs.entries
                ):

                    normalized = normalize_path(
                        path
                    )

                    if (
                        normalized == target
                        or normalized.startswith(
                            prefix
                        )
                    ):

                        delete_indices.add(
                            index
                        )

        for index in sorted(
            delete_indices,
            reverse=True
        ):

            del self.wfs.entries[
                index
            ]

        self.current_index = None

        self.name_var.set("")

        self.text.delete(
            "1.0",
            tk.END
        )

        self.dirty.clear()

        self.refresh()

    # =====================================================
    # Import
    # =====================================================

    def import_files(self):

        if self.wfs is None:

            messagebox.showinfo(
                "Import",
                "Load an image first.",
                parent=self
            )

            return

        paths = filedialog.askopenfilenames(
            title="Import files",
            parent=self
        )

        if not paths:
            return

        self._import_paths(
            paths
        )

    def _import_paths(
        self,
        paths
    ):

        added = 0

        for host_path in paths:

            if not os.path.isfile(
                host_path
            ):
                continue

            try:

                with open(
                    host_path,
                    "rb"
                ) as f:

                    data = f.read()

            except OSError as e:

                messagebox.showerror(
                    "Import failed",
                    f"{host_path}\n\n{e}",
                    parent=self
                )

                continue

            filename = os.path.basename(
                host_path
            )

            if self.current_path == "/":

                wfs_path = (
                    "/"
                    + filename
                )

            else:

                wfs_path = (
                    self.current_path.rstrip("/")
                    + "/"
                    + filename
                )

            wfs_path = normalize_path(
                wfs_path
            )

            index = self.wfs.find(
                wfs_path
            )

            if index >= 0:

                self.wfs.entries[
                    index
                ][1] = bytearray(
                    data
                )

                self.dirty.add(
                    index
                )

            else:

                self.wfs.entries.append(
                    [
                        wfs_path,
                        bytearray(data)
                    ]
                )

                self.dirty.add(
                    len(self.wfs.entries) - 1
                )

            added += 1

        self.refresh()

        self.status_left.set(
            f"Imported {added} file(s) "
            f"into {self.current_path} — "
            f"remember to Save."
        )

    # =====================================================
    # Export
    # =====================================================

    def export_selected(self):

        if self.wfs is None:
            return

        indices = self.selected_indices()

        if not indices:
            return

        # -------------------------------------------------
        # Single file
        # -------------------------------------------------

        if len(indices) == 1:

            index = indices[0]

            name, data = (
                self.wfs.entries[index]
            )

            output = filedialog.asksaveasfilename(
                title="Export file",
                initialfile=base_name(name),
                parent=self
            )

            if not output:
                return

            try:

                with open(
                    output,
                    "wb"
                ) as f:

                    f.write(data)

            except OSError as e:

                messagebox.showerror(
                    "Export failed",
                    str(e),
                    parent=self
                )

                return

            self.status_left.set(
                f"Exported {name} → {output}"
            )

            return

        # -------------------------------------------------
        # Multiple files
        # -------------------------------------------------

        output_dir = filedialog.askdirectory(
            title="Export files to folder",
            parent=self
        )

        if not output_dir:
            return

        for index in indices:

            name, data = (
                self.wfs.entries[index]
            )

            output = os.path.join(
                output_dir,
                base_name(name)
            )

            with open(
                output,
                "wb"
            ) as f:

                f.write(data)

        self.status_left.set(
            f"Exported {len(indices)} file(s)"
        )

    # =====================================================
    # Drag & Drop
    # =====================================================

    def on_drop(
        self,
        event
    ):

        if self.wfs is None:

            self.status_left.set(
                "Load an image first."
            )

            return

        raw = event.data

        paths = []

        buffer = ""

        in_brace = False

        for char in raw:

            if char == "{":

                in_brace = True

                buffer = ""

            elif char == "}":

                in_brace = False

                if buffer:
                    paths.append(
                        buffer
                    )

                buffer = ""

            elif (
                char == " "
                and not in_brace
            ):

                if buffer:

                    paths.append(
                        buffer
                    )

                    buffer = ""

            else:

                buffer += char

        if buffer:
            paths.append(
                buffer
            )

        paths = [
            path
            for path in paths
            if os.path.isfile(path)
        ]

        if paths:

            self._import_paths(
                paths
            )

    # =====================================================
    # Sorting
    # =====================================================

    def sort_by(
        self,
        column
    ):

        if self.sort_col == column:

            self.sort_rev = (
                not self.sort_rev
            )

        else:

            self.sort_col = column

            self.sort_rev = False

        self.refresh()

    # =====================================================
    # Status
    # =====================================================

    def update_status(self):

        if self.wfs is None:

            self.status_right.set("")

            return

        used = sum(
            len(name)
            + len(data)
            + 6
            for name, data
            in self.wfs.entries
        )

        total = (
            self.wfs.sectors
            * SECTOR
            - SECTOR
        )

        files = 0
        folders = 0

        for name, data in (
            self.wfs.entries
        ):

            if name.endswith("/"):
                folders += 1

            else:
                files += 1

        self.status_right.set(
            f"{files} file(s)  |  "
            f"{folders} folder(s)  |  "
            f"{human_size(used)} / "
            f"{human_size(total)} used"
        )

    # =====================================================
    # Save / inject
    # =====================================================

    def inject(self):

        if self.wfs is None:

            messagebox.showinfo(
                "Save",
                "Load an image first.",
                parent=self
            )

            return

        self.commit_current()

        try:

            self.wfs.inject()

        except Exception as e:

            messagebox.showerror(
                "Save failed",
                str(e),
                parent=self
            )

            return

        self.dirty.clear()

        self.refresh()

        messagebox.showinfo(
            "Saved",
            f"Wrote {len(self.wfs.entries)} "
            f"entries to disk.",
            parent=self
        )

    # =====================================================
    # Preview
    # =====================================================

    def toggle_preview(self):

        if self.preview_var.get():

            self.paned.add(
                self.right,
                weight=2
            )

        else:

            self.paned.forget(
                self.right
            )

    # =====================================================
    # About
    # =====================================================

    def about(self):

        messagebox.showinfo(
            "About",
            "wFs Explorer\n\n"
            "Browse, edit, and inject wFs "
            "files into waveOS disk images.\n\n"
            f"Drag-and-drop: "
            f"{'enabled' if HAVE_DND else 'not available'}",
            parent=self
        )


# =========================================================
# Main
# =========================================================

if __name__ == "__main__":
    App().mainloop()