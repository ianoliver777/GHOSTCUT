# Installing GHOSTCUT

## macOS (Apple Silicon + Intel, macOS 11+)

**Installer (recommended):** open `GHOSTCUT-<version>-macOS.pkg` and follow the steps. It installs:

| What | Where |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3/GHOSTCUT.vst3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components/GHOSTCUT.component` |
| Standalone app | `/Applications/GHOSTCUT.app` |

Use **Customize** on the "Installation Type" page to skip any of them.

**Unsigned test builds** (CI artifacts before signing is set up): macOS will block them. Either right-click the .pkg → Open, or clear the quarantine flag after a manual copy:
```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/GHOSTCUT.vst3 ~/Library/Audio/Plug-Ins/Components/GHOSTCUT.component
```

**Ableton Live 12:** Settings → Plug-Ins → turn on *Use Audio Units v2* and/or *VST3 Plug-In System Folders* → **Rescan**. GHOSTCUT shows up under Plug-Ins → TRIPLE SEVEN AUDIO as an instrument.

**Uninstall:** delete the three items above. Your presets, models, cache and exports live in `~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/` and are kept until you delete that folder.

## Windows 10 / 11 (x64)

Run `GHOSTCUT-<version>-Windows-Setup.exe` (administrator rights are needed for the shared VST3 folder):

| What | Where |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\GHOSTCUT.vst3` |
| Standalone app | `C:\Program Files\TRIPLE SEVEN AUDIO\GHOSTCUT\GHOSTCUT.exe` |

ONNX Runtime (`onnxruntime.dll`) is installed inside the VST3 bundle and next to the standalone exe. GHOSTCUT loads it from there by full path, so it never conflicts with another copy on your system.

Unsigned test builds trigger SmartScreen: choose **More info → Run anyway**.

**Ableton Live 12 (Windows):** Settings → Plug-Ins → turn on *VST3 Plug-In System Folders* → **Rescan**.

**Uninstall:** Settings → Apps → GHOSTCUT → Uninstall.

## Optional: ML vocal separation model

Models are not included (see `LICENSING_REVIEW.md`). Download `Kim_Vocal_2.onnx` or `UVR-MDX-NET-Voc_FT.onnx` (links in the README), then GHOSTCUT → SETTINGS → **Install model…**. Without a model, SEPARATE uses the DSP center-extract method.

## Folders GHOSTCUT uses

`~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/` (on Windows: `Music\TRIPLE SEVEN AUDIO\GHOSTCUT\`)

| Folder | Contents | Safe to delete? |
|---|---|---|
| `Presets/` | your user presets | only if you don't want them |
| `Models/` | installed separation models | yes (re-install to use ML) |
| `Cache/` | separated vocals + analysis (`*.wav`, `*.gcff`) | yes: it's rebuilt as needed (SETTINGS → Clear stem cache) |
| `Fragments/`, `Loops/`, `MIDI/` | drag-to-DAW renders | only if no Live set still uses them |
