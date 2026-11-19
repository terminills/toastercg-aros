#!/usr/bin/env python3
"""Find the struct sizes that let NewBook.c's v4 loader walk a real book to END!."""
import struct, sys, itertools

def walk(d, attr, lineattr, draw, page=12):
    if d[:15] != b"ToasterBookFile" or d[15:16] != b"4":
        return None
    p, stats = 16, {"FONT": 0, "BRSH": 0, "DRAW": 0, "PAGE": 0, "LINE": 0, "attrs": 0}
    while p + 4 <= len(d):
        t = d[p:p+4]; p += 4
        if t in (b"FONT", b"DRAW", b"BRSH"):
            ident, size = struct.unpack(">HH", d[p:p+4]); p += 4
            if t in (b"FONT", b"DRAW"):
                p += 2
            if t == b"FONT":
                p += 4
            if t == b"DRAW":
                p += draw
            p += size
        elif t == b"PAGE":
            p += 2 + page
        elif t == b"LINE":
            n, = struct.unpack(">H", d[p:p+2]); p += 2
            for _ in range(n):
                hasattr_ = d[p+3]; p += 4
                if hasattr_:
                    p += attr; stats["attrs"] += 1
            p += lineattr
        elif t == b"END!":
            return stats if p == len(d) else None
        else:
            return None
        stats[t.decode()] += 1
    return None

DRAW = int(__import__("os").environ.get("DRAWSZ", "0"))
books = [open(f, "rb").read() for f in sys.argv[1:]]
for attr, lineattr in itertools.product(range(2, 81, 2), range(2, 81, 2)):
    res = [walk(b, attr, lineattr, DRAW) for b in books]
    if all(res):
        print("Attributes=%d LINE_ATTR_SIZE=%d" % (attr, lineattr), res)
