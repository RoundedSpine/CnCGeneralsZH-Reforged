#!/usr/bin/env bash
set -euo pipefail
BUILD=$(cd "$1" && pwd)
CODE=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$CODE/../.." && pwd)
source_app="$BUILD/generals.app"
[ -x "$source_app/generals" ] || { echo 'Missing device app bundle' >&2; exit 1; }
mkdir -p "$BUILD/ipa/Payload"
app="$BUILD/ipa/Payload/Reforged.app"
rm -rf "$app"
ditto "$source_app" "$app"
ditto "$BUILD/overlay" "$app/Overlay"
find "$app/Overlay" -maxdepth 1 -name 'Reforged*.big' -delete
"$CODE/Tools/stage-ios-fonts.sh" "$app/Fonts"
mkdir -p "$app/Licenses"
. "$CODE/Tools/package-common.sh"
linked=$(package_link_libraries "$BUILD/build.ninja" "$BUILD/linked-libraries.txt")
entries=$(package_license_entries "$CODE/Tools/macos-app-licenses.txt" $linked)
for entry in $entries; do license_entry "$entry" "$app/Licenses"; done
cp "$REPO/NOTICE.md" "$app/Licenses/NOTICE.md"
cp "$CODE/Platform/iOS/Reforged.entitlements" "$BUILD/Reforged.entitlements"
python3 "$CODE/Tools/generals-icon.py" png "$CODE/Main/Generals.ico" "$app/AppIcon120.png" 120
python3 "$CODE/Tools/generals-icon.py" png "$CODE/Main/Generals.ico" "$app/AppIcon152.png" 152
python3 - "$app/Info.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
with open(p, 'rb') as f: info = plistlib.load(f)
info['CFBundleIcons'] = {'CFBundlePrimaryIcon': {'CFBundleIconFiles': ['AppIcon120']}}
info['CFBundleIcons~ipad'] = {'CFBundlePrimaryIcon': {'CFBundleIconFiles': ['AppIcon152']}}
with open(p, 'wb') as f: plistlib.dump(info, f)
PY
[ "$(lipo -archs "$app/generals")" = arm64 ]
# Reject an accidentally packaged macOS or simulator executable.
otool -l "$app/generals" > "$BUILD/ios-load-commands.txt"
awk '/cmd LC_BUILD_VERSION/ {block=1; next} block && /platform/ {if ($2 == 2 || $2 == "IOS") ok=1; block=0} END {exit !ok}' "$BUILD/ios-load-commands.txt"
codesign --force --sign - --timestamp=none --entitlements "$BUILD/Reforged.entitlements" "$app"
codesign --verify --deep --strict "$app"
codesign -d --entitlements :- "$app" > "$BUILD/signed-entitlements.plist"
python3 - "$BUILD/signed-entitlements.plist" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'rb') as f: e = plistlib.load(f)
for key in ('increased-memory-limit', 'extended-virtual-addressing'):
    assert e['com.apple.developer.kernel.' + key] is True, key
PY
rm -f "$BUILD/Reforged-iOS-unsigned.ipa"
(cd "$BUILD/ipa" && zip -qry "$BUILD/Reforged-iOS-unsigned.ipa" Payload)
echo "IPA: $BUILD/Reforged-iOS-unsigned.ipa (requires signing by your sideloading tool)"
