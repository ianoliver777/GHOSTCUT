#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace ghostcut
{

/** Keys, scales and pitch quantisation. Pure functions, no state. */
class ScaleQuantizer
{
public:
    enum class Scale { Minor = 0, Major, Dorian, Phrygian, HarmonicMinor, PentatonicMinor, PentatonicMajor, Chromatic, numScales };

    static juce::StringArray scaleNames();
    static juce::StringArray keyNames();             // C, C#, ... B
    static const std::vector<int>& intervals (Scale);  // semitones from root

    /** MIDI note for a scale degree (can be negative / beyond one octave) above rootMidi. */
    static int degreeToMidi (int rootMidi, Scale, int degree);
    /** Nearest in-scale note to `midi` (ties resolve downward). */
    static int quantize (int midi, int keyPitchClass, Scale);
    static bool inScale (int midi, int keyPitchClass, Scale);
};

} // namespace ghostcut
