#!/bin/bash
# Signs and notarises the macOS bundles (CI only runs this when the secrets
# exist). Usage: tools/sign/macos.sh <build-dir>
# Env: MACOS_CERTIFICATE (base64 .p12), MACOS_CERTIFICATE_PASSWORD,
#      MACOS_NOTARY_KEY (base64 App Store Connect API key, JSON with key_id/issuer_id/key).
set -euo pipefail
build="${1:-build}"
kc="$RUNNER_TEMP/vdx-signing.keychain-db"
echo "$MACOS_CERTIFICATE" | base64 --decode > "$RUNNER_TEMP/cert.p12"
security create-keychain -p vdx "$kc"
security set-keychain-settings -lut 21600 "$kc"
security unlock-keychain -p vdx "$kc"
security import "$RUNNER_TEMP/cert.p12" -P "$MACOS_CERTIFICATE_PASSWORD" -A -t cert -f pkcs12 -k "$kc"
security list-keychain -d user -s "$kc"
identity=$(security find-identity -v -p codesigning "$kc" | awk -F'"' '/Developer ID Application/ {print $2; exit}')
echo "$MACOS_NOTARY_KEY" | base64 --decode > "$RUNNER_TEMP/notary.json"
key_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["key_id"])' "$RUNNER_TEMP/notary.json")
issuer=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["issuer_id"])' "$RUNNER_TEMP/notary.json")
python3 -c 'import json,sys; open(sys.argv[2],"w").write(json.load(open(sys.argv[1]))["key"])' "$RUNNER_TEMP/notary.json" "$RUNNER_TEMP/notary.p8"
find "$build" -path "*VDX_artefacts*" \( -name "VDX.app" -o -name "VDX.vst3" -o -name "VDX.component" \) -maxdepth 6 | while read -r bundle; do
  codesign --force --deep --options runtime --timestamp --sign "$identity" "$bundle"
  ditto -c -k --keepParent "$bundle" "$bundle.zip"
  xcrun notarytool submit "$bundle.zip" --key "$RUNNER_TEMP/notary.p8" --key-id "$key_id" --issuer "$issuer" --wait
  xcrun stapler staple "$bundle"
done
