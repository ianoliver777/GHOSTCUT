# GHOSTCUT

**AI-powered vocal discovery and creative sampling instrument by TRIPLE SEVEN AUDIO.**
*FIND THE FEELING. Engineered to Feel.*

VST3 / AU / Standalone · macOS (Apple Silicon + Intel, universal) · Windows 10/11 · JUCE 8 + CMake

---

## Status: Phase 1 (working plugin)

| Feature | State |
|---|---|
| Import WAV / MP3 / AIFF / FLAC by drag-and-drop or IMPORT (mono, stereo, any sample rate, up to 30 min) | ✅ working |
| Waveform with zoom (buttons, Cmd/Ctrl + wheel, pinch), scrolling, ruler, cursor, playhead | ✅ working |
| Source playback, loop, region selection, trim to selection | ✅ working |
| Basic vocal phrase detection (DSP: energy gate + spectral flux + YIN voicing/pitch) | ✅ working |
| Fragment browser: timestamps, mini waveforms, detected root, voiced %, audition, resizable boundaries | ✅ working |
| Manual fragments from a selection, root-note override, delete | ✅ working |
| Export fragment WAV (16 / 24 / 32f, source sample rate, not resampled) + drag-to-DAW | ✅ working |
| MIDI playback: SLICES (one fragment per key) and CHROMATIC (selected fragment, pitched from its root) | ✅ working |
| DAW state recall (parameters, source reference, trim, fragments, selection, settings) | ✅ working |
| Missing-source handling (fragments kept, LOCATE to relink, fingerprint check) | ✅ working |
| Vocal separation, scoring (ETHEREAL / HAUNTED / ...), search | ⏳ Phase 2 |
| GENERATE / SEQUENCE | ⏳ Phase 3 |
| EFFECTS | ⏳ Phase 4 |
| GHOST MATCH, preset library | ⏳ Phase 5 |

Tabs for unbuilt phases are visible but clearly marked as not built yet.

### Verified so far
- `GhostcutTests` passes (phrase detection at 44.1/48/96 kHz, pitch detection, lock-free handoff stress test, engine rendering + sample-rate ratio, bit-exact 24-bit export, save / restore / missing-file / relocate / trim).
- VST3 builds clean on Linux and passes **pluginval at strictness 10**.
- Standalone tested: import → 9/9 phrases found with correct pitches → quit → relaunch restores everything.
- macOS / Windows builds, AU validation (`auval`) and pluginval on those platforms run in GitHub Actions (`.github/workflows/build.yml`). **Ableton Live 12 testing has to be done on your machine**: see below.

---

## Build on macOS

Requirements: Xcode 15+ (or the Command Line Tools), CMake 3.22+ (`brew install cmake ninja`), git.

```bash
git clone https://github.com/ianoliver777/GHOSTCUT.git
cd GHOSTCUT

# configure (downloads JUCE 8.0.15 automatically, builds a universal arm64 + x86_64 binary)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# build VST3 + AU + Standalone + tests
cmake --build build --parallel

# run the unit tests (must print ALL TESTS PASSED)
./build/GhostcutTests_artefacts/Release/GhostcutTests

# install for your user
cp -R build/GHOSTCUT_artefacts/Release/VST3/GHOSTCUT.vst3      ~/Library/Audio/Plug-Ins/VST3/
cp -R build/GHOSTCUT_artefacts/Release/AU/GHOSTCUT.component   ~/Library/Audio/Plug-Ins/Components/

# validate the AU (type / subtype / manufacturer)
killall -9 AudioComponentRegistrar 2>/dev/null; auval -v aumu Ghct T7Au
```

Faster local builds: `-DCMAKE_OSX_ARCHITECTURES=arm64`. Using a local JUCE checkout: `-DGHOSTCUT_JUCE_DIR=/path/to/JUCE`.
Want Xcode? `cmake -S . -B build-xcode -G Xcode`.

**Downloaded a CI build instead?** The binaries are unsigned (signing + notarization is Phase 6), so macOS will quarantine them:
```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/GHOSTCUT.vst3 ~/Library/Audio/Plug-Ins/Components/GHOSTCUT.component
```

## Build on Windows

Visual Studio 2022 with "Desktop development with C++", plus CMake.
```bat
cmake -S . -B build
cmake --build build --config Release
build\GhostcutTests_artefacts\Release\GhostcutTests.exe
xcopy /E /I build\GHOSTCUT_artefacts\Release\VST3\GHOSTCUT.vst3 "C:\Program Files\Common Files\VST3\GHOSTCUT.vst3"
```

---

## Verify in Ableton Live 12

