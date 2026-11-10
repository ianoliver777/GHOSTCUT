# Code signing and notarization

Unsigned builds work for testing, but customers need signed installers: macOS Gatekeeper blocks unsigned/unnotarized plug-ins, and Windows SmartScreen warns on unsigned installers.

## macOS: Developer ID + notarization

### One-time setup
1. **Apple Developer Program** membership (paid, as an individual or as TRIPLE SEVEN WORLD as an organization).
2. In Xcode → Settings → Accounts (or developer.apple.com → Certificates), create:
   - **Developer ID Application** (signs the plug-ins and the app)
   - **Developer ID Installer** (signs the .pkg)
3. Create an **app-specific password** at appleid.apple.com, then store notary credentials in your keychain:
   ```bash
   xcrun notarytool store-credentials ghostcut-notary \
     --apple-id "you@example.com" --team-id "TEAMID" --password "abcd-efgh-ijkl-mnop"
   ```

### Signing a release locally
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 scripts/make_notices.py build/_deps/juce-src build/_deps/onnxruntime-src . dist/THIRD_PARTY_NOTICES.txt

export DEVELOPER_ID_APP="Developer ID Application: <Name> (<TEAMID>)"
export DEVELOPER_ID_INSTALLER="Developer ID Installer: <Name> (<TEAMID>)"
export NOTARY_PROFILE=ghostcut-notary
installer/macos/build_pkg.sh build 0.9.0 dist/THIRD_PARTY_NOTICES.txt dist
```
`build_pkg.sh` signs inside-out (the bundled `libonnxruntime.dylib` first, then each bundle) with the hardened runtime and a secure timestamp. It verifies each signature, builds a signed product archive, submits it to the notary service, waits, staples the ticket and runs `spctl` to confirm Gatekeeper accepts it.

Check a result yourself:
```bash
codesign -dv --verbose=4 /Library/Audio/Plug-Ins/Components/GHOSTCUT.component
spctl --assess --type install -v dist/GHOSTCUT-0.9.0-macOS.pkg
```

### Signing in GitHub Actions
The workflow already passes `DEVELOPER_ID_APP`, `DEVELOPER_ID_INSTALLER` and `NOTARY_PROFILE` secrets to `build_pkg.sh`. A CI runner also needs the certificates in a temporary keychain and the notary profile created there. Add this step before "macOS installer" once you have the certificates exported as a password-protected `.p12`:

```yaml
      - name: Import signing certificates
        if: runner.os == 'macOS' && env.MACOS_CERTS_P12 != ''
        env:
          MACOS_CERTS_P12: ${{ secrets.MACOS_CERTS_P12 }}            # base64 of a .p12 holding both Developer ID certs
          MACOS_CERTS_PASSWORD: ${{ secrets.MACOS_CERTS_PASSWORD }}
          NOTARY_APPLE_ID: ${{ secrets.NOTARY_APPLE_ID }}
          NOTARY_TEAM_ID: ${{ secrets.NOTARY_TEAM_ID }}
          NOTARY_PASSWORD: ${{ secrets.NOTARY_PASSWORD }}
        run: |
          KC=$RUNNER_TEMP/build.keychain; PW=$(uuidgen)
          security create-keychain -p "$PW" "$KC"; security set-keychain-settings -lut 21600 "$KC"
          security unlock-keychain -p "$PW" "$KC"
          echo "$MACOS_CERTS_P12" | base64 --decode > $RUNNER_TEMP/certs.p12
          security import $RUNNER_TEMP/certs.p12 -k "$KC" -P "$MACOS_CERTS_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild
          security list-keychains -d user -s "$KC" login.keychain
          security set-key-partition-list -S apple-tool:,apple: -s -k "$PW" "$KC"
          xcrun notarytool store-credentials ghostcut-notary --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
            --password "$NOTARY_PASSWORD" --keychain "$KC"
```
Then set the repository secret `NOTARY_PROFILE` to `ghostcut-notary`.

## Windows: Authenticode

1. Buy an **OV or EV code-signing certificate** (e.g. from DigiCert, Sectigo or SSL.com). Since 2023, new certificates come on a hardware token or a cloud HSM (e.g. Azure Trusted Signing, SSL.com eSigner); plain `.pfx` files are no longer issued.
2. Sign the plug-in and the app **before** packaging, then sign the installer:
   ```bat
   signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a "build\GHOSTCUT_artefacts\Release\VST3\GHOSTCUT.vst3\Contents\x86_64-win\GHOSTCUT.vst3"
   signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a "build\GHOSTCUT_artefacts\Release\Standalone\GHOSTCUT.exe"
   ```
3. Let Inno Setup sign the installer and uninstaller: in Inno Setup → Tools → Configure Sign Tools add
   `ghostcutsign = signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a $f`
   and add `SignTool=ghostcutsign` to `[Setup]` in `installer/windows/GHOSTCUT.iss`.
4. `onnxruntime.dll` is already signed by Microsoft; leave its signature alone.

SmartScreen reputation builds up over downloads. EV certificates get it immediately.

## AU / VST3 identifiers (don't change after release)

| | Value |
|---|---|
| Bundle id | `com.triplesevenaudio.ghostcut` |
| AU manufacturer / subtype | `T7Au` / `Ghct` |
| VST3 class id | derived by JUCE from the above |

Changing them after release makes DAWs treat GHOSTCUT as a different plug-in, and saved projects lose it. If other TRIPLE SEVEN AUDIO plug-ins use a different manufacturer code, align this one **before** v1.0.
