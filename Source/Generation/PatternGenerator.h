#pragma once

#include "Pattern.h"
#include "../Core/Fragment.h"

namespace ghostcut
{

/**
    The musical character of each generation mode, as concrete numbers.
    Sequencing (rhythm, gates, repetition), melody (interval habits, harmony
    layers) and per-chop DSP (stretch, reverse, fades, formant) all differ per mode.
    Phase 4 adds a matching effects chain per mode.
*/
struct ModeProfile
{
    std::array<float, 16> rhythm;   // hit weight per 1/16 position within a bar
    float densityScale;             // how many hits density=1 produces (relative)
    float gateMin, gateMax;         // steps
    bool fitToGate;                 // stretch chops to their gate
    float stretchMin, stretchMax;   // when not fitting
    float reverseProb;
    float repeatProb;               // reuse the previous slot
    float ratchetProb;
    float randomStartProb;          // start somewhere inside the fragment
    std::vector<int> stepIntervals; // scale-degree moves favoured by the melody walk
    float leapProb;                 // chance of a big jump instead
    float chromaticProb;            // chance of an out-of-scale "wrong" note (HAUNTED)
    std::vector<int> layerSemis;    // harmony layers to choose from (0 = none)
    float layerProb, layerGain, layerDetune;
    float fadeInMs, fadeOutMs;
    float formantMin, formantMax;   // semitones
    float velBase, velAccent;
    bool choke;                     // new hits cut old ones
    int motifLength;                // steps before the melody repeats (with variation)
};

const ModeProfile& getModeProfile (GenMode);

/** One fragment offered to the generator. */
struct GenSlot
{
    int64_t uid = 0;
    int root = 60;                    // pitch the chop plays at with transpose 0
    float durationSec = 1.0f;
    float modeScore = 0.5f;           // how well it suits the chosen mode (0..1)
};

/**
    Deterministic pattern generation. Same settings + same seed + same slots
    = the same pattern, always. Pure function: no audio, no threads, no globals.
*/
class PatternGenerator
{
public:
    static Pattern generate (const GenSettings&, const std::vector<GenSlot>& slots, double bpm);

    /** Picks up to `maxSlots` fragments for a mode by their matching score. */
    static std::vector<GenSlot> pickSlots (const std::vector<Fragment>& frags, GenMode mode, int maxSlots = 6);
    static float modeScore (const Fragment& f, GenMode mode);
};

} // namespace ghostcut
