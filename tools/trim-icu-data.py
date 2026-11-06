#!/usr/bin/env python3
"""Rebuilds the bundled ICU data library with only what Qt Core uses.

Qt Core calls ICU for case conversion, collation (sorting) and time zones.
Everything else in the data (converter tables, line breaking, transliteration,
locale names of languages, currencies, units and regions, spelled-out numbers)
is never read by melonRA, and is about two thirds of the library.

usage: trim-icu-data.py <libicudata.so.N> <output directory>
Writes <output directory>/icudt.dat and icudt.S, ready to be assembled into a
library with the same soname.
"""
import os
import struct
import subprocess
import sys

# top-level trees and kinds of item to leave out
DROP_TREES = {"brkitr", "curr", "lang", "rbnf", "region", "translit", "unit", "zone"}
DROP_SUFFIXES = (".cnv",)
# converter tables still kept: the Windows code pages (1250 to 1258)
KEEP_PREFIXES = tuple(f"ibm-{cp}_" for cp in range(5346, 5355))


def read_blob(path):
    syms = subprocess.run(["nm", "-D", "--defined-only", "-S", path],
                          capture_output=True, text=True, check=True).stdout
    for line in syms.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3].startswith("icudt") and parts[3].endswith("_dat"):
            vaddr, size, name = int(parts[0], 16), int(parts[1], 16), parts[3]
            break
    else:
        sys.exit("no icudt*_dat symbol in " + path)
    # map the symbol's address to a file offset through the program headers
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(phnum):
        p_type, _, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIQQQQ", data, phoff + i * phentsize)
        if p_type == 1 and p_vaddr <= vaddr < p_vaddr + p_filesz:
            off = vaddr - p_vaddr + p_offset
            return name, data[off:off + size]
    sys.exit("couldn't locate the data in " + path)


def trim(blob):
    hs, = struct.unpack_from("<H", blob, 0)
    header, toc = blob[:hs], blob[hs:]
    count, = struct.unpack_from("<I", toc, 0)
    entries = [struct.unpack_from("<II", toc, 4 + 8 * i) for i in range(count)]
    items = []
    for i, (name_off, data_off) in enumerate(entries):
        end = entries[i + 1][1] if i + 1 < count else len(toc)
        name = toc[name_off:toc.index(b"\0", name_off)].decode()
        items.append((name, toc[data_off:end]))

    def keep(name):
        path = name.split("/", 1)[1] if "/" in name else name
        tree = path.split("/")[0] if "/" in path else None
        if path.startswith(KEEP_PREFIXES):
            return True
        return tree not in DROP_TREES and not path.endswith(DROP_SUFFIXES)

    kept = [it for it in items if keep(it[0])]
    # same layout as the original: count, (name offset, data offset) pairs,
    # the names, then each item aligned to 16 bytes
    names = b"".join(n.encode() + b"\0" for n, _ in kept)
    table_size = 4 + 8 * len(kept)
    pos = table_size + len(names)
    pos += (-(hs + pos)) % 16
    body, offsets = bytearray(), []
    for _, data in kept:
        offsets.append(pos + len(body))
        body += data
        body += b"\0" * ((-(hs + pos + len(body))) % 16)
    out = bytearray(struct.pack("<I", len(kept)))
    noff = table_size
    for (n, _), doff in zip(kept, offsets):
        out += struct.pack("<II", noff, doff)
        noff += len(n.encode()) + 1
    out += names
    out += b"\0" * (pos - len(out))
    out += body
    return header + bytes(out), len(items), len(kept)


def main():
    lib, outdir = sys.argv[1], sys.argv[2]
    symbol, blob = read_blob(lib)
    trimmed, total, kept = trim(blob)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "icudt.dat"), "wb") as f:
        f.write(trimmed)
    with open(os.path.join(outdir, "icudt.S"), "w") as f:
        f.write(f'.section .rodata\n.globl {symbol}\n.type {symbol}, @object\n.balign 16\n'
                f'{symbol}:\n.incbin "icudt.dat"\n.size {symbol}, . - {symbol}\n'
                '.section .note.GNU-stack,"",@progbits\n')
    print(f"{symbol}: kept {kept} of {total} items, {len(blob) / 1e6:.1f} MB -> {len(trimmed) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
