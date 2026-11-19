#!/usr/bin/env python3
"""mklha: an LHA archive of files and drawers, stored (-lh0-) with level-2
headers, which every LHA and unarchiver on AROS and AmigaOS reads.

usage: mklha.py archive.lha path...   (paths relative to the current drawer)
"""
import os
import struct
import sys


def crc16(data, crc=0):
    """CRC-16/ARC (polynomial 0xA001), as LHA uses."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def ext(kind, payload):
    return struct.pack('<HB', 3 + len(payload), kind) + payload


def header(method, size, mtime, data_crc, name, dirname):
    exts = ext(0x00, b'\0\0')                 # header CRC, filled in below
    if name:
        exts += ext(0x01, name)
    if dirname:
        exts += ext(0x02, dirname)
    exts += b'\0\0'                           # no more extended headers
    base = struct.pack('<H5sIIIBBHBH', 0, method, size, size, int(mtime), 0x20, 2, data_crc, ord('U'), 0)
    # the first extended header's size goes in the base's last field
    first = struct.unpack('<H', exts[:2])[0]
    base = base[:-2] + struct.pack('<H', first)
    hdr = bytearray(base + exts[2:])
    struct.pack_into('<H', hdr, 0, len(hdr))
    # the header CRC: over the whole header with its own field zero
    at = len(base) + 1
    struct.pack_into('<H', hdr, at, crc16(bytes(hdr)))
    return bytes(hdr)


def entries(paths):
    for p in paths:
        if os.path.isdir(p):
            yield p, True
            for root, dirs, files in os.walk(p):
                dirs.sort()
                for d in dirs:
                    yield os.path.join(root, d), True
                for f in sorted(files):
                    yield os.path.join(root, f), False
        else:
            yield p, False


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], 'wb') as out:
        for path, is_dir in entries(sys.argv[2:]):
            parts = path.replace(os.sep, '/').split('/')
            mtime = os.path.getmtime(path)
            if is_dir:
                dirname = b'\xff'.join(p.encode('latin-1') for p in parts) + b'\xff'
                out.write(header(b'-lhd-', 0, mtime, 0, b'', dirname))
                continue
            data = open(path, 'rb').read()
            dirname = b'\xff'.join(p.encode('latin-1') for p in parts[:-1]) + b'\xff' if len(parts) > 1 else b''
            out.write(header(b'-lh0-', len(data), mtime, crc16(data), parts[-1].encode('latin-1'), dirname))
            out.write(data)
        out.write(b'\0')


if __name__ == '__main__':
    main()
