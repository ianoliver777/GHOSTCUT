#pragma once

#include "../Core/Fragment.h"
#include <juce_core/juce_core.h>

namespace ghostcut
{

/**
    Offline structured search. A description such as
        "airy high-pitched vocal with a sustained vowel"
    is turned into measurable constraints (register, sustain, breathiness, ...).
    This is keyword-to-feature mapping, NOT semantic language understanding: words
    it doesn't know are listed as ignored so the user can see exactly what was used.
    An optional language-model front end (Phase 5) would only produce these same
    constraints.
*/
class SearchQuery
{
public:
    enum class Feature
    {
        Note, Duration, Sustain, Breathiness, Airiness, Attack, Decay, Slope, Bend, Range,
        Vibrato, Centroid, Level, Stability, Harmonicity, Jumps, Voiced,
        ScoreEthereal, ScoreHaunted, ScoreNostalgic, ScoreHeavenly, ScoreExperimental
    };

    struct Constraint
    {
        juce::String label;     // shown to the user, e.g. "high register >= G4"
        Feature feature;
        float lo, hi;           // satisfaction ramps from 0 at lo to 1 at hi (hi < lo = "lower is better")
        float weight = 1.0f;
        bool band = false;      // if true: 1 at lo..hi centre, falling to 0 at +-(hi-lo) from it
    };

    static SearchQuery parse (const juce::String& text);

    bool isEmpty() const noexcept { return constraints.empty() && likeFragment < 0; }
    /** 0..1, mean satisfaction over all constraints. */
    float match (const Fragment& f) const;

    std::vector<Constraint> constraints;
    juce::StringArray ignoredWords;
    int likeFragment = -1;      // "like #12" / "similar to 12": 1-based fragment number, -1 = none

    /** Constraints in JSON, for an optional external interpreter (see OptionalInterpreter.h). */
    juce::var toJson() const;
    static SearchQuery fromJson (const juce::var& json, juce::String& error);

    static float featureValue (const Fragment& f, Feature feat);
};

} // namespace ghostcut
