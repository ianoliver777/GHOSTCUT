#pragma once

#include "PhraseSegmenter.h"
#include <array>

namespace ghostcut
{

/**
    GHOST MATCH: feature-based similarity between a reference vocal and fragments.

    Each region gets a Signature of measurable characteristics:
      pitch contour (shape, key-independent), register, note duration, breathiness,
      spectral envelope (12 log bands), rhythmic structure (onset rate, energy
      modulation) and harmonicity.
    Similarity is a weighted mix of per-feature similarities in 0..1.

    This finds fragments with *similar measurable characteristics*. It does not
    claim perceptual similarity, and it never copies the reference: the reference
    is only measured.
*/
class ReferenceMatcher
{
public:
    static constexpr int contourPoints = 32;

    struct Signature
    {
        FragmentFeatures f;
        float medianNote = -1.0f;            // MIDI, -1 = unpitched
        std::vector<float> contour;          // semitones relative to the median, resampled; empty if unpitched
        std::array<float, FrameFeatures::numBands> spectrum {}; // mean band dB, normalised to 0 mean
        float onsetsPerSec = 0.0f;
        float energyModulation = 0.0f;       // std of the dB envelope
        float meanNoteSec = 0.0f;            // average voiced run length
        bool valid = false;
    };

    struct Match
    {
        float total = 0.0f;
        // per-feature similarities 0..1 (shown to the user)
        float contour = 0, reg = 0, duration = 0, breath = 0, spectrum = 0, rhythm = 0, harmonic = 0;
        juce::String describe() const;
    };

    static Signature describe (const SourceSnapshot& src, const FrameFeatures& frames, int64_t start, int64_t end);
    /** Signature of a whole file's audible region (for the reference). */
    static Signature describeWhole (const SourceSnapshot& src, const FrameFeatures& frames);
    static Match compare (const Signature& reference, const Signature& candidate);

    /** Dynamic-time-warped mean absolute distance between two contours (semitones). */
    static float contourDistance (const std::vector<float>& a, const std::vector<float>& b);
};

} // namespace ghostcut
