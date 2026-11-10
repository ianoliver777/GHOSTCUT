# GHOSTCUT

**AI-powered vocal discovery and creative sampling instrument by TRIPLE SEVEN AUDIO.**
*FIND THE FEELING. Engineered to Feel.*

VST3 / AU / Standalone · macOS (Apple Silicon + Intel, universal) · Windows 10/11 · JUCE 8 + CMake

---

## Status: Phase 2 (vocal intelligence)

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
| **Vocal separation**: ML (MDX-Net ONNX models, offline CPU) or DSP center extract, ORIGINAL / VOCALS switch, stem cache | ✅ working |
| **Deeper analysis**: pitch contour, range, slope, bends, sustain, vibrato rate/depth, harmonicity, breathiness, air, attack/decay, jumps | ✅ working |
| **Phrases / Syllables** detection granularity | ✅ working |
| **Character scores** ETHEREAL / HAUNTED / NOSTALGIC / HEAVENLY / EXPERIMENTAL (labelled heuristics, each explains *why*) | ✅ working |
| **Search**: describe what you want, mapped to measurable features, shows what it understood and ignored | ✅ working |
| **Sort** by time, any score, or search match | ✅ working |
| GENERATE / SEQUENCE | ⏳ Phase 3 |
| EFFECTS | ⏳ Phase 4 |
| GHOST MATCH, preset library | ⏳ Phase 5 |

Tabs for unbuilt phases are visible but clearly marked as not built yet.

### Verified in Phase 2
- C++ MDX separation reproduces the Python/ONNX Runtime reference exactly: **19.95 dB SDR** on a speech + drums + bass test mix (the unseparated mix scores −9.97 dB). It runs at ~1.8× realtime on 4 CPU threads.
- Unit tests: STFT perfect reconstruction (n_fft 7680 and 4096) and torch bin layout, resampler round-trip, center-extract, vibrato rate/depth, glide slope/bend, breathiness, attack, score sanity, syllable splitting, all four example searches from the brief, separation + recall from the stem cache.
- pluginval strictness 10 with ONNX Runtime bundled in the VST3.

### Verified in Phase 1
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

## Vocal separation models

GHOSTCUT runs MDX-Net vocal models through ONNX Runtime, fully offline on the CPU. **No model weights ship with the plugin.** To use ML separation:

1. Download one of the supported models (both are 66 MB):
   - `Kim_Vocal_2.onnx`: https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/Kim_Vocal_2.onnx
   - `UVR-MDX-NET-Voc_FT.onnx`: https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-MDX-NET-Voc_FT.onnx
2. GHOSTCUT → SETTINGS → **Install model…** and pick the file. It is verified by fingerprint and copied to `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Models/`.
3. Choose it in the separation dropdown, then press **SEPARATE** on the Discover page.

Only these two exact files are accepted, because their parameters (n_fft 7680, 3072 bins, 256 frames, output gain) were verified. Any other file is rejected with a message instead of producing garbage.

**Without a model**, *Center extract (DSP)* still works on stereo mixes: it keeps L/R-coherent, centre-panned content above ~150 Hz. It's rougher than ML but has no licensing questions.

> ⚠️ **Licensing before you sell:** these community models come from the UVR project. Their weights' terms for *commercial redistribution* are not clearly stated. That is why GHOSTCUT makes the user install them rather than bundling them. Before shipping a commercial build with a bundled or auto-downloaded model, get written confirmation from the model authors, or train or licence your own model. Phase 6 includes that licensing review.

How it works: audio → 44.1 kHz stereo (Kaiser-windowed sinc resampler) → chunks of 261,120 samples with 3,840-sample trims → torch-compatible STFT (pocketfft) → model → ISTFT → back to the source rate. Separated stems are cached as 32-bit float WAV, keyed by source fingerprint + trim + model, so reopening a project or re-analysing is instant.

## Search: what it understands

Search is **keyword-to-feature mapping, not AI language understanding**. It turns words into measurable targets and always shows what it used and what it ignored:

| You type | It looks for |
|---|---|
| high, high-pitched, soprano / low, deep | register |
| falsetto | high register + some breath |
| airy / breathy, whisper | energy above 5 kHz / aperiodicity + noise flatness |
| sustained, held, long note, vowel | longest held note (pitch within ±0.75 st) |
| short / long, `under 2 seconds`, `over 500ms` | fragment length |
| descending, falling / rising | pitch slope |
| bend, glide, slide | largest glide within 200 ms |
| vibrato, wobbly | vibrato depth |
| soft, gentle / loud | attack time, level |
| dark, warm / bright | spectral centroid |
| clean, pure, stable | harmonicity, pitch stability |
| ghostly, eerie / ethereal, dreamy / heavenly, angelic / nostalgic / weird, glitchy, experimental | the matching character score |
| reverse, reversing | long tail + sharp start (reverses into a swell) |
| `above A3`, `below C4`, `around E4` | register (Ableton note names, C3 = 60) |
| not / no + word | inverts it (`no vibrato`) |

