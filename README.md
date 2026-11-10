# GHOSTCUT

**AI-powered vocal discovery and creative sampling instrument by TRIPLE SEVEN AUDIO.**
*FIND THE FEELING. Engineered to Feel.*

VST3 / AU / Standalone · macOS (Apple Silicon + Intel, universal) · Windows 10/11 · JUCE 8 + CMake

---

## Status: Phase 6 (commercial preparation): v0.9.0 release candidate

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
| **GENERATE**: 5 modes (ETHEREAL / HAUNTED / NOSTALGIA / HEAVEN / CHAOS) that change rhythm, melody, layering and per-chop DSP | ✅ working |
| Key, scale, range, steps, rate, density, complexity, randomness, swing, **seeded and reproducible** | ✅ working |
| Pitch-aware chops: formant-preserving pitch shift, time stretch, reverse, sub-range, fades, harmony layers (Signalsmith Stretch) | ✅ working |
| **SEQUENCE**: 16/32 steps, slot lanes, per-step note / velocity / gate / chop range / reverse / ratchet / layer / choke | ✅ working |
| Host tempo + transport sync (sample-accurate), swing, internal play for preview | ✅ working |
| Pattern bank A–H saved in the project | ✅ working |
| Export loop WAV (seamless, tail wrapped) and MIDI. Drag either straight into Live | ✅ working |
| **EFFECTS**: TEXTURE (saturation, tilt, low/high cut, chorus, wow, flutter, noise), LO-FI (rate, bits, bandwidth, drive, mix), DELAY (tempo-synced, ping-pong, filtered feedback), REVERB (8-line FDN: size, decay, pre-delay, damping, width, mix) | ✅ working |
| PITCH / FORMANT (semitones, fine, formant, harmony layers) and REVERSE (all chops, transition, reverse step order), rendered offline for best quality | ✅ working |
| Mode FX presets + "FX follow mode". All ~35 effect controls are automatable | ✅ working |
| Loop exports include the effects (two-pass render, so tails wrap seamlessly) | ✅ working |
| **GHOST MATCH**: rank fragments by similarity to a reference vocal (contour shape via DTW, register, timbre, length, breathiness, rhythm, harmonicity) with a per-feature breakdown | ✅ working |
| Advanced search: `like #12`, `very airy`, negation, numbers and notes, plus a JSON contract for an optional language-model interpreter (not included, offline keywords stay the default) | ✅ working |
| **Presets**: 17 factory presets in 6 categories (also exposed as host programs), user presets with categories, save / load / delete / next / prev, recalled with the project | ✅ working |
| Analysis upgrades: spectral envelope (12 bands), octave-error correction, isolated-frame cleanup, on-disk frame cache (instant re-open) | ✅ working |
| **Release tooling**: macOS .pkg (VST3 + AU + app, optional Developer ID signing + notarization) and Windows Inno Setup installer built in CI, auto-generated third-party notices, draft GitHub Release on `v*` tags | ✅ working |
| Docs: [User guide](docs/USER_GUIDE.md) · [Install](docs/INSTALL.md) · [Signing & notarization](docs/SIGNING.md) · [Licensing review](docs/LICENSING_REVIEW.md) · [Release checklist](docs/RELEASE_CHECKLIST.md) | ✅ |
| Shortcuts: ⌘/Ctrl+1–5 pages, ⌘/Ctrl+G generate, ⌘/Ctrl+Shift+G rebuild, ⌘/Ctrl+P play pattern, Space play, ↑/↓ browse, ⌘/Ctrl+F search | ✅ working |

Tabs for unbuilt phases are visible but clearly marked as not built yet.

