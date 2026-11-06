#!/bin/bash
# Packages the macOS build as one installer, VDX-<version>-macOS.pkg (D-PL17).
# Usage: tools/package/macos.sh <artefacts-dir> <version> <out-dir>
#   <artefacts-dir> is plugin/VDX_artefacts/Release in the build tree, after
#   tools/package/notices.py --install (and tools/sign/macos.sh when signing).
#
# Bundles without a Developer ID signature are signed ad hoc here: Apple
# silicon runs no unsigned code, and the linker's signature does not seal the
# app bundle's resources. The package itself is unsigned, so macOS asks once
# (System Settings > Privacy & Security > Open Anyway). Files that Installer
# writes carry no quarantine flag, so hosts load the installed plugins.
set -euo pipefail
art="${1:?usage: macos.sh <artefacts-dir> <version> <out-dir>}"
version="${2:?version}"
out="${3:?out dir}"
id=io.github.wiz4rd-de.vdx
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"

names=(app vst3 au)
bundles=("$art/Standalone/VDX.app" "$art/VST3/VDX.vst3" "$art/AU/VDX.component")
targets=(/Applications /Library/Audio/Plug-Ins/VST3 /Library/Audio/Plug-Ins/Components)

for i in 0 1 2; do
  b="${bundles[$i]}"
  [[ -d "$b" ]] || { echo "macos.sh: missing $b" >&2; exit 1; }
  [[ -f "$b/Contents/Resources/NOTICES.txt" ]] ||
    { echo "macos.sh: $b has no NOTICES.txt; run tools/package/notices.py --install first" >&2; exit 1; }
  if ! codesign -dvv "$b" 2>&1 | grep -q '^Authority=Developer ID Application'; then
    codesign --force --deep --sign - "$b"
  fi
  codesign --verify --deep --strict "$b"
  echo "macos.sh: $(basename "$b"): $(lipo -archs "$b/Contents/MacOS/"* 2>/dev/null | head -1)"

  root="$work/root-${names[$i]}"
  mkdir -p "$root"
  ditto --norsrc --noextattr --noqtn "$b" "$root/$(basename "$b")"
  plist="$work/${names[$i]}.plist"
  pkgbuild --analyze --root "$root" "$plist" >/dev/null
  # Install exactly where the package says (pkgbuild would otherwise follow a
  # copy the user moved elsewhere, and skip a bundle whose version looks older).
  python3 -I - "$plist" <<'EOF'
import plistlib, sys
path = sys.argv[1]
with open(path, "rb") as f:
    entries = plistlib.load(f)
for e in entries:
    e["BundleIsRelocatable"] = False
    e["BundleIsVersionChecked"] = False
with open(path, "wb") as f:
    plistlib.dump(entries, f)
EOF
  pkgbuild --root "$root" --component-plist "$plist" --identifier "$id.${names[$i]}" \
    --version "$version" --install-location "${targets[$i]}" "$work/${names[$i]}.pkg" >/dev/null
done

mkdir -p "$work/resources"
cat > "$work/resources/README.txt" <<EOF
VDX $version

This installer puts:
  VDX.app (standalone app)       in /Applications
  VDX.vst3 (VST3 plugin)         in /Library/Audio/Plug-Ins/VST3
  VDX.component (Audio Unit)     in /Library/Audio/Plug-Ins/Components
"Customize" on the next page lets you leave any of them out.
To uninstall, delete those items.

VDX is released under the MIT License. Each installed item contains
Contents/Resources/NOTICES.txt with the licences of VDX and of the
third-party code built into it.

https://github.com/wiz4rd-de/VDX
EOF

cat > "$work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>VDX $version</title>
    <readme file="README.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <volume-check>
        <allowed-os-versions>
            <os-version min="13.0"/>
        </allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="app" title="VDX app" description="The standalone app, in /Applications.">
        <pkg-ref id="$id.app"/>
    </choice>
    <choice id="vst3" title="VST3 plugin" description="VDX.vst3, in /Library/Audio/Plug-Ins/VST3.">
        <pkg-ref id="$id.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit plugin" description="VDX.component, in /Library/Audio/Plug-Ins/Components.">
        <pkg-ref id="$id.au"/>
    </choice>
    <pkg-ref id="$id.app" version="$version" onConclusion="none">app.pkg</pkg-ref>
    <pkg-ref id="$id.vst3" version="$version" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="$id.au" version="$version" onConclusion="none">au.pkg</pkg-ref>
</installer-gui-script>
EOF

pkg="$out/VDX-$version-macOS.pkg"
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" \
  --package-path "$work" "$pkg" >/dev/null
echo "macos.sh: wrote $pkg"
