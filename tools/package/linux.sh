#!/bin/bash
# Packages the Linux build as VDX-<version>-Linux-x64.tar.gz (D-PL17).
# Usage: tools/package/linux.sh <artefacts-dir> <version> <out-dir>
#   <artefacts-dir> is plugin/VDX_artefacts/Release in the build tree, after
#   tools/package/notices.py --install.
set -euo pipefail
art="${1:?usage: linux.sh <artefacts-dir> <version> <out-dir>}"
version="${2:?version}"
out="${3:?out dir}"
name="VDX-$version-Linux-x64"
for p in "$art/Standalone/VDX" "$art/Standalone/NOTICES.txt" "$art/VST3/VDX.vst3/Contents/Resources/NOTICES.txt"; do
  [[ -e "$p" ]] || { echo "linux.sh: missing $p (build, then run tools/package/notices.py --install)" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name" "$out"
cp "$art/Standalone/VDX" "$art/Standalone/NOTICES.txt" "$work/$name/"
cp -R "$art/VST3/VDX.vst3" "$work/$name/"
cat > "$work/$name/INSTALL.txt" <<EOF
VDX $version for Linux x64

Built and tested on Ubuntu 24.04. Other distributions need glibc 2.39 and
libstdc++ from GCC 13 or newer. The interface needs WebKitGTK 4.1:
    sudo apt install libwebkit2gtk-4.1-0

Standalone app: run ./VDX
VST3 plugin:    copy VDX.vst3 into ~/.vst3 (for you) or /usr/lib/vst3 (for everyone)
    mkdir -p ~/.vst3 && cp -R VDX.vst3 ~/.vst3/

NOTICES.txt holds the licences of VDX and of the third-party code built into it.
https://github.com/wiz4rd-de/VDX
EOF
owner=()
tar --version 2>/dev/null | grep -q GNU && owner=(--owner=0 --group=0)
tar -C "$work" ${owner[@]+"${owner[@]}"} -czf "$out/$name.tar.gz" "$name"
echo "linux.sh: wrote $out/$name.tar.gz"
