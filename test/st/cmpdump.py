#!/usr/bin/env python3
"""cmpdump.py A.dump B.dump -- compare st_host's and MAME's frames.

Prints how many frames matched in each part (RAM, vector RAM, the USB's
RAMs, the three CPUs' registers and RAM) and the first frame and offset
where each part differs. Exit status 1 if any does.
"""
import sys

PARTS = [('RAM C800-CFFF', 0x800), ('vector RAM', 0x1000), ('USB program RAM', 0x1000), ('USB work RAM', 0x400),
         ('main CPU registers', 32), ('USB CPU registers', 8), ('USB CPU RAM', 64),
         ('speech CPU registers', 8), ('speech CPU RAM', 64)]
REC = 8 + sum(n for _, n in PARTS)

def frames(name):
    d = open(name, 'rb').read()
    for i in range(0, len(d) - REC + 1, REC):
        assert d[i:i + 4] == b'STFR', 'bad record at %d' % i
        yield int.from_bytes(d[i + 4:i + 8], 'little'), d[i + 8:i + REC]

def main():
    a, b = dict(frames(sys.argv[1])), dict(frames(sys.argv[2]))
    common = sorted(set(a) & set(b))
    if not common:
        print('no frames in common'); sys.exit(1)
    print('frames compared: %d (%d..%d)' % (len(common), common[0], common[-1]))
    bad = 0
    off = 0
    for name, n in PARTS:
        diff = [f for f in common if a[f][off:off + n] != b[f][off:off + n]]
        line = '  %-22s %d/%d frames identical' % (name, len(common) - len(diff), len(common))
        if diff:
            f = diff[0]
            x, y = a[f][off:off + n], b[f][off:off + n]
            j = next(k for k in range(n) if x[k] != y[k])
            line += ', first difference frame %d offset %04X (%02X vs %02X)' % (f, j, x[j], y[j])
            bad = 1
        print(line)
        off += n
    sys.exit(bad)

main()
