#!/usr/bin/env python3
"""Prints the contents of a 6-operator FM voice or bank .syx file.

    python3 tools/syx_inspect.py FILE.syx [--voice N]   # N = slot 1..32 for a full listing

Recognises a 163-byte single voice, a 4104-byte bank and a headerless
4096-byte bank (SPEC §4.6, §4.7, R-D4). Reports checksum mismatches.
"""
import sys

OP_NAMES = ["R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4", "BP", "LD", "RD", "LC", "RC",
            "RS", "AMS", "KVS", "OL", "MODE", "COARSE", "FINE", "DET"]


def checksum(data):
    return (-sum(data)) & 0x7F


def unpack(p):
    """128 packed bytes -> 155 unpacked values (fields only)."""
    v = [0] * 155
    for op in range(6):
        s, d = p[op * 17:op * 17 + 17], op * 21
        v[d:d + 11] = s[0:11]
        v[d + 11], v[d + 12] = s[11] & 3, (s[11] >> 2) & 3
        v[d + 13], v[d + 20] = s[12] & 7, (s[12] >> 3) & 15
        v[d + 14], v[d + 15] = s[13] & 3, (s[13] >> 2) & 7
        v[d + 16] = s[14]
        v[d + 17], v[d + 18] = s[15] & 1, (s[15] >> 1) & 31
        v[d + 19] = s[16]
    g = p[102:128]
    v[126:134] = g[0:8]
    v[134], v[135], v[136] = g[8], g[9] & 7, (g[9] >> 3) & 1
    v[137:141] = g[10:14]
    v[141], v[142], v[143] = g[14] & 1, (g[14] >> 1) & 7, (g[14] >> 4) & 7
    v[144] = g[15]
    v[145:155] = g[16:26]
    return v


def name_of(vals):
    return "".join(chr(b) if 0x20 <= b <= 0x7E else " " for b in vals[145:155])


def dump(v):
    for op in range(1, 7):
        base = (6 - op) * 21
        print(f"  OP{op}: " + " ".join(f"{n}={v[base + i]}" for i, n in enumerate(OP_NAMES)))
    print(f"  PEG R={v[126:130]} L={v[130:134]}  ALG={v[134] + 1} FB={v[135]} OKS={v[136]}")
    print(f"  LFO speed={v[137]} delay={v[138]} pmd={v[139]} amd={v[140]} sync={v[141]} wave={v[142]} "
          f"PMS={v[143]} transpose={v[144]}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    data = open(sys.argv[1], "rb").read()
    want = int(sys.argv[sys.argv.index("--voice") + 1]) if "--voice" in sys.argv else None
    if len(data) == 4096:
        bank, ok = data, True
    elif len(data) >= 163 and data[0] == 0xF0 and data[1] == 0x43 and data[3] == 0 and len(data) == 163:
        body = data[6:161]
        ok = checksum(body) == data[161]
        v = list(body)
        print(f"single voice '{name_of(v)}'" + ("" if ok else "  CHECK SUM ERROR"))
        dump(v)
        return 0
    elif len(data) >= 4104 and data[0] == 0xF0 and data[1] == 0x43 and data[3] == 9:
        bank = data[6:4102]
        ok = checksum(bank) == data[4102]
    else:
        print(f"not a voice or bank file ({len(data)} bytes)")
        return 1
    print(f"bank, 32 voices" + ("" if ok else "  CHECK SUM ERROR"))
    for slot in range(32):
        v = unpack(bank[slot * 128:slot * 128 + 128])
        print(f"{slot + 1:2} {name_of(v)}  ALG {v[134] + 1:2}  FB {v[135]}")
        if want == slot + 1:
            dump(v)
    return 0


if __name__ == "__main__":
    sys.exit(main())
