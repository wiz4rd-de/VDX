#!/usr/bin/env python3
"""Generate the VDX default bank (SPEC §15.3).

Reads banks/default/*.json (one readable file per voice, field "slot" 1..32)
and writes:
  banks/vdx-default.syx        the 4104-byte 32-voice bank dump (format 9)
  core/sysex/default_bank.inc  the same bytes as a C++ initializer list,
                               embedded in vdx_core

Slots without a JSON file hold INIT VOICE (R-P10). Slot 1 must be INIT VOICE.

  python3 tools/make_default_bank.py           regenerate both files
  python3 tools/make_default_bank.py --check   fail if they are out of date
"""
import argparse
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "banks", "default")
SYX_OUT = os.path.join(ROOT, "banks", "vdx-default.syx")
INC_OUT = os.path.join(ROOT, "core", "sysex", "default_bank.inc")

CURVES = {"-LIN": 0, "-EXP": 1, "+EXP": 2, "+LIN": 3}
WAVES = {"TRI": 0, "SAW DN": 1, "SAW UP": 2, "SQU": 3, "SIN": 4, "S/H": 5}
MODES = {"ratio": 0, "fixed": 1}


def init_operator(output_level):
    return {
        "rates": [99, 99, 99, 99], "levels": [99, 99, 99, 0],
        "break_point": 0, "left_depth": 0, "right_depth": 0,
        "left_curve": "-LIN", "right_curve": "-LIN", "rate_scaling": 0,
        "am_sens": 0, "velocity_sens": 0, "output_level": output_level,
        "mode": "ratio", "coarse": 1, "fine": 0, "detune": 0,
    }


def init_voice(slot):
    """INIT VOICE exactly as R-P10."""
    return {
        "slot": slot, "name": "INIT VOICE", "algorithm": 1, "feedback": 0,
        "osc_key_sync": 1, "transpose": 24, "pitch_mod_sens": 3,
        "lfo": {"wave": "TRI", "speed": 35, "delay": 0, "pm_depth": 0, "am_depth": 0, "key_sync": 1},
        "pitch_eg": {"rates": [99, 99, 99, 99], "levels": [50, 50, 50, 50]},
        "operators": {str(n): init_operator(99 if n == 1 else 0) for n in range(1, 7)},
    }


def rng(name, value, lo, hi):
    if not isinstance(value, int) or value < lo or value > hi:
        raise ValueError(f"{name} = {value!r} is outside {lo}..{hi}")
    return value


def unpacked(v):
    """The 155 unpacked bytes (SPEC §4.1). Operator blocks are stored OP6 first."""
    u = [0] * 155
    for n in range(1, 7):
        o = v["operators"][str(n)]
        base = (6 - n) * 21
        p = f"OP{n} "
        for k in range(4):
            u[base + k] = rng(p + "rate", o["rates"][k], 0, 99)
            u[base + 4 + k] = rng(p + "level", o["levels"][k], 0, 99)
        u[base + 8] = rng(p + "break_point", o["break_point"], 0, 99)
        u[base + 9] = rng(p + "left_depth", o["left_depth"], 0, 99)
        u[base + 10] = rng(p + "right_depth", o["right_depth"], 0, 99)
        u[base + 11] = CURVES[o["left_curve"]]
        u[base + 12] = CURVES[o["right_curve"]]
        u[base + 13] = rng(p + "rate_scaling", o["rate_scaling"], 0, 7)
        u[base + 14] = rng(p + "am_sens", o["am_sens"], 0, 3)
        u[base + 15] = rng(p + "velocity_sens", o["velocity_sens"], 0, 7)
        u[base + 16] = rng(p + "output_level", o["output_level"], 0, 99)
        u[base + 17] = MODES[o["mode"]]
        u[base + 18] = rng(p + "coarse", o["coarse"], 0, 31)
        u[base + 19] = rng(p + "fine", o["fine"], 0, 99)
        u[base + 20] = rng(p + "detune", o["detune"], -7, 7) + 7
    for k in range(4):
        u[126 + k] = rng("pitch_eg rate", v["pitch_eg"]["rates"][k], 0, 99)
        u[130 + k] = rng("pitch_eg level", v["pitch_eg"]["levels"][k], 0, 99)
    u[134] = rng("algorithm", v["algorithm"], 1, 32) - 1
    u[135] = rng("feedback", v["feedback"], 0, 7)
    u[136] = rng("osc_key_sync", v["osc_key_sync"], 0, 1)
    lfo = v["lfo"]
    u[137] = rng("lfo speed", lfo["speed"], 0, 99)
    u[138] = rng("lfo delay", lfo["delay"], 0, 99)
    u[139] = rng("lfo pm_depth", lfo["pm_depth"], 0, 99)
    u[140] = rng("lfo am_depth", lfo["am_depth"], 0, 99)
    u[141] = rng("lfo key_sync", lfo["key_sync"], 0, 1)
    u[142] = WAVES[lfo["wave"]]
    u[143] = rng("pitch_mod_sens", v["pitch_mod_sens"], 0, 7)
    u[144] = rng("transpose", v["transpose"], 0, 48)
    name = v["name"]
    if len(name) > 10 or any(not (0x20 <= ord(c) <= 0x7E) for c in name):
        raise ValueError(f"bad voice name {name!r}")
    for k, c in enumerate(name.ljust(10)):
        u[145 + k] = ord(c)
    return u