## Character scores (heuristics)

Each score is a weighted mix of measured features. They're sorting aids, not emotion detection. The inspector lists the reasons behind the top score.

- **ETHEREAL**: high register, breathy, airy top end, soft attack, held notes
- **HAUNTED**: dark tone, long decay, pitch bends, unstable pitch, low register, quiet
- **NOSTALGIC**: gentle vibrato, mid register, warm tone, held notes, steady melody
- **HEAVENLY**: high register, pure tone, stable pitch, held notes, not breathy
- **EXPERIMENTAL**: pitch jumps, big glides, wide range, busy spectrum, noisy / unvoiced

Exact weights: `Source/Analysis/FragmentRanker.cpp`.

## Architecture

```
Source/
  Core/        Fragment, SourceSnapshot, AudioThreadExchange (lock-free handoff), Session (model + state)
  Audio/       AudioImporter (decode, peaks, fingerprint), SamplerEngine (voices, MIDI), AudioExporter
  Analysis/    PhraseSegmenter (energy / flux / YIN, phrases + syllables), FeatureExtractor, FragmentRanker,
               SearchQuery, VocalSeparator (MDX + center extract + cache), OnnxRuntime (dynamic loader), Stft
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
| Steinberg VST3 SDK 3.8 (bundled in JUCE) | VST3 format | MIT | |
| ONNX Runtime 1.20.1 | ML inference (CPU) | MIT | shared library copied into each bundle (`Contents/Frameworks` on mac, next to the binary on Windows) and loaded by absolute path at run time |
| pocketfft (C++ header) | arbitrary-size FFT (n_fft 7680) | BSD-3-Clause | `ThirdParty/pocketfft` |
| MDX-Net vocal models (Kim Vocal 2, UVR Voc FT) | vocal separation | **unclear for commercial redistribution** | not shipped. The user installs them, see above |
| pluginval (CI only, not shipped) | validation | GPLv3 | tool, not linked |

**Planned:**
| Component | Purpose | License | Note |
|---|---|---|---|
| Signalsmith Stretch | time-stretch / pitch-shift (Phase 3) | MIT | avoid Rubber Band (GPL unless commercially licensed) |
| Demucs (htdemucs) | possible higher-quality separation | code MIT | weights not reachable from the dev environment; MDX-Net chosen instead for Phase 2 |
| CREPE-tiny (optional) | ML pitch upgrade over YIN | MIT | only if YIN proves insufficient on real vocals |

---

## Project portability (how state is stored)

- The DAW project stores a **reference**, not the audio: the absolute path, the file size, a content fingerprint (FNV-1a over size + first/last 256 KB), and the trim range.
- Fragments (start/end in samples, manual flag, root override, measured features), selection, loop, sensitivity and export bit depth are stored in the project.
- Isolated vocals are cached on disk in `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Cache` (key: fingerprint + trim + model). A reopened project reloads its vocals from there. If the cache was cleared, separation re-runs automatically (with progress). Frame measurements are recomputed in the background after load (seconds).
- The project also stores the separation method, the ORIGINAL/VOCALS view, granularity, sort and search text.
- Missing file → fragments are kept, the reference is kept (re-saving doesn't lose it), and LOCATE or dropping a file with the same name relinks it. A different file at the same path triggers a warning.
- Drag-to-DAW renders are written to `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Fragments/` (not a temp folder) so Live's references survive reboots.
- For full portability use Live's **File → Collect All and Save**, and keep sources inside the project folder.

## Known Phase 1 limitations
- ML separation runs on the CPU (~1.8× realtime with 4 threads). A 4-minute song takes about 2 minutes the first time, then it's instant from the cache. It deliberately uses half your cores so the DAW keeps playing.
- Character scores and breathiness are heuristics from measured features. The weights were tuned on synthetic material and should be tuned further on real vocals.
- Pitch tracking is YIN (70–1100 Hz). Very breathy or whispered material often has no stable pitch, which is shown as "no stable pitch".
- Stealing a voice when all 32 are busy restarts it without a crossfade (rare, may click).
- Binaries are not code-signed or notarized yet (Phase 6).
- AU manufacturer code `T7Au` / plugin code `Ghct`. If other TRIPLE SEVEN AUDIO plugins already use a different manufacturer code, align it **before the first public release** (changing it later breaks saved projects).

---

© TRIPLE SEVEN AUDIO, triplesevenaudio.com
