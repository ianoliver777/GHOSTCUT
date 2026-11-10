# Release checklist

## Before tagging
- [ ] `ALL TESTS PASSED` locally and in CI (both platforms)
- [ ] pluginval strictness 10 (CI) + `auval -v aumu Ghct T7Au` (CI, macOS)
- [ ] Manual pass in **Ableton Live 12** on macOS (Apple Silicon **and** Rosetta/Intel if possible) and Windows (README → "Verify in Ableton Live 12")
- [ ] Save, close and reopen a Live set with GHOSTCUT on 3 tracks: everything recalls (source, fragments, vocals view, pattern bank, FX, preset name)
- [ ] Missing-source flow (rename the file, reopen, LOCATE)
- [ ] CPU check: pattern playing + all FX on → Live's CPU meter stays low (benchmark test: ~3–4% of one core)
- [ ] Bump `project(GHOSTCUT VERSION x.y.z)` in `CMakeLists.txt`
- [ ] Update README status + changelog
- [ ] Licensing actions in `LICENSING_REVIEW.md` resolved for a commercial release (JUCE licence, models, EULA)

## Tag + build
```bash
git tag v0.9.0 && git push origin v0.9.0
```
CI builds both installers and attaches them to a **draft** GitHub Release with auto-generated notes. Signed + notarized builds need the secrets in `SIGNING.md`.

## After the build
- [ ] Download the draft release assets, install on a clean machine (or a fresh user account), rescan in Live and smoke test
- [ ] macOS: `spctl --assess --type install -v GHOSTCUT-*.pkg` reports accepted (signed builds)
- [ ] Windows: right-click the setup exe → Properties → Digital Signatures shows TRIPLE SEVEN AUDIO (signed builds)
- [ ] Publish the release; upload the installers to the Sellfy store
- [ ] Keep the exact commit + `THIRD_PARTY_NOTICES.txt` with the release
