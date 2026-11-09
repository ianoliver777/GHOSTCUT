#pragma once

#include "../Core/SourceSnapshot.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace ghostcut
{

/** Writes unprocessed regions of the source to WAV at the source's own sample rate,
    so nothing is resampled. Processed / generated exports arrive in later phases. */
class AudioExporter
{
public:
    enum class BitDepth { Int16 = 16, Int24 = 24, Float32 = 32 };

    /** Returns an empty string on success, otherwise an error message. */
    static juce::String writeRegion (const SourceSnapshot& src, int64_t start, int64_t end,
                                     const juce::File& dest, BitDepth depth);

    /** Folder for drag-to-DAW renders. Lives in the user's Music folder (not temp) so
        the DAW's reference to the dragged file stays valid after a reboot. */
    static juce::File getDragExportFolder();

    static juce::String makeFragmentFileName (const SourceSnapshot& src, int fragmentNumber,
                                              int64_t start);
};

} // namespace ghostcut
