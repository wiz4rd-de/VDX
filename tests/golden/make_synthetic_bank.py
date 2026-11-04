#!/usr/bin/env python3
"""Writes tests/golden/synthetic_bank.syx, the AC-D3.1 round-trip fixture.

A valid 4104-byte bank dump (correct checksum) whose voices contain
out-of-range values, stray bits in every packed bit-field byte, and name
bytes 0x00 and 0x7F. Deterministic (fixed seed); committed, not generated
at test time.
"""
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
rnd = random.Random(1)
data = []
for slot in range(32):
    v = [rnd.randrange(0, 128) for _ in range(128)]
    if slot == 0:
        for op in range(6):
            q = op * 17
            v[q + 0], v[q + 1] = 120, 127      # EG rates > 99
            v[q + 8] = 127                     # break point > 99
            v[q + 11] = 0x7F                   # RC/LC + stray bits 4..6
            v[q + 12] = 0x7F                   # DET 15 (> 14) + RS 7
            v[q + 13] = 0x7F                   # KVS/AMS + stray bits 5..6
            v[q + 14] = 127                    # output level > 99
            v[q + 15] = 0x7F                   # COARSE/MODE + stray bit 6
            v[q + 16] = 127                    # fine > 99
        v[110] = 0x7F                          # algorithm 127
        v[111] = 0x7F                          # OKS/FB + stray bits 4..6
        v[116] = 0x7F                          # PMS 7, WAVE 7 (> 5), LKS 1
        v[117] = 0x7F                          # transpose 127
        v[118:128] = [0x00, 0x7F, 0x41, 0x00, 0x7F, 0x42, 0x20, 0x7F, 0x00, 0x43]
    elif slot == 1:
        v[118:128] = [0x00] * 10
    elif slot == 2:
        v[118:128] = [0x7F] * 10
    data += v
checksum = (-sum(data)) & 0x7F
syx = bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00] + data + [checksum, 0xF7])
assert len(syx) == 4104
with open(os.path.join(HERE, "synthetic_bank.syx"), "wb") as f:
    f.write(syx)
