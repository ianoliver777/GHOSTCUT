#pragma once

#include "../Core/SourceSnapshot.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace ghostcut
{

/** Decodes WAV / MP3 / AIFF / FLAC into a SourceSnapshot. Runs on worker threads only. */
class AudioImporter
{
public:
    struct Result
    {
        std::shared_ptr<SourceSnapshot> snapshot; // null on failure
        juce::String error;
    };

    static constexpr double maxLengthSeconds = 30.0 * 60.0;

    AudioImporter();

    /** File extensions accepted for drag-and-drop, e.g. "*.wav;*.mp3". */
    juce::String getWildcard() const;
    bool canImport (const juce::File& f) const;

    /** Loads the file (optionally trimmed to [trimStart, trimEnd) in file samples).
        progress receives 0..1; return false from shouldContinue to cancel. */
    Result load (const juce::File& file,
                 int64_t trimStart, int64_t trimEnd,
                 const std::function<void (float)>& progress,
                 const std::function<bool()>& shouldContinue);

    /** Fast fingerprint: file size + hash of the first and last 256 KB. */
    static juce::String computeContentHash (const juce::File& file);

    /** Builds a new snapshot that holds only [start, end) of an existing one. */
    static std::shared_ptr<SourceSnapshot> crop (const SourceSnapshot& src, int64_t start, int64_t end);

private:
    juce::AudioFormatManager formats;
    juce::CriticalSection formatLock;
};

} // namespace ghostcut
