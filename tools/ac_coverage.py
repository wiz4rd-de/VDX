#!/usr/bin/env python3
"""Lists every acceptance criterion of SPEC.md §13 and the tests that name it.

    python3 tools/ac_coverage.py            # table + missing list, exit 1 if any AC has no test
    python3 tools/ac_coverage.py --missing  # only the missing IDs
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEST_GLOBS = [
    "tests/**/*.cpp",
    "plugin/tests/**/*.cpp",
    "ui/tests/**/*.ts",
    "ui/tests/**/*.mjs",
    "ui/src/**/*.test.ts",
    "ui/e2e/**/*.ts",
    "tests/CMakeLists.txt",
    "tools/bench/CMakeLists.txt",
    "tools/clean_checkout.sh",
    "ui/scripts/*.mjs",
]


def spec_ids():
    text = (ROOT / "SPEC.md").read_text()
    start = text.index("## 13. Acceptance criteria")
    end = text.index("### 13.6 Traceability")
    ids = []
    for m in re.finditer(r"^\| (AC-[A-Z0-9.]+)", text[start:end], re.M):
        if m.group(1) not in ids:
            ids.append(m.group(1))
    return ids


def test_names():
    found = {}
    for g in TEST_GLOBS:
        for f in ROOT.glob(g):
            if "node_modules" in f.parts:
                continue
            try:
                t = f.read_text(errors="replace")
            except OSError:
                continue
            for m in re.finditer(r"AC-[A-Z]+[0-9]*(?:\.[0-9]+)?", t):
                found.setdefault(m.group(0), set()).add(str(f.relative_to(ROOT)))
    return found


def main():
    ids = spec_ids()
    found = test_names()
    missing = [i for i in ids if i not in found]
    if "--missing" not in sys.argv:
        for i in ids:
            where = ", ".join(sorted(found.get(i, [])))
            print(f"{i:10} {'MISSING' if i in missing else where}")
        print(f"\n{len(ids) - len(missing)}/{len(ids)} acceptance criteria have a named test")
    if missing:
        print("missing:", " ".join(missing))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
