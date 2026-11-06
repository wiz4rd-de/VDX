#!/usr/bin/env python3
"""Writes NOTICES.txt for a VDX binary distribution (D-PL17).

The file holds VDX's own licence and the licence text of every third-party
component compiled into the plugin and standalone builds: the components in
docs/LICENSES.md, plus the code that JUCE vendors in the modules VDX links.
That JUCE list comes from JUCE's own SPDX inventory (third_party/JUCE/JUCE.spdx.json)
and the module dependency closure, so it follows JUCE updates by itself; the
script fails when it cannot find a licence file, rather than ship an
incomplete notice.

    python3 tools/package/notices.py --out NOTICES.txt
    python3 tools/package/notices.py --install build/plugin/plugin/VDX_artefacts/Release
    python3 tools/package/notices.py --install <dir> --webview2-license <NuGet package>/LICENSE.txt

--install writes NOTICES.txt into each built bundle (app, VST3, AU) or next to
the standalone executable, before signing, so the notice travels with every
binary however it is distributed.
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JUCE = ROOT / "third_party" / "JUCE"

# Vendored JUCE packages inside linked modules that are not compiled into VDX.
NOT_COMPILED = {
    "ASIO SDK": "JUCE_ASIO is off",
    "Oboe": "Android only",
    "AAX SDK": "VDX builds no AAX",
    "LV2": "plugin hosting is off",
    "lilv": "plugin hosting is off",
    "serd": "plugin hosting is off",
    "sord": "plugin hosting is off",
    "sratom": "plugin hosting is off",
    "pslextensions": "plugin hosting is off",
}

LICENCE_NAMES = ["LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING", "COPYING.txt"]

RULE = "-" * 79


def fail(msg: str) -> None:
    print(f"notices: {msg}", file=sys.stderr)
    sys.exit(1)


def read(path: Path) -> str:
    if not path.is_file():
        fail(f"missing licence file {path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}")
    return path.read_text(encoding="utf-8", errors="replace").strip()


def vdx_version() -> str:
    m = re.search(r"project\(VDX VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text())
    return m.group(1) if m else "unknown"


def juce_version() -> str:
    m = re.search(r"project\(JUCE VERSION ([0-9.]+)", (JUCE / "CMakeLists.txt").read_text())
    return m.group(1) if m else "unknown"


def linked_modules() -> set:
    text = (ROOT / "plugin" / "CMakeLists.txt").read_text()
    start = {m for m in re.findall(r"juce::(juce_[a-z0-9_]+)", text) if not m.startswith("juce_recommended")}
    start.add("juce_audio_plugin_client")  # the format wrappers
    seen, todo = set(), sorted(start)
    while todo:
        mod = todo.pop()
        if mod in seen:
            continue
        header = JUCE / "modules" / mod / f"{mod}.h"
        if not header.is_file():
            fail(f"JUCE module header not found: {header}")
        seen.add(mod)
        m = re.search(r"^\s*dependencies:(.*)$", header.read_text(errors="replace"), re.M)
        if m:
            todo += [d for d in re.split(r"[\s,]+", m.group(1).strip()) if d]
    return seen


def licence_file(directory: Path) -> Path:
    for name in LICENCE_NAMES:
        if (directory / name).is_file():
            return directory / name
    for f in sorted(directory.iterdir()):
        if f.is_file() and re.search(r"licen[cs]e", f.name, re.I):
            return f
    if (directory / "README").is_file():  # the IJG terms live in jpglib/README
        return directory / "README"
    fail(f"no licence file in {directory.relative_to(ROOT)}")


def juce_vendored(modules: set) -> list:
    spdx = json.loads((JUCE / "JUCE.spdx.json").read_text())
    out = []
    for p in spdx["packages"]:
        m = re.match(r"Vendored at (modules/(juce_[a-z0-9_]+)\S*) in the JUCE source tree", p.get("sourceInfo", ""))
        if not m or m.group(2) not in modules or p["name"] in NOT_COMPILED:
            continue
        if "LicenseRef-JUCE-Commercial" in p.get("licenseConcluded", ""):
            continue  # JUCE's own code (e.g. @juce-framework/webview): the JUCE section covers it
        directory = JUCE / m.group(1)
        if not directory.is_dir():
            # a single vendored file (e.g. a header): its licence is in the file itself
            fail(f"{p['name']}: {m.group(1)} is not a directory; add it to the script")
        out.append((p["name"], p.get("versionInfo", ""), p.get("licenseConcluded", ""), licence_file(directory)))
    return out


def section(title: str, licence: str, body: str) -> str:
    return f"{RULE}\n{title}\nLicence: {licence}\n{RULE}\n\n{body}\n\n"


def build(webview2_license) -> str:
    ui = ROOT / "ui" / "node_modules"
    msfa = ROOT / "core" / "fm" / "third_party" / "msfa"
    fonts = ROOT / "ui" / "src" / "fonts"
    parts = [
        f"VDX {vdx_version()}: licence notices\n\n"
        "VDX's own code is released under the MIT License (first section).\n"
        "The other sections are the third-party components compiled into this\n"
        "build, each under its own licence, which the MIT License does not cover.\n"
        "docs/LICENSES.md in the source repository lists them by use.\n\n",
        section("VDX", "MIT", read(ROOT / "LICENSE")),
        section(
            f"JUCE {juce_version()}",
            "JUCE 9 End User Licence Agreement (Starter)",
            "VDX uses the JUCE framework under the JUCE 9 End User Licence Agreement,\n"
            "https://juce.com/legal/juce-9-licence/. JUCE is not covered by the MIT\n"
            "License above. Code that JUCE vendors follows under its own licences.",
        ),
    ]
    for name, version, lic, path in juce_vendored(linked_modules()):
        parts.append(section(f"{name} {version} (vendored by JUCE)", lic, read(path)))
    parts += [
        section("MSFA (Music Synthesizer for Android, via Dexed)", "Apache-2.0",
                read(msfa / "NOTICE") + "\n\n" + read(msfa / "LICENSE-2.0.txt")),
        section("r8brain-free-src", "MIT", read(ROOT / "third_party" / "r8brain" / "LICENSE")),
        section("Svelte", "MIT", read(ui / "svelte" / "LICENSE.md")),
        section("clsx (Svelte runtime dependency)", "MIT", read(ui / "clsx" / "license")),
        section("esm-env (Svelte runtime dependency)", "MIT", read(ui / "esm-env" / "LICENSE")),
        section("Barlow Condensed", "SIL OFL 1.1", read(fonts / "OFL-BarlowCondensed.txt")),
        section("Oswald", "SIL OFL 1.1", read(fonts / "OFL-Oswald.txt")),
    ]
    if webview2_license:
        parts.append(section("Microsoft WebView2 SDK (static loader, Windows)",
                             "BSD-3-Clause", read(Path(webview2_license))))
    return "".join(parts).rstrip() + "\n"


def install(text: str, artefacts: Path) -> None:
    targets = []
    for bundle in ("Standalone/VDX.app", "VST3/VDX.vst3", "AU/VDX.component"):
        if (artefacts / bundle).is_dir():
            targets.append(artefacts / bundle / "Contents" / "Resources")
    if not (artefacts / "Standalone" / "VDX.app").exists() and (artefacts / "Standalone").is_dir():
        targets.append(artefacts / "Standalone")  # Windows/Linux: next to the executable
    if not targets:
        fail(f"no VDX bundles under {artefacts}")
    for t in targets:
        t.mkdir(parents=True, exist_ok=True)
        (t / "NOTICES.txt").write_text(text, encoding="utf-8")
        print(f"notices: wrote {t / 'NOTICES.txt'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=Path, help="write NOTICES.txt to this path")
    ap.add_argument("--install", type=Path, help="VDX_artefacts/<config> directory to install NOTICES.txt into")
    ap.add_argument("--webview2-license", help="LICENSE.txt of the Microsoft.Web.WebView2 NuGet package (Windows);"
                    " its NOTICE.txt covers the SDK's .NET code generator, not the static loader")
    args = ap.parse_args()
    if not args.out and not args.install:
        ap.error("give --out and/or --install")
    text = build(args.webview2_license)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"notices: wrote {args.out}")
    if args.install:
        install(text, args.install)
    return 0


if __name__ == "__main__":
    sys.exit(main())