1. **Rescan.** Settings → Plug-Ins → turn on *Use Audio Units v2* and/or *VST3 System Folders* → **Rescan**. GHOSTCUT appears under *Plug-Ins → TRIPLE SEVEN AUDIO* as an **instrument**.
2. **Load.** Drag GHOSTCUT onto an empty MIDI track. Open its window. It should show the DISCOVER page with "DROP A WAV OR MP3 HERE".
3. **Import.** Drag an acapella WAV or MP3 from Finder (or Live's browser) onto the plugin. The status bar shows loading → "Analyzing phrases… %" → "N fragments found". Live keeps playing smoothly during analysis (it runs off the audio thread).
4. **Browse.** Click rows in the fragment list: each one auditions instantly. ↑/↓ keys step through them. Space plays from the cursor.
5. **MIDI.** Arm the track and play your keyboard. In **SLICES** mode, C3 = fragment 1, C#3 = fragment 2, and so on (the KEY column shows the mapping). Switch to **CHROMATIC**: the selected fragment plays across the keyboard, at its original pitch on its ROOT key.
6. **Draw a clip.** Write a MIDI clip with a few notes from C3 upward and loop it. The chops should trigger sample-accurately with no clicks. Try TRIGGER = Gate with a short RELEASE for stuttery chops.
7. **Edit.** Drag across the waveform to select, press **+ FRAGMENT**. Select a fragment and drag its edges in the lane under the waveform.
8. **Export.** Drag a fragment row (or DRAG TO DAW) onto an audio track. Live gets a WAV clip.
9. **Recall (the important one).** Save the Live set, close it, reopen it. GHOSTCUT must come back with the same source, the same fragments, boundaries, root overrides, and knob positions. Also duplicate the track: the copy must be identical.
10. **Missing file.** Rename the source file in Finder and reopen the set. GHOSTCUT shows "Source missing… click LOCATE", keeps the fragments, and relinks when you locate (or drop) the file.
11. **Automation.** OUTPUT, ATTACK, RELEASE, VELOCITY, BASE, KEY MODE and TRIGGER are automatable in Live's device panel.

Something off? Note the step number and what the status bar said.

---

## Architecture

```
Source/
  Core/        Fragment, SourceSnapshot, AudioThreadExchange (lock-free handoff), Session (model + state)
  Audio/       AudioImporter (decode, peaks, fingerprint), SamplerEngine (voices, MIDI), AudioExporter
  Analysis/    PhraseSegmenter (energy / flux / YIN; frame features reused by Phase 2)
  UI/          GhostLookAndFeel, WaveformView, FragmentBrowser, DiscoverPanel
  PluginProcessor / PluginEditor
Tests/         GhostcutTests console app
```

Planned additions follow the brief: `Analysis/VocalSeparator` (ONNX), `FeatureExtractor`, `FragmentRanker`, `ReferenceMatcher`, `Generation/`, `DSP/`, `Sequencer/`, `Presets/`. The added `Core/` module holds the types everything shares.

**Real-time rules (enforced by design):**
- The audio thread never allocates, frees, locks, does file I/O or runs analysis.
- Decoded audio (`SourceSnapshot`) and the fragment table are **immutable** once published. The UI hands them to the audio thread through `AudioThreadExchange`: an atomic pointer swap, plus a lock-free retire queue so old objects are freed on the message thread, never in the callback.
- UI → audio commands (play, stop, audition) go through a fixed-size SPSC queue. Audio → UI (playhead, active voices) is atomics only.
- Decoding and analysis run on a worker thread with progress and cancellation. A newer import cancels and supersedes an older one.
- Voices use 4-point Hermite interpolation, which also covers the source-rate to host-rate conversion. Every voice has 3 ms edge fades, so any cut point is click-free.

---

## Third-party dependencies & licensing

| Component | Used for | License | Notes |
|---|---|---|---|
| JUCE 8.0.15 | framework, MP3/WAV decoding, FFT | AGPLv3 **or** JUCE commercial licence | Selling closed-source requires a JUCE licence (free *Starter* tier below its revenue cap, check juce.com for current terms) |
| JUCE MP3 decoder (`JUCE_USE_MP3AUDIOFORMAT`) | MP3 import | part of JUCE | MP3 patents have expired. On macOS CoreAudio also decodes MP3 |
| Steinberg VST3 SDK (bundled in JUCE) | VST3 format | MIT (since SDK 3.8) | |
| pluginval (CI only, not shipped) | validation | GPLv3 | tool, not linked |

**Planned (not yet included), chosen for Phase 2+:**
| Component | Purpose | License | Deployment note |
|---|---|---|---|
| ONNX Runtime | offline inference, mac arm64/x64 + win x64 | MIT | ship the dylib/dll inside the bundle |
| Demucs (htdemucs) weights exported to ONNX | vocal / instrumental separation | code MIT; **confirm weight redistribution terms before shipping** | ~80 MB, so download on first use rather than bundling in the installer |
| Signalsmith Stretch | time-stretch / pitch-shift | MIT | avoid Rubber Band (GPL unless commercially licensed) |
| CREPE-tiny (optional) | ML pitch upgrade over YIN | MIT | only if YIN proves insufficient on real vocals |

---

## Project portability (how state is stored)

- The DAW project stores a **reference**, not the audio: the absolute path, the file size, a content fingerprint (FNV-1a over size + first/last 256 KB), and the trim range.
- Fragments (start/end in samples, manual flag, root override, measured features), selection, loop, sensitivity and export bit depth are stored in the project.
- No analysis cache is written to disk in Phase 1. Measurements are stored per fragment, and frame analysis re-runs in the background after a reload (seconds). Phase 2 adds an on-disk cache for separated stems, keyed by the fingerprint, in `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Cache`.
- Missing file → fragments are kept, the reference is kept (re-saving doesn't lose it), and LOCATE or dropping a file with the same name relinks it. A different file at the same path triggers a warning.
- Drag-to-DAW renders are written to `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Fragments/` (not a temp folder) so Live's references survive reboots.
- For full portability use Live's **File → Collect All and Save**, and keep sources inside the project folder.

## Known Phase 1 limitations
- Phrase detection is tuned for **isolated vocals / acapellas**. On full mixes everything is "active", so cuts land on energy dips. Vocal separation (Phase 2) fixes this.
- Measurements (pitch, voicing, brightness) are shown as raw numbers. Character scores arrive in Phase 2 as explicitly labelled heuristics.
- Stealing a voice when all 32 are busy restarts it without a crossfade (rare, may click).
- Binaries are not code-signed or notarized yet (Phase 6).
- AU manufacturer code `T7Au` / plugin code `Ghct`. If other TRIPLE SEVEN AUDIO plugins already use a different manufacturer code, align it **before the first public release** (changing it later breaks saved projects).

---

© TRIPLE SEVEN AUDIO, triplesevenaudio.com
