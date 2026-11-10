# GHOSTCUT user guide

*FIND THE FEELING. TRIPLE SEVEN AUDIO, Engineered to Feel.*

GHOSTCUT finds the most usable vocal moments in a recording and turns them into atmospheric chops you can play, sequence and export.

## 60-second start
1. Load GHOSTCUT on a **MIDI track** in Live.
2. **Drag a song or an acapella** (WAV/MP3) onto the plug-in. Fragments appear automatically.
3. For a full song, press **SEPARATE**: the drums and music drop away and fragments are re-detected on the voice.
4. Click fragments to audition them. **↑/↓** steps through them.
5. Play your keyboard: C3 = fragment 1, C#3 = fragment 2, and so on.
6. Go to **GENERATE**, pick a mode, press **GENERATE**, then press play in Live.
7. Drag the loop (or MIDI) out to an audio track, or keep it playing inside GHOSTCUT.

## DISCOVER
| Control | What it does |
|---|---|
| IMPORT / drag & drop | WAV, MP3, AIFF, FLAC, mono or stereo, any sample rate, up to 30 min |
| ANALYZE + Phrases/Syllables + SENSITIVITY | re-detect fragments. Syllables = smaller chops at note onsets |
| SEPARATE · ORIGINAL / VOCALS | isolate vocals (ML model or DSP center extract). Hear/export either; fragments are shared |
| Waveform | drag = select · double-click = play from here · ⌘/Ctrl+wheel or pinch = zoom · wheel = scroll |
| Fragment lane | click a block to select/audition · drag the selected block's edges to adjust |
| TRIM · + FRAGMENT | crop to the selection (the file on disk is untouched) · turn the selection into a fragment |
| Search box | describe it: *"airy high sustained vowel"*, *"short ghostly for reversing"*, *"like #12"*, *"very breathy under 2 seconds"*. The line underneath shows what it understood and what it ignored |
| Sort | time, a character score, search match, or GHOST MATCH |
| GHOST MATCH | pick a reference vocal; fragments are ranked by measured similarity (shape, register, timbre, length, breath, rhythm) |
| Inspector | measurements, character scores with reasons, root note, PIN (for GENERATE), drag to DAW, export WAV |

**Character scores** (ETHEREAL, HAUNTED, NOSTALGIC, HEAVENLY, EXPERIMENTAL) are heuristics built from measurable features. Use them to sort, not as truth.

## Playing it (MIDI instrument strip)
- **KEY MODE**: *Slices* = one fragment per key from BASE upward. *Chromatic* = the selected fragment across the keyboard, at its original pitch on its ROOT key.
- **TRIGGER**: *One-shot* plays the whole chop. *Gate* stops on key release (with RELEASE).
- ATTACK, RELEASE, VELOCITY sensitivity, OUTPUT.

## GENERATE
1. Choose a **mode**: ETHEREAL, HAUNTED, NOSTALGIA, HEAVEN or CHAOS. Each changes rhythm, melody, layering and per-chop processing (stretch, reverse, formant).
2. **Key / scale / range**, **steps** (16/32) and **rate** (1/8, 1/16, 1/32).
3. **Density · Complexity · Randomness · Swing**.
4. **GENERATE** = new seed. **REBUILD** = same seed with your new settings. Note a seed you like: the same seed + settings + fragments gives the same pattern.
5. Chops come from your PINNED fragments, or the 6 best for the mode.
6. **Sync: host transport** plays the pattern whenever Live plays. Without a host, use PLAY and the fallback tempo.
7. Export a loop WAV (seamless, with effects) or MIDI, or drag either straight to Live.

## SEQUENCE
- Slots **A–H** each hold a pattern (saved in your Live set).
- Lanes = fragments. Click a cell to place that fragment, click a selected hit again to remove it, drag up/down for velocity.
- The step inspector covers note ±1/±12, velocity, gate, chop start/end, reverse, choke, ratchet ×2–4 and harmony layer.
- RANDOMIZE builds a fresh pattern in this slot; CLEAR empties it.

## EFFECTS
TEXTURE → LO-FI → DELAY → REVERB run on GHOSTCUT's output (live and in loop exports). PITCH / FORMANT and REVERSE are applied when pattern chops are rendered (best quality); PITCH also transposes keyboard playing. **Mode FX** buttons load the chain designed for each mode. "FX follow the GENERATE mode" does that automatically.

## Presets
◀ ▶ in the header step through presets, click the name for the categorised menu, and SAVE stores your own. Presets hold the sound (parameters + GENERATE settings), never audio, fragments, patterns or seeds.

## Shortcuts
| Key | Action |
|---|---|
| Space | play / stop the source (Discover) |
| ↑ / ↓ | previous / next fragment (in the list's order) |
| Delete | delete the selected fragment |
| ⌘/Ctrl + F | jump to search |
| ⌘/Ctrl + 1…5 | DISCOVER / GENERATE / SEQUENCE / EFFECTS / SETTINGS |
| ⌘/Ctrl + G | generate (new seed) |
| ⌘/Ctrl + Shift + G | rebuild (same seed) |
| ⌘/Ctrl + P | play / stop the pattern |

## Projects and files
Your Live set stores a *reference* to the source file (path + fingerprint), fragments, vocals view, patterns, search, GHOST MATCH reference and all settings. Isolated vocals and analysis reload from the cache. If a source file moves, GHOSTCUT keeps everything and asks you to LOCATE it. Use Live's **Collect All and Save** to keep sources with the project.

## Sample rights
GHOSTCUT helps you discover and transform material. You're responsible for having the rights to the recordings you import and release.
