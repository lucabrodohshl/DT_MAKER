#!/usr/bin/env bash
# =============================================================================
# Build "Verified Twin Studio.app" and a .dmg for macOS (Apple Silicon or Intel,
# matching the build machine).
#
#   ./scripts/macos/build-app.sh            (or: make macos-app)
#
# Output: dist/macos/Verified Twin Studio.app and dist/macos/VerifiedTwinStudio-<version>.dmg
#
# The bundle is self-contained: the product executables, the only non-system
# library they need (Z3, rewritten to load from the bundle), the built web UI,
# the examples and scenarios. The launcher (apps/macos/launcher.mm) starts the
# stack under ~/Library/Application Support/Verified Twin Studio and opens the
# browser. The bundle is signed ad hoc; for distribution outside this Mac, sign
# with a Developer ID and notarize:
#   codesign --deep --force --options runtime --sign "Developer ID Application: …" "<app>"
#   xcrun notarytool submit <dmg> --keychain-profile <profile> --wait && xcrun stapler staple <dmg>
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

PRESET="studio-release"
BIN="build/$PRESET/bin"
OUT="dist/macos"
APP="$OUT/Verified Twin Studio.app"
WORK="build/macos"
VERSION="$(sed -n 's/^project([^)]*VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
VERSION="${VERSION:-1.0.0}"
TOOLS=(twin twin-studio twin-runtime twin-world twin-pt-feed)

log() { printf '\033[1m[macos]\033[0m %s\n' "$*"; }

[[ "$(uname)" == "Darwin" ]] || { echo "macOS only" >&2; exit 1; }

# --------------------------------------------------------------------- build
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    [[ -d third_party/install ]] || ./scripts/bootstrap-deps.sh
    [[ -f "build/$PRESET/CMakeCache.txt" ]] || cmake --preset "$PRESET" >/dev/null
    log "building the C++ components ($PRESET)"
    cmake --build "build/$PRESET" --target "${TOOLS[@]}" -j 8 >/dev/null
    log "building the web UI"
    (cd web/studio && { [[ -d node_modules ]] || npm ci --no-audit --no-fund >/dev/null; } && npm run build >/dev/null)
fi
for t in "${TOOLS[@]}"; do [[ -x "$BIN/$t" ]] || { echo "missing $BIN/$t" >&2; exit 1; }; done
[[ -f web/studio/dist/index.html ]] || { echo "missing web/studio/dist (build the UI)" >&2; exit 1; }

mkdir -p "$WORK"
log "compiling the launcher and the icon"
clang++ -std=c++20 -fobjc-arc -O2 -Wall -framework Cocoa apps/macos/launcher.mm -o "$WORK/VerifiedTwinStudio"
clang++ -std=c++20 -fobjc-arc -O2 -framework Cocoa apps/macos/make_icon.mm -o "$WORK/make_icon"
rm -rf "$WORK/AppIcon.iconset"
"$WORK/make_icon" "$WORK/AppIcon.iconset"
iconutil -c icns "$WORK/AppIcon.iconset" -o "$WORK/AppIcon.icns"

# ------------------------------------------------------------------ assemble
log "assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/bin" "$APP/Contents/Resources/lib"
cp "$WORK/VerifiedTwinStudio" "$APP/Contents/MacOS/"
cp "$WORK/AppIcon.icns" "$APP/Contents/Resources/"
for t in "${TOOLS[@]}"; do cp "$BIN/$t" "$APP/Contents/Resources/bin/"; done
R="$APP/Contents/Resources"
cp -R web/studio/dist "$R/web"
find "$R/web" -name '*.map' -delete
mkdir -p "$R/examples" "$R/models" "$R/scenarios"
cp -R examples/indoor-drone examples/industrial-pump examples/templates "$R/examples/"
cp -R models/indoor_drone "$R/models/"

# Bundle non-system dynamic libraries (Z3) and point the executables at them.
for t in "${TOOLS[@]}"; do
    exe="$R/bin/$t"
    while read -r dep; do
        name="$(basename "$dep")"
        if [[ ! -f "$R/lib/$name" ]]; then
            cp -L "$dep" "$R/lib/$name"
            chmod u+w "$R/lib/$name"
            install_name_tool -id "@rpath/$name" "$R/lib/$name"
        fi
        install_name_tool -change "$dep" "@executable_path/../lib/$name" "$exe"
    done < <(otool -L "$exe" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/Library/)')
done
if otool -L "$R"/bin/* "$R"/lib/* | grep -E '/opt/homebrew|/usr/local' >/dev/null; then
    echo "a bundled binary still references a non-system library path" >&2
    exit 1
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Verified Twin Studio</string>
  <key>CFBundleDisplayName</key><string>Verified Twin Studio</string>
  <key>CFBundleIdentifier</key><string>org.verifiedtwin.studio</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleExecutable</key><string>VerifiedTwinStudio</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.developer-tools</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>Verified Twin Studio ${VERSION}</string>
</dict>
</plist>
PLIST

# ---------------------------------------------------------------------- sign
log "signing (ad hoc)"
for f in "$R"/lib/* "$R"/bin/*; do codesign --force --sign - "$f"; done
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
"$R/bin/twin" version >/dev/null  # the bundled toolchain loads the bundled Z3

# ----------------------------------------------------------------------- dmg
log "creating the disk image"
DMG="$OUT/VerifiedTwinStudio-$VERSION.dmg"
STAGE="$WORK/dmg"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Verified Twin Studio" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
log "done: $APP"
log "      $DMG ($(du -h "$DMG" | cut -f1))"
