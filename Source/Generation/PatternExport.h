#pragma once

#include "ChopRenderer.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ghostcut
{

/** Offline exports of a pattern. Timing rules match SamplerEngine's sequencer
    (swing, ratchets, gates, choke) so what you export is what you heard. */
class PatternExport
{
public:
    /** Mixes `loops` repetitions at the clips' sample rate. With wrapTail, sound that
        rings past the loop end is folded back onto the start so the file loops seamlessly. */
    static juce::AudioBuffer<float> renderLoop (const RenderedPattern& rp, double bpm, int loops = 1, bool wrapTail = true);

    /** Notes = the pitches the chops were tuned to (layers included). 960 PPQ. */
    static juce::MidiFile toMidi (const Pattern& p, double bpm);

    static double loopSeconds (const Pattern& p, double bpm);
};

} // namespace ghostcut
