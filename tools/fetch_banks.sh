#!/usr/bin/env bash
# Downloads community 32-voice banks into the git-ignored banks/external/ for
# local testing only (AC-D3.1 round trips). They are never committed or
# bundled (SPEC §12, §15.3).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$root/banks/external"
url="${VDX_BANKS_URL:-https://github.com/asb2m10/dexed/raw/master/assets/builtin_pgm.zip}"
mkdir -p "$dest"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "fetching $url"
curl -fsSL -o "$tmp/banks.zip" "$url"
(cd "$tmp" && unzip -q -o banks.zip)
find "$tmp" -iname "*.syx" -exec cp {} "$dest/" \;
echo "$(find "$dest" -iname '*.syx' | wc -l | tr -d ' ') banks in banks/external/"
