#include "ReferenceMatcher.h"
#include "FeatureExtractor.h"
#include <algorithm>
#include <numeric>

namespace ghostcut
{

juce::String ReferenceMatcher::Match::describe() const
{
    auto pc = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    return "contour " + pc (contour) + "  register " + pc (reg) + "  timbre " + pc (spectrum)
         + "  length " + pc (duration) + "  breath " + pc (breath) + "  rhythm " + pc (rhythm);
}

ReferenceMatcher::Signature ReferenceMatcher::describe (const SourceSnapshot& src, const FrameFeatures& ff, int64_t start, int64_t end)
{
    Signature s;
    const int nf = ff.numFrames();
    if (nf == 0 || end <= start) return s;

    Fragment tmp;
    tmp.start = start; tmp.end = end;
    FeatureExtractor::measure (tmp, src, ff);
    s.f = tmp.features;
    s.medianNote = tmp.features.medianF0Hz > 0 ? (float) (69.0 + 12.0 * std::log2 (tmp.features.medianF0Hz / 440.0)) : -1.0f;

    const double hopSamples = ff.hopSeconds * src.sampleRate;
    const int a = juce::jlimit (0, nf - 1, (int) std::floor (start / hopSamples));
    const int b = juce::jlimit (a + 1, nf, (int) std::ceil (end / hopSamples));
    const int n = b - a;

    // contour: voiced frames, relative to median, resampled to a fixed length
    if (s.medianNote > 0)
    {
        std::vector<float> v;
        for (int f = a; f < b; ++f)
            if (ff.f0[(size_t) f] > 0)
                v.push_back ((float) (69.0 + 12.0 * std::log2 (ff.f0[(size_t) f] / 440.0)) - s.medianNote);
        if (v.size() >= 4)
        {
            s.contour.resize (contourPoints);
            for (int i = 0; i < contourPoints; ++i)
            {
                const float pos = (float) i / (contourPoints - 1) * (float) (v.size() - 1);
                const int i0 = (int) pos;
                const int i1 = std::min ((int) v.size() - 1, i0 + 1);
                s.contour[(size_t) i] = v[(size_t) i0] + (pos - (float) i0) * (v[(size_t) i1] - v[(size_t) i0]);
            }
        }
    }

    // spectral envelope over the louder frames, mean-removed (level independent)
    const float peak = s.f.peakDb;
    int count = 0;
    for (int f = a; f < b; ++f)
    {
        if (ff.db[(size_t) f] < peak - 25.0f) continue;
        for (int k = 0; k < FrameFeatures::numBands; ++k) s.spectrum[(size_t) k] += ff.bands[(size_t) f * FrameFeatures::numBands + (size_t) k];
        ++count;
    }
    if (count > 0)
    {
        float mean = 0.0f;
        for (auto& v : s.spectrum) { v /= (float) count; mean += v; }
        mean /= FrameFeatures::numBands;
        for (auto& v : s.spectrum) v -= mean;
    }

    // rhythm: onsets (flux peaks over the region's mean) and envelope modulation
    double fluxMean = 0.0;
    for (int f = a; f < b; ++f) fluxMean += ff.flux[(size_t) f];
    fluxMean /= n;
    int onsets = 0;
    for (int f = a + 1; f + 1 < b; ++f)
        if (ff.flux[(size_t) f] > 2.5 * fluxMean && ff.flux[(size_t) f] >= ff.flux[(size_t) f - 1] && ff.flux[(size_t) f] >= ff.flux[(size_t) f + 1]
            && ff.db[(size_t) f] > peak - 30.0f)
            ++onsets;
    s.onsetsPerSec = (float) onsets / std::max (0.05f, (float) (n * ff.hopSeconds));
    double m = 0, sq = 0; int c = 0;
    for (int f = a; f < b; ++f) if (ff.db[(size_t) f] > peak - 40.0f) { m += ff.db[(size_t) f]; sq += ff.db[(size_t) f] * ff.db[(size_t) f]; ++c; }
    if (c > 0) { m /= c; s.energyModulation = (float) std::sqrt (std::max (0.0, sq / c - m * m)); }

    // note duration: mean length of voiced runs
    int runs = 0, voicedFrames = 0, cur = 0;
    for (int f = a; f <= b; ++f)
    {
        const bool v = f < b && ff.f0[(size_t) f] > 0;
        if (v) { ++cur; ++voicedFrames; }
        else if (cur > 0) { ++runs; cur = 0; }
    }
    s.meanNoteSec = runs > 0 ? (float) (voicedFrames * ff.hopSeconds / runs) : 0.0f;
    s.valid = true;
    return s;
}

ReferenceMatcher::Signature ReferenceMatcher::describeWhole (const SourceSnapshot& src, const FrameFeatures& ff)
{
    // the audible span: first to last frame within 40 dB of the loudest frame
    const int nf = ff.numFrames();
    if (nf == 0) return {};
    const float peak = *std::max_element (ff.db.begin(), ff.db.end());
    int a = 0, b = nf - 1;
    while (a < nf && ff.db[(size_t) a] < peak - 40.0f) ++a;
    while (b > a && ff.db[(size_t) b] < peak - 40.0f) --b;
    const double hopSamples = ff.hopSeconds * src.sampleRate;
    return describe (src, ff, (int64_t) (a * hopSamples), std::min<int64_t> (src.numSamples(), (int64_t) ((b + 1) * hopSamples)));
}

float ReferenceMatcher::contourDistance (const std::vector<float>& x, const std::vector<float>& y)
{
    const int n = (int) x.size(), m = (int) y.size();
    if (n == 0 || m == 0) return 12.0f;
    // DTW with a Sakoe-Chiba band of 25% so a shape can't be stretched beyond recognition
    const int band = std::max (2, std::max (n, m) / 4);
    std::vector<float> D ((size_t) (n + 1) * (size_t) (m + 1), std::numeric_limits<float>::infinity());
    auto at = [&] (int i, int j) -> float& { return D[(size_t) i * (size_t) (m + 1) + (size_t) j]; };
    at (0, 0) = 0.0f;
    for (int i = 1; i <= n; ++i)
        for (int j = std::max (1, i - band); j <= std::min (m, i + band); ++j)
            at (i, j) = std::abs (x[(size_t) i - 1] - y[(size_t) j - 1]) + std::min ({ at (i - 1, j), at (i, j - 1), at (i - 1, j - 1) });
    return at (n, m) / (float) (n + m) * 2.0f;
}

ReferenceMatcher::Match ReferenceMatcher::compare (const Signature& r, const Signature& c)
{
    Match m;
    if (! r.valid || ! c.valid) return m;
    auto sim = [] (float diff, float scale) { return std::exp (-std::abs (diff) / scale); };

    const bool bothPitched = r.medianNote > 0 && c.medianNote > 0;
    m.reg = bothPitched ? sim (r.medianNote - c.medianNote, 6.0f) : (r.medianNote <= 0 && c.medianNote <= 0 ? 0.7f : 0.1f);
    m.contour = (! r.contour.empty() && ! c.contour.empty()) ? sim (contourDistance (r.contour, c.contour), 1.2f)
                                                            : (r.contour.empty() && c.contour.empty() ? 0.5f : 0.15f);
    m.duration = sim (std::log2 (std::max (0.05f, r.f.durationSec) / std::max (0.05f, c.f.durationSec)), 0.8f) * 0.5f
               + sim (std::log2 (std::max (0.03f, r.meanNoteSec) / std::max (0.03f, c.meanNoteSec)), 0.8f) * 0.5f;
    m.breath = sim (r.f.breathiness - c.f.breathiness, 0.15f) * 0.6f + sim (r.f.airiness - c.f.airiness, 0.05f) * 0.4f;
    float spec = 0.0f;
    for (int k = 0; k < FrameFeatures::numBands; ++k) spec += std::abs (r.spectrum[(size_t) k] - c.spectrum[(size_t) k]);
    m.spectrum = sim (spec / FrameFeatures::numBands, 5.0f);
    m.rhythm = sim (r.onsetsPerSec - c.onsetsPerSec, 1.5f) * 0.5f + sim (r.energyModulation - c.energyModulation, 4.0f) * 0.5f;
    m.harmonic = sim (r.f.harmonicity - c.f.harmonicity, 0.15f) * 0.5f + sim (r.f.voicedRatio - c.f.voicedRatio, 0.2f) * 0.5f;

    // Shape first: GHOSTCUT re-pitches chops anyway, so the melodic gesture matters more
    // than the key it was sung in.
    m.total = 0.32f * m.contour + 0.14f * m.reg + 0.14f * m.spectrum + 0.12f * m.duration
            + 0.12f * m.breath + 0.09f * m.rhythm + 0.07f * m.harmonic;
    return m;
}

} // namespace ghostcut
