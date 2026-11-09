#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <vector>

namespace ghostcut
{

/** Measured features of a fragment. Everything here is a direct DSP measurement,
    not a subjective judgement. Phase 2 adds the labelled heuristic scores
    (ETHEREAL / HAUNTED / ...) on top of these. */
struct FragmentFeatures
{
    float medianF0Hz          = 0.0f;  // 0 = no reliable pitch found
    float pitchStabilitySemis = 0.0f;  // std-dev of voiced pitch, in semitones
    float voicedRatio         = 0.0f;  // 0..1 fraction of frames judged periodic
    float meanDb              = -100.0f;
    float peakDb              = -100.0f;
    float centroidHz          = 0.0f;  // mean spectral centroid (brightness proxy)
};

/** One vocal fragment: a region of the current source, in source-sample units. */
struct Fragment
{
    int64_t uid   = 0;     // stable id, survives re-sorting / edits
    int64_t start = 0;     // inclusive, samples in the source snapshot
    int64_t end   = 0;     // exclusive
    bool manual   = false; // created by the user rather than the detector

    int detectedNote = -1; // MIDI note from medianF0Hz, -1 = unknown
    int rootOverride = -1; // user override, -1 = use detected

    FragmentFeatures features;

    int64_t length() const noexcept { return end - start; }

    /** Root used for chromatic playback. Falls back to the given default. */
    int effectiveRoot (int fallback) const noexcept
    {
        if (rootOverride >= 0) return rootOverride;
        if (detectedNote >= 0) return detectedNote;
        return fallback;
    }
};

/** Compact, immutable table the audio thread reads. Built on the message thread. */
struct PlayFragment
{
    int64_t start = 0;
    int64_t end   = 0;
    int     root  = -1;
};

struct FragmentTable
{
    std::vector<PlayFragment> fragments;
};

inline int frequencyToMidiNote (float hz) noexcept
{
    if (hz <= 0.0f) return -1;
    return (int) std::lround (69.0 + 12.0 * std::log2 ((double) hz / 440.0));
}

/** Note names using Ableton's convention (MIDI 60 = C3). */
inline juce::String noteName (int midiNote)
{
    if (midiNote < 0 || midiNote > 127) return "--";
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}

} // namespace ghostcut
