#!/usr/bin/env python3
import sys
import struct

def main():
    if len(sys.argv) != 3:
        print("usage: bin2coff.py <input.bin> <output.obj>")
        sys.exit(1)

    src, dst = sys.argv[1], sys.argv[2]

    with open(src, "rb") as f:
        data = f.read()

    data_size = len(data)

    # --- Layout ---
    # File header:    20 bytes
    # Section header: 40 bytes
    # Raw data:       data_size bytes
    # Symbol table:   2 * 18 = 36 bytes
    # String table:   variable
    raw_data_offset = 20 + 40

    # Build the string table with the two long symbol names.
    # COFF string table: 4-byte size field, then NUL-terminated strings.
    # The size field includes itself.
    strings = []
    strtab_body = b""
    # Offsets are 1-based relative to the start of the size field.
    # So the first string starts at offset 4.
    strtab_current_offset = 4

    def add_string(s):
        nonlocal strtab_body, strtab_current_offset
        encoded = s.encode("ascii") + b"\x00"
        offset = strtab_current_offset
        strtab_body += encoded
        strtab_current_offset += len(encoded)
        return offset

    name1_off = add_string("_binary_kernel_bin_start")
    name2_off = add_string("_binary_kernel_bin_end")

    strtab_size = 4 + len(strtab_body)
    strtab = struct.pack("<I", strtab_size) + strtab_body

    def make_symbol(name, name_offset, value, section_number, storage_class):
        # If the name is 8 bytes or less, store it inline.
        # Otherwise, write 4 zero bytes + a 4-byte offset into the string table.
        encoded = name.encode("ascii")
        if len(encoded) <= 8:
            name_field = encoded + b"\x00" * (8 - len(encoded))
        else:
            name_field = struct.pack("<II", 0, name_offset)
        return struct.pack(
            "<8sIhHBB",
            name_field,
            value,
            section_number,
            0,
            storage_class,
            0
        )

    symtab = b""
    symtab += make_symbol("_binary_kernel_bin_start", name1_off, 0, 1, 2)
    symtab += make_symbol("_binary_kernel_bin_end", name2_off, data_size, 1, 2)

    symtab_offset = raw_data_offset + data_size

    file_hdr = struct.pack(
        "<HHIIIHH",
        0x8664,      # machine: AMD64
        1,           # number of sections
        0,           # timestamp
        symtab_offset,
        2,           # number of symbols
        0,           # optional header size
        0            # characteristics
    )

    sec_hdr = struct.pack(
        "<8sIIIIIIHHI",
        b".data\x00\x00\x00",
        0,                    # physical address
        0,                    # virtual address
        data_size,            # size of raw data
        raw_data_offset,      # file offset of raw data
        0, 0, 0, 0,           # reloc/line pointers and counts
        0x40000040            # characteristics: initialized data, read
    )

    with open(dst, "wb") as f:
        f.write(file_hdr)
        f.write(sec_hdr)
        f.write(data)
        f.write(symtab)
        f.write(strtab)

    total = len(file_hdr) + len(sec_hdr) + data_size + len(symtab) + len(strtab)
    print(f"wrote {dst}: {total} bytes")

if __name__ == "__main__":
    main()