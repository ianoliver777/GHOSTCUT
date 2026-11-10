#!/usr/bin/env bash
# Builds GHOSTCUT-<version>-macOS.pkg from a finished Release build.
#
#   installer/macos/build_pkg.sh <build dir> <version> <notices file> [out dir]
#
# Optional signing + notarization (all three groups must be set to use them):
#   DEVELOPER_ID_APP        "Developer ID Application: Your Name (TEAMID)"   -> signs the plugins + app
#   DEVELOPER_ID_INSTALLER  "Developer ID Installer: Your Name (TEAMID)"     -> signs the .pkg
#   NOTARY_PROFILE          keychain profile created with `xcrun notarytool store-credentials`
# Without them the package is built unsigned (fine for testing; Gatekeeper will warn users).
set -euo pipefail

BUILD="${1:?build dir}"
VERSION="${2:?version}"
NOTICES="${3:?notices file}"
OUT="${4:-dist}"
ART="$BUILD/GHOSTCUT_artefacts/Release"
ID="com.triplesevenaudio.ghostcut"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT" "$WORK/vst3" "$WORK/au" "$WORK/app" "$WORK/res"

cp -R "$ART/VST3/GHOSTCUT.vst3"      "$WORK/vst3/"
cp -R "$ART/AU/GHOSTCUT.component"   "$WORK/au/"
cp -R "$ART/Standalone/GHOSTCUT.app" "$WORK/app/"

if [[ -n "${DEVELOPER_ID_APP:-}" ]]; then
  echo "== codesign (hardened runtime, timestamp)"
  for b in "$WORK/vst3/GHOSTCUT.vst3" "$WORK/au/GHOSTCUT.component" "$WORK/app/GHOSTCUT.app"; do
    # sign the bundled ONNX Runtime first, then the bundle (inside-out)
    find "$b/Contents/Frameworks" -name "*.dylib" -exec codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" {} \; 2>/dev/null || true
    codesign --force --timestamp --options runtime --sign "$DEVELOPER_ID_APP" "$b"
    codesign --verify --deep --strict --verbose=2 "$b"
  done
else
  echo "== no DEVELOPER_ID_APP: keeping ad-hoc signatures (unsigned build)"
fi

pkgbuild --root "$WORK/vst3" --identifier "$ID.vst3" --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/au"   --identifier "$ID.au"   --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/Components" "$WORK/au.pkg"
pkgbuild --root "$WORK/app"  --identifier "$ID.app"  --version "$VERSION" --install-location "/Applications" "$WORK/app.pkg"

cp "$NOTICES" "$WORK/res/THIRD_PARTY_NOTICES.txt"
cat > "$WORK/res/welcome.html" <<HTML
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>GHOSTCUT $VERSION</h2>
<p>AI vocal discovery and creative sampling instrument by <b>TRIPLE SEVEN AUDIO</b>.<br><i>Engineered to Feel.</i></p>
<p>Installs the VST3 and Audio Unit plug-ins for all users plus the standalone app.
After installing, rescan plug-ins in your DAW (Ableton Live: Settings &rarr; Plug-Ins &rarr; Rescan).</p>
<p>ML vocal separation models are not included; see the README for installing one.</p>
</body></html>
HTML

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>GHOSTCUT $VERSION</title>
  <organization>$ID</organization>
  <welcome file="welcome.html" mime-type="text/html"/>
  <license file="THIRD_PARTY_NOTICES.txt"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plug-in" description="/Library/Audio/Plug-Ins/VST3"><pkg-ref id="$ID.vst3"/></choice>
  <choice id="au" title="Audio Unit plug-in" description="/Library/Audio/Plug-Ins/Components"><pkg-ref id="$ID.au"/></choice>
  <choice id="app" title="Standalone app" description="/Applications"><pkg-ref id="$ID.app"/></choice>
  <pkg-ref id="$ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.au" version="$VERSION">au.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
XML

PKG="$OUT/GHOSTCUT-$VERSION-macOS.pkg"
SIGN_ARGS=()
[[ -n "${DEVELOPER_ID_INSTALLER:-}" ]] && SIGN_ARGS=(--sign "$DEVELOPER_ID_INSTALLER" --timestamp)
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --resources "$WORK/res" "${SIGN_ARGS[@]}" "$PKG"

if [[ -n "${DEVELOPER_ID_INSTALLER:-}" && -n "${NOTARY_PROFILE:-}" ]]; then
  echo "== notarize"
  xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
  spctl --assess --type install --verbose "$PKG"
else
  echo "== not notarized (set DEVELOPER_ID_INSTALLER + NOTARY_PROFILE)"
fi
echo "built $PKG"