def packed(u):
    """Pack 155 unpacked bytes into 128 bytes (SPEC §4.6), unused bits 0."""
    p = [0] * 128
    for i in range(6):
        b, q = i * 21, i * 17
        p[q:q + 11] = u[b:b + 11]
        p[q + 11] = (u[b + 12] << 2) | u[b + 11]
        p[q + 12] = (u[b + 20] << 3) | u[b + 13]
        p[q + 13] = (u[b + 15] << 2) | u[b + 14]
        p[q + 14] = u[b + 16]
        p[q + 15] = (u[b + 18] << 1) | u[b + 17]
        p[q + 16] = u[b + 19]
    p[102:110] = u[126:134]
    p[110] = u[134]
    p[111] = (u[136] << 3) | u[135]
    p[112:116] = u[137:141]
    p[116] = (u[143] << 4) | (u[142] << 1) | u[141]
    p[117] = u[144]
    p[118:128] = u[145:155]
    return p


def build():
    voices = {}
    for path in sorted(glob.glob(os.path.join(SRC_DIR, "*.json"))):
        with open(path, encoding="utf-8") as f:
            v = json.load(f)
        slot = rng(os.path.basename(path) + " slot", v["slot"], 1, 32)
        if slot in voices:
            raise ValueError(f"slot {slot} defined twice")
        voices[slot] = v
    if 1 not in voices or unpacked(voices[1]) != unpacked(init_voice(1)):
        raise ValueError("slot 1 must be INIT VOICE exactly as R-P10")
    data = []
    for slot in range(1, 33):
        data += packed(unpacked(voices.get(slot, init_voice(slot))))
    assert len(data) == 4096
    checksum = (-sum(data)) & 0x7F
    return bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00] + data + [checksum, 0xF7])


def inc_text(syx):
    lines = ["// Generated by tools/make_default_bank.py from banks/default/*.json. Do not edit.",
             "// The 4104-byte default bank (banks/vdx-default.syx) as an initializer list."]
    for i in range(0, len(syx), 16):
        lines.append(" ".join(f"0x{b:02X}," for b in syx[i:i + 16]))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="verify the generated files are up to date")
    args = ap.parse_args()
    syx = build()
    assert len(syx) == 4104
    inc = inc_text(syx)
    if args.check:
        ok = True
        if not os.path.exists(SYX_OUT) or open(SYX_OUT, "rb").read() != syx:
            print(f"{SYX_OUT} is out of date", file=sys.stderr)
            ok = False
        if not os.path.exists(INC_OUT) or open(INC_OUT, encoding="utf-8").read() != inc:
            print(f"{INC_OUT} is out of date", file=sys.stderr)
            ok = False
        return 0 if ok else 1
    with open(SYX_OUT, "wb") as f:
        f.write(syx)
    with open(INC_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(inc)
    print(f"wrote {SYX_OUT} ({len(syx)} bytes) and {INC_OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
