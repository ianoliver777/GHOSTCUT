#include "FragmentRanker.h"
#include <algorithm>

namespace ghostcut
{

namespace
{
    float ramp (float x, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, (x - lo) / (hi - lo)); }
    float bell (float x, float lo, float hi) // 1 inside [lo, hi], falls off over half the width outside
    {
        const float w = (hi - lo) * 0.5f;
        if (x < lo) return juce::jlimit (0.0f, 1.0f, 1.0f - (lo - x) / w);
        if (x > hi) return juce::jlimit (0.0f, 1.0f, 1.0f - (x - hi) / w);
        return 1.0f;
    }
    float note (const FragmentFeatures& f) { return f.medianF0Hz > 0 ? (float) (69.0 + 12.0 * std::log2 (f.medianF0Hz / 440.0)) : -1.0f; }
}

std::vector<FragmentRanker::Term> FragmentRanker::terms (const FragmentFeatures& f, int kind)
{
    const float n = note (f);
    const bool pitched = n > 0 && f.voicedRatio > 0.2f;
    auto P = [pitched] (float v) { return pitched ? v : 0.0f; };

    switch (kind)
    {
        case FragmentScores::Ethereal: return {
            { "high register",  0.25f, P (ramp (n, 60, 76)) },
            { "breathy",        0.25f, ramp (f.breathiness, 0.2f, 0.6f) },
            { "airy top end",   0.15f, ramp (f.airiness, 0.01f, 0.12f) },
            { "soft attack",    0.15f, ramp (f.attackSec, 0.03f, 0.25f) },
            { "held note",      0.20f, ramp (f.sustainSec, 0.2f, 1.2f) } };

        case FragmentScores::Haunted: return {
            { "dark tone",      0.25f, 1.0f - ramp (f.centroidHz, 800, 3500) },
            { "long decay",     0.20f, ramp (f.decaySec, 0.1f, 0.8f) },
            { "pitch bends",    0.20f, P (ramp (f.bendSemis, 0.5f, 4.0f)) },
            { "unstable pitch", 0.15f, P (ramp (f.pitchStabilitySemis, 0.3f, 1.5f)) },
            { "low register",   0.10f, P (1.0f - ramp (n, 52, 70)) },
            { "quiet",          0.10f, 1.0f - ramp (f.meanDb, -40, -14) } };

        case FragmentScores::Nostalgic: return {
            { "gentle vibrato", 0.30f, f.vibratoRateHz > 0 ? bell (f.vibratoDepthSemis, 0.15f, 0.8f) : 0.0f },
            { "mid register",   0.20f, P (bell (n, 55, 70)) },
            { "warm tone",      0.20f, bell (f.centroidHz, 900, 2500) },
            { "held note",      0.20f, ramp (f.sustainSec, 0.2f, 1.0f) },
            { "steady melody",  0.10f, P (1.0f - ramp (std::abs (f.pitchSlope), 1.0f, 6.0f)) } };

        case FragmentScores::Heavenly: return {
            { "high register",  0.25f, P (ramp (n, 62, 79)) },
            { "pure tone",      0.25f, ramp (f.harmonicity, 0.6f, 0.95f) },
            { "stable pitch",   0.20f, P (1.0f - ramp (f.pitchStabilitySemis, 0.2f, 1.2f)) },
            { "held note",      0.20f, ramp (f.sustainSec, 0.3f, 1.5f) },
            { "clean, not breathy", 0.10f, 1.0f - ramp (f.breathiness, 0.2f, 0.6f) } };

        case FragmentScores::Experimental: return {
            { "pitch jumps",    0.30f, ramp (f.jumpsPerSec, 0.5f, 4.0f) },
            { "big glides",     0.20f, P (ramp (f.bendSemis, 1.5f, 7.0f)) },
            { "wide range",     0.20f, P (ramp (f.pitchRangeSemis, 4.0f, 14.0f)) },
            { "busy spectrum",  0.15f, ramp (f.fluxMean, 0.15f, 0.5f) },
            { "noisy / unvoiced", 0.15f, 1.0f - f.voicedRatio } };

        default: return {};
    }
}

void FragmentRanker::score (Fragment& f)
{
    for (int k = 0; k < FragmentScores::numKinds; ++k)
    {
        float s = 0.0f, w = 0.0f;
        for (auto& t : terms (f.features, k)) { s += t.weight * t.value; w += t.weight; }
        f.scores.value[k] = w > 0 ? juce::jlimit (0.0f, 1.0f, s / w) : 0.0f;
    }
}

juce::String FragmentRanker::explain (const Fragment& f, int kind, int maxTerms)
{
    auto t = terms (f.features, kind);
    std::sort (t.begin(), t.end(), [] (const Term& a, const Term& b) { return a.weight * a.value > b.weight * b.value; });
    juce::StringArray parts;
    for (auto& x : t)
        if (x.value >= 0.5f && parts.size() < maxTerms) parts.add (x.label);
    return parts.isEmpty() ? juce::String ("no strong traits") : parts.joinIntoString (", ");
}

} // namespace ghostcut
