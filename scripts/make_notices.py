#!/usr/bin/env python3
"""Builds THIRD_PARTY_NOTICES.txt for GHOSTCUT installers.

Usage: make_notices.py <JUCE dir> <ONNX Runtime dir> <repo dir> <output file>

Only components that are actually compiled into or shipped with GHOSTCUT are listed.
"""
import pathlib, sys

juce, ort, repo, out = map(pathlib.Path, sys.argv[1:5])

def text(p):
    p = pathlib.Path(p)
    return p.read_text(encoding="utf-8", errors="replace").strip() if p.exists() else f"[license file not found: {p}]"

sections = [
    ("JUCE 8 (framework)", "Used under the JUCE 8 commercial licence (or AGPLv3). See https://juce.com/legal/juce-8-licence/",
     None),
    ("Steinberg VST3 SDK (via JUCE)", "MIT", juce / "modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"),
    ("Apple AudioUnitSDK (via JUCE, macOS AU only)", "Apache License 2.0", juce / "modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt"),
    ("FLAC (via JUCE)", "BSD", juce / "modules/juce_audio_formats/codecs/flac/Flac Licence.txt"),
    ("Ogg Vorbis (via JUCE)", "BSD", juce / "modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt"),
    ("zlib (via JUCE)", "zlib", juce / "modules/juce_core/zip/zlib/README"),
    ("libpng (via JUCE)", "libpng / zlib", juce / "modules/juce_graphics/image_formats/pnglib/LICENSE"),
    ("Independent JPEG Group jpeglib (via JUCE)", "IJG", juce / "modules/juce_graphics/image_formats/jpglib/README"),
    ("HarfBuzz (via JUCE)", "Old MIT", juce / "modules/juce_graphics/fonts/harfbuzz/COPYING"),
    ("SheenBidi (via JUCE)", "Apache License 2.0", juce / "modules/juce_graphics/unicode/sheenbidi/LICENSE"),
    ("ONNX Runtime", "MIT", ort / "LICENSE"),
    ("ONNX Runtime third-party notices", "various (see below)", ort / "ThirdPartyNotices.txt"),
    ("pocketfft (C++)", "BSD-3-Clause", repo / "ThirdParty/pocketfft/LICENSE.md"),
    ("Signalsmith Stretch", "MIT", repo / "ThirdParty/signalsmith-stretch/LICENSE.txt"),
    ("Signalsmith Linear", "MIT", repo / "ThirdParty/signalsmith-linear/LICENSE.txt"),
]

parts = ["GHOSTCUT by TRIPLE SEVEN AUDIO - third-party software notices", "=" * 64, "",
         "Vocal separation model weights are NOT included with GHOSTCUT. Models installed by",
         "the user are governed by their own licences.", ""]
for name, lic, path in sections:
    parts += ["-" * 64, f"{name}  [{lic}]", "-" * 64, text(path) if path else "", ""]
out.write_text("\n".join(parts), encoding="utf-8")
print(f"wrote {out} ({out.stat().st_size // 1024} KB)")
