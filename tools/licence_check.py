#!/usr/bin/env python3
"""VDX licence and branding checks (SPEC R-L3, R-L4, R-L5).

    python3 tools/licence_check.py              # R-L3: no copyleft licence text in tracked files
    python3 tools/licence_check.py --branding   # R-L5: no vendor names in ui/src, plugin/, plugin metadata
    python3 tools/licence_check.py --licenses   # R-L4: docs/LICENSES.md names every shipped component
    python3 tools/licence_check.py --self-test  # all of the above + proves a planted phrase is caught

Exit status 0 = pass, 1 = fail.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Built by concatenation so this file does not itself contain the phrases.
PHRASES = ["GNU General " + "Public License", "GNU " + "Affero"]

# R-L3 exclusions: the JUCE submodule, the optional oracle, docs/LICENSES.md
# (which names licences) and SPEC.md (whose R-L3 quotes the phrases; DECISIONS.md D-L1).
EXCLUDE_PREFIXES = ["third_party/JUCE", "tools/oracle/"]
EXCLUDE_FILES = {"docs/LICENSES.md", "SPEC.md"}

BRAND_WORDS = ["YAMAHA", "Yamaha", "DX7"]
BRAND_ALLOWED = {
    # the bank manager's file-filter description may say "DX7 SysEx files"
    "DX7 SysEx files",
}


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    files = [f for f in out.decode("utf-8", "replace").split("\0") if f]
    return [f for f in files if (ROOT / f).is_file()]


def excluded(path: str) -> bool:
    return path in EXCLUDE_FILES or any(path == p.rstrip("/") or path.startswith(p) for p in EXCLUDE_PREFIXES)


def find_phrases(paths):
    hits = []
    for rel, full in paths:
        try:
            text = pathlib.Path(full).read_bytes().decode("utf-8", "replace")
        except OSError:
            continue
        for p in PHRASES:
            if p in text:
                hits.append((rel, p))
    return hits


def licence_check(extra=None) -> bool:
    paths = [(f, ROOT / f) for f in tracked_files() if not excluded(f)]
    if extra:
        paths += extra
    hits = find_phrases(paths)
    for rel, p in hits:
        print(f"licence check: '{p}' found in {rel}")
    if not hits:
        print(f"licence check: {len(paths)} files clean")
    return not hits


def branding_check() -> bool:
    ok = True
    roots = [ROOT / "ui" / "src", ROOT / "plugin"]
    files = []
    for r in roots:
        if r.exists():
            files += [p for p in r.rglob("*") if p.is_file()]
    # built plugin metadata, when present
    build = ROOT / "build"
    if build.exists():
        for pat in ("**/VDX.vst3/Contents/Info.plist", "**/VDX.vst3/Contents/Resources/moduleinfo.json",
                    "**/VDX.component/Contents/Info.plist", "**/VDX.app/Contents/Info.plist"):
            files += [p for p in build.glob(pat) if p.is_file()]
    for f in files:
        try:
            text = f.read_bytes().decode("utf-8", "replace")
        except OSError:
            continue
        for allowed in BRAND_ALLOWED:
            text = text.replace(allowed, "")
        for w in BRAND_WORDS:
            if w in text:
                print(f"branding check: '{w}' found in {f.relative_to(ROOT)}")
                ok = False
    if ok:
        print(f"branding check: {len(files)} files clean")
    return ok


REQUIRED_LICENSE_ENTRIES = [
    ("JUCE", "Starter"),
    ("VST3 SDK", "MIT"),
    ("MSFA", "Apache-2.0"),
    ("r8brain", "MIT"),
    ("Svelte", "MIT"),
    ("Barlow Condensed", "OFL"),
    ("Oswald", "OFL"),
]


def licenses_doc_check() -> bool:
    doc = ROOT / "docs" / "LICENSES.md"
    if not doc.exists():
        print("licenses doc: docs/LICENSES.md missing")
        return False
    lines = doc.read_text().splitlines()
    ok = True
    for name, lic in REQUIRED_LICENSE_ENTRIES:
        if not any(name in l and lic in l for l in lines):
            print(f"licenses doc: no line names {name} with {lic}")
            ok = False
    if not any("NOTICE" in l and "MSFA" in l for l in lines):
        print("licenses doc: MSFA NOTICE not referenced")
        ok = False
    if "Build and test tools" not in doc.read_text():
        print("licenses doc: build and test tools are not listed separately")
        ok = False
    if ok:
        print("licenses doc: all R-L4 entries present")
    return ok


def self_test() -> bool:
    ok = licence_check() and branding_check() and licenses_doc_check()
    # Plant the phrase in a copy of a first-party file: the check must fail.
    with tempfile.TemporaryDirectory() as d:
        planted = pathlib.Path(d) / "planted.cpp"
        planted.write_text("// " + PHRASES[0] + "\n" + (ROOT / "core" / "core.h").read_text())
        if licence_check(extra=[("core/planted.cpp", planted)]):
            print("self-test: planted phrase was NOT detected")
            ok = False
        else:
            print("self-test: planted phrase detected as expected")
    return ok


def main() -> int:
    if "--self-test" in sys.argv:
        return 0 if self_test() else 1
    if "--branding" in sys.argv:
        return 0 if branding_check() else 1
    if "--licenses" in sys.argv:
        return 0 if licenses_doc_check() else 1
    return 0 if licence_check() else 1


if __name__ == "__main__":
    sys.exit(main())
