#pragma once

#include "ScaleQuantizer.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

namespace ghostcut
{

enum class GenMode { Ethereal = 0, Haunted, Nostalgia, Heaven, Chaos, numModes };

inline const char* genModeName (GenMode m)
{
    static const char* n[] = { "ETHEREAL", "HAUNTED", "NOSTALGIA", "HEAVEN", "CHAOS" };
    return n[juce::jlimit (0, 4, (int) m)];
}

/** User-facing generation controls. Everything needed to reproduce a pattern. */
struct GenSettings
{
    GenMode mode = GenMode::Ethereal;
    int key = 9;                                  // pitch class, 9 = A
    ScaleQuantizer::Scale scale = ScaleQuantizer::Scale::Minor;
    int rootOctaveNote = 57;                      // lowest note of the melody range (A2 in Ableton naming)
    int octaveRange = 2;                          // 1..3
    int steps = 16;                               // 16 or 32
    int stepDiv = 4;                              // steps per beat: 4 = 1/16, 2 = 1/8, 8 = 1/32
    float density = 0.5f, complexity = 0.5f, randomness = 0.3f;
    float swing = 0.0f;                           // 0..0.75 (fraction of a step for off-beats)
    uint32_t seed = 777;

    juce::ValueTree toTree() const;
    static GenSettings fromTree (const juce::ValueTree&);
};

/** One extra pitched copy of an event (harmony / octave layer). */
struct Layer
{
    int semitones = 0;      // relative to the event's note
    float gain = 0.5f;
    float detuneCents = 0.0f;
};

/** A sounding chop placed on a step. */
struct StepEvent
{
    int slot = 0;           // index into Pattern::slotUids
    int note = 60;          // target MIDI note (chop is pitched from its root to this)
    float velocity = 0.8f;  // 0..1
    float gateSteps = 1.0f; // length in steps
    bool reverse = false;
    float startFrac = 0.0f, endFrac = 1.0f; // sub-range of the fragment
    float stretch = 1.0f;   // output length / input length (1 = natural)
    bool fitToGate = false; // stretch the chop to exactly fill its gate
    int ratchet = 1;        // 1..4 repeats inside the step
    float fadeInMs = 3.0f, fadeOutMs = 10.0f;
    float formantSemis = 0.0f;
    bool choke = true;      // a new event cuts this one (mono-style chopping)
    std::vector<Layer> layers;
};

/** A step pattern: up to 32 steps, each holding zero or one event. */
struct Pattern
{
    int length = 16;
    int stepDiv = 4;
    float swing = 0.0f;
    std::vector<int64_t> slotUids;                 // fragments used (by Fragment::uid)
    std::vector<int> slotRoots;                    // root note per slot (for pitch maths)
    std::array<bool, 32> active {};
    std::array<StepEvent, 32> events {};
    GenSettings madeWith;                          // provenance: reproduce with the same seed

    int numEvents() const { int c = 0; for (int i = 0; i < length; ++i) c += active[(size_t) i] ? 1 : 0; return c; }

    juce::ValueTree toTree() const;
    static Pattern fromTree (const juce::ValueTree&);
};

} // namespace ghostcut
