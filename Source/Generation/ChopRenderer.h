#pragma once

#include "Pattern.h"
#include "../Core/SourceSnapshot.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace ghostcut
{

/** Offline rendering of one chop: sub-range, reverse, pitch shift (formant-preserving),
    time stretch, formant shift and fades. Uses Signalsmith Stretch (MIT).
    Worker threads only: allocates. */
class ChopRenderer
{
public:
    struct Request
    {
        int64_t start = 0, end = 0;      // fragment bounds in the source
        float startFrac = 0.0f, endFrac = 1.0f;
        bool reverse = false;
        float semitones = 0.0f;          // total pitch shift (incl. layer + detune)
        float stretch = 1.0f;            // output length / input length
        int64_t fixedOutputLength = -1;  // >0: stretch to exactly this many samples (fit-to-gate)
        float formantSemis = 0.0f;       // extra formant shift; pitch shifts keep formants by default
        bool preserveFormants = true;
        float fadeInMs = 3.0f, fadeOutMs = 10.0f;
    };

    static juce::AudioBuffer<float> render (const SourceSnapshot& src, const Request& r);

    static constexpr double maxOutputSeconds = 20.0;
};

/** A pattern plus every chop it needs, rendered and immutable. This is what the
    audio thread plays. Clips are at the source sample rate. */
struct RenderedPattern
{
    struct Hit { int clip = -1; float gain = 1.0f; };

    Pattern pattern;
    double sampleRate = 44100.0;
    std::vector<juce::AudioBuffer<float>> clips;
    std::array<std::vector<Hit>, 32> hits;       // per step: main chop + layers
};

/** Builds a RenderedPattern (worker thread). Missing slots are skipped. */
std::shared_ptr<RenderedPattern> renderPattern (const Pattern& p, const SourceSnapshot& src,
                                                const std::vector<std::pair<int64_t, int64_t>>& slotBounds,
                                                double bpm, const std::function<bool()>& keepGoing = {});

} // namespace ghostcut