### Verified in Phase 6
- Performance: 10 s at 48 kHz / 128-sample blocks with 48 voices sounding, a pattern running and **every** effect on renders in ~0.33 s, about **3.4% of one CPU core**.
- Third-party notices are generated from the real license files (all 15 found).
- Still to do on real hardware (can't be done in CI): Ableton Live 12 sessions on macOS + Windows, and signing with your certificates.

### Verified in Phase 5
- GHOST MATCH: self-match 1.00. A same-shaped phrase in another key (0.79) beats an opposite-shaped phrase in the same register (0.71), which beats a short noisy one (0.20). In a session, the matching phrase ranks first, and the reference survives save/restore.
- Strong 2nd-harmonic tone: 200 voiced frames, 0 octave errors.
- Presets: factory load sets mode FX and GENERATE settings and never touches your seed. A user preset round-trips exactly; delete works; next/prev walks the list.
- pluginval strictness 10 passes. It caught an on/off parameter recall bug in this phase: switches are now 2-state choices, so state recall is exact.

### Verified in Phase 4
- All effects off = bit-exact passthrough. Toggling every section on/off mid-signal stays click-free (linear 20 ms fades that land exactly on 0/1).
- Reverb: the mid-band decay tracks the setting (1 s → 0.9 s, 3 s → 2.4 s measured). It stays stable at 20 s decay with max size and no damping for 20 s of noise.
- Delay: a 1/8 at 120 bpm echo lands within 4 samples; ping-pong alternates L → R; feedback decays.
- Lo-fi: 4-bit gives 15 levels, and a 4 kHz rate holds samples. Saturation adds >10× third harmonic. Noise is exactly silent when off. Wow bends pitch ~0.15 st.
- Global pitch/reverse/harmony reach the rendered chops (+5 st measured within 0.3 st).
- pluginval strictness 10 passes.

### Verified in Phase 3
- Same seed → identical pattern (tested, including after save/restore). Notes stay in key for the in-key modes over 120 seeds.
- Modes differ measurably (200 seeds each): CHAOS ~8.7 hits/bar vs HEAVEN ~3; HEAVEN holds notes 2× longer than NOSTALGIA; HAUNTED reverses >30% of hits; HEAVEN layers harmonies on >60%.
- Pitch shift +7 st lands within 0.3 st. Stretch ×2 keeps pitch. Formants preserved on a +12 st shift (vowel centroid 1894 → 1871 Hz vs 3413 Hz without preservation).
- Sequencer: steps fire on the exact sample at 120 bpm, swing offset exact, ratchets ×4, gates, host stop releases voices, loop jumps resync, and a pattern swap frees the old pattern only after its voices finish.
- pluginval strictness 10 passes.

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

## Generate + Sequence

1. Discover fragments (ideally on VOCALS). Optionally **PIN** up to 8 in the Discover inspector, otherwise the generator uses the 6 that score highest for the mode.
2. GENERATE page: pick a mode, key/scale/range, and density / complexity / randomness / swing, then **GENERATE** (new seed) or **REBUILD** (same seed).
3. In Live the pattern plays whenever the transport runs (Sync: host transport). In the standalone app, press PLAY.
4. SEQUENCE page: edit the pattern by lanes, one row per fragment. Click a cell to place that fragment, click again to remove it, drag up/down for velocity. The inspector covers note, gate, chop range, reverse, ratchet, layer and choke. Slots A–H hold 8 patterns.
5. Export a loop WAV or a MIDI file, or drag either straight into Live.

What each mode actually does (`Source/Generation/PatternGenerator.cpp`):

| Mode | Rhythm | Melody | Per-chop DSP |
|---|---|---|---|
| ETHEREAL | sparse, on strong beats, notes overlap | stepwise, motif of 8 | slowed ×1.3–2, 40/220 ms fades, +12/+7 shimmer layers, formant up to +1.5 st |
| HAUNTED | off-grid, syncopated | small steps, 25% chromatic "wrong" notes, tritones | 45% reversed, slowed ×1.6–2.6, long 350 ms tails, darker formants (−4 to −1.5 st), ghost layer an octave down |
| NOSTALGIA | swing-friendly boom-bap pocket | repetitive 4-step motif | near-natural speed, short chops, 55% fragment repetition, slight detune |
| HEAVEN | very sparse, long holds | slow stepwise | stretched to fill the gate, stacked +12/+7/+4 harmonies (80%), bright formants |
| CHAOS | dense | big leaps | ratchets ×2–4, random chop starts, ×0.5–1.5 speed, formant ±6 st, 1–4 ms fades |

Each mode also has a matching effects preset (EFFECTS → mode FX, or automatic with "FX follow the GENERATE mode"):

| Mode | FX |
|---|---|
| ETHEREAL | big bright reverb (6.5 s), dotted-1/8 ping-pong, chorus, high-pass |
| HAUNTED | huge dark reverb (9 s, heavy damping, 80 ms pre-delay), dotted-1/4 filtered delay, tilt dark, wow, faint hiss |
| NOSTALGIA | saturation, wow + flutter, hiss/crackle, 26 kHz / 12-bit lo-fi, short warm room |
| HEAVEN | 14 s bright reverb, 1/4 ping-pong, chorus, tilt bright |
| CHAOS | 1/16T delay at 70% feedback, 9 kHz / 7-bit crush, saturation, flutter |

## GHOST MATCH

Discover → **GHOST MATCH** → pick a reference vocal (any WAV/MP3). GHOSTCUT measures it (and only measures it: the reference is never played, stored or exported) and ranks your fragments by *feature similarity*:

| Feature | How | Weight |
|---|---|---|
| Pitch contour shape | semitones relative to its median, 32 points, DTW with a 25% band (key-independent) | 32% |
| Register | median pitch distance (gentle: chops get re-pitched anyway) | 14% |
| Timbre | 12-band spectral envelope, level-normalised | 14% |
| Length | fragment length + average note length | 12% |
| Breathiness / air | heuristic breathiness + energy above 5 kHz | 12% |
| Rhythm | onset rate + energy modulation | 9% |
| Harmonicity | periodicity + voiced share | 7% |

The inspector shows the per-feature breakdown. This is measurable similarity, not a promise that two sounds feel the same.

## Presets

The header bar holds presets: ◀ ▶ step through them, click the name for the categorised menu, and SAVE stores your own. A preset = every parameter (instrument, pattern sync, effects, pitch/formant) + GENERATE settings. It never includes audio, fragments, patterns or your seed. User presets live in `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Presets/<Category>/`. Factory presets also appear in Live's preset/program list.

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
| very / really + word | doubles its weight (`very breathy`) |
| `like #12`, `similar to 12` | similarity to fragment 12 (the GHOST MATCH features) |

**Optional language model (not included):** `Source/Analysis/QueryInterpreter.h` defines how a local or cloud model could translate free text into the same constraint JSON. GHOSTCUT validates that JSON (unknown features are rejected) and shows the constraints like any other search. Keyword search stays the offline default.

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
               SearchQuery, QueryInterpreter, ReferenceMatcher (GHOST MATCH), VocalSeparator (MDX + center extract + cache),
               OnnxRuntime (dynamic loader), Stft
  Generation/  Random (PCG32), ScaleQuantizer, Pattern, PatternGenerator (+ mode profiles), ChopRenderer
               (Signalsmith), PatternExport (loop WAV + MIDI)
  DSP/         Biquad / DelayLine / smoothers, Effects (Texture, LoFi, SyncDelay, FdnReverb, EffectsChain)
  FxParameters (effect parameters, readers, mode presets)
  Presets/     PresetManager (factory table, user files, host programs)
  UI/          + PatternView (roll / lanes editor), GeneratePanel, SequencePanel, EffectsPanel
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
| Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.6 | pitch shift, time stretch, formants | MIT | `ThirdParty/signalsmith-*`. Uses Accelerate on macOS |
| MDX-Net vocal models (Kim Vocal 2, UVR Voc FT) | vocal separation | **unclear for commercial redistribution** | not shipped. The user installs them, see above |
| pluginval (CI only, not shipped) | validation | GPLv3 | tool, not linked |

**Planned:**
| Component | Purpose | License | Note |
|---|---|---|---|
| Demucs (htdemucs) | possible higher-quality separation | code MIT | weights not reachable from the dev environment; MDX-Net chosen instead for Phase 2 |
| CREPE-tiny (optional) | ML pitch upgrade over YIN | MIT | only if YIN proves insufficient on real vocals |

---

## Project portability (how state is stored)

- The DAW project stores a **reference**, not the audio: the absolute path, the file size, a content fingerprint (FNV-1a over size + first/last 256 KB), and the trim range.
- Fragments (start/end in samples, manual flag, root override, measured features), selection, loop, sensitivity and export bit depth are stored in the project.
- Frame analysis is cached in the same folder (`*.gcff`, versioned), so reopening a project skips re-analysis.
- Isolated vocals are cached on disk in `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Cache` (key: fingerprint + trim + model). A reopened project reloads its vocals from there. If the cache was cleared, separation re-runs automatically (with progress). Frame measurements are recomputed in the background after load (seconds).
- The project also stores the separation method, the ORIGINAL/VOCALS view, granularity, sort and search text.
- Missing file → fragments are kept, the reference is kept (re-saving doesn't lose it), and LOCATE or dropping a file with the same name relinks it. A different file at the same path triggers a warning.
- Drag-to-DAW renders are written to `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Fragments/` (not a temp folder) so Live's references survive reboots.
- For full portability use Live's **File → Collect All and Save**, and keep sources inside the project folder.

## Known Phase 1 limitations
- ML separation runs on the CPU (~1.8× realtime with 4 threads). A 4-minute song takes about 2 minutes the first time, then it's instant from the cache. It deliberately uses half your cores so the DAW keeps playing.
- Character scores and breathiness are heuristics from measured features. The weights were tuned on synthetic material and should be tuned further on real vocals.
- Pitch tracking is YIN (70–1100 Hz). Very breathy or whispered material often has no stable pitch, which is shown as "no stable pitch".
- Stealing a voice when all 48 are busy restarts it without a crossfade (rare, may click).
- Chops are pre-rendered (pitch/stretch/formant) on a worker thread whenever the pattern, the view or the tempo changes (fit-to-gate modes). Edits take effect after a short render, usually well under a second.
- Re-running ANALYZE gives fragments new ids, so a pattern built on the old ones shows "MISSING" lanes until you regenerate.
- Signing needs your Apple Developer ID + a Windows code-signing certificate (see docs/SIGNING.md). Until then CI builds are unsigned.
- AU manufacturer code `T7Au` / plugin code `Ghct`. If other TRIPLE SEVEN AUDIO plugins already use a different manufacturer code, align it **before the first public release** (changing it later breaks saved projects).

---

© TRIPLE SEVEN AUDIO, triplesevenaudio.com
