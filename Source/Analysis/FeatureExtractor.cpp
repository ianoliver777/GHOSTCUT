#include "FeatureExtractor.h"
#include <algorithm>
#include <numeric>

namespace ghostcut
{

namespace
{
    float pct (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) juce::jlimit (0.0f, (float) (v.size() - 1), p * (float) (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    }

    /** Longest run of consecutive voiced frames; returns [a, b). */
    std::pair<int, int> longestVoicedRun (const std::vector<float>& st)
    {
        int bestA = 0, bestB = 0, a = -1;
        for (int i = 0; i <= (int) st.size(); ++i)
        {
            const bool v = i < (int) st.size() && ! std::isnan (st[(size_t) i]);
            if (v && a < 0) a = i;
            if (! v && a >= 0) { if (i - a > bestB - bestA) { bestA = a; bestB = i; } a = -1; }
        }
        return { bestA, bestB };
    }
}

void FeatureExtractor::measure (Fragment& frag, const SourceSnapshot& src, const FrameFeatures& ff)
{
    FragmentFeatures out;
    const int nf = ff.numFrames();
    frag.measured = false;
    if (nf == 0 || frag.length() <= 0) { frag.features = out; frag.detectedNote = -1; return; }

    const double hop = ff.hopSeconds;
    const double hopSamples = hop * src.sampleRate;
    const int a = juce::jlimit (0, nf - 1, (int) std::floor (frag.start / hopSamples));
    const int b = juce::jlimit (a + 1, nf, (int) std::ceil (frag.end / hopSamples));
    const int n = b - a;
    out.durationSec = (float) ((double) frag.length() / src.sampleRate);

    // ---- level / spectrum --------------------------------------------------
    double dbSum = 0.0, cent = 0.0, flux = 0.0;
    float peak = -100.0f;
    for (int f = a; f < b; ++f)
    {
        dbSum += ff.db[(size_t) f];
        peak = std::max (peak, ff.db[(size_t) f]);
        cent += ff.centroid[(size_t) f];
        flux += ff.flux[(size_t) f];
    }
    out.meanDb = (float) (dbSum / n);
    out.peakDb = peak;
    out.centroidHz = (float) (cent / n);
    out.fluxMean = (float) (flux / n);

    // "active" frames: within 30 dB of the fragment's peak
    double hf = 0.0, flat = 0.0, aper = 0.0;
    int active = 0, voiced = 0;
    double clarSum = 0.0;
    for (int f = a; f < b; ++f)
    {
        if (ff.db[(size_t) f] < peak - 30.0f) continue;
        ++active;
        hf += ff.hfRatio.empty() ? 0.0 : ff.hfRatio[(size_t) f];
        flat += ff.flatness.empty() ? 0.0 : ff.flatness[(size_t) f];
        aper += 1.0 - ff.clarity[(size_t) f];
        if (ff.f0[(size_t) f] > 0.0f) { ++voiced; clarSum += ff.clarity[(size_t) f]; }
    }
    active = std::max (active, 1);
    out.airiness = (float) (hf / active);
    out.harmonicity = voiced > 0 ? (float) (clarSum / voiced) : 0.0f;
    // HEURISTIC breathiness: aperiodic energy + noise-like upper band
    out.breathiness = (float) juce::jlimit (0.0, 1.0, 0.6 * (aper / active) + 0.4 * std::sqrt (flat / active));

    int voicedAll = 0;
    for (int f = a; f < b; ++f) if (ff.f0[(size_t) f] > 0.0f) ++voicedAll;
    out.voicedRatio = (float) voicedAll / (float) n;

    // ---- envelope ----------------------------------------------------------
    int firstNearPeak = a, lastNearPeak = b - 1;
    for (int f = a; f < b; ++f) if (ff.db[(size_t) f] >= peak - 3.0f) { firstNearPeak = f; break; }
    for (int f = b - 1; f >= a; --f) if (ff.db[(size_t) f] >= peak - 3.0f) { lastNearPeak = f; break; }
    int lastAudible = lastNearPeak;
    for (int f = b - 1; f > lastNearPeak; --f) if (ff.db[(size_t) f] >= peak - 40.0f) { lastAudible = f; break; }
    out.attackSec = (float) ((firstNearPeak - a) * hop);
    out.decaySec  = (float) ((lastAudible - lastNearPeak) * hop);

    // ---- pitch contour (semitones relative to A4, NaN = unvoiced) ----------
    std::vector<float> st ((size_t) n, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> voicedHz;
    for (int f = a; f < b; ++f)
        if (ff.f0[(size_t) f] > 0.0f)
        {
            st[(size_t) (f - a)] = (float) (12.0 * std::log2 (ff.f0[(size_t) f] / 440.0));
            voicedHz.push_back (ff.f0[(size_t) f]);
        }

    if (voicedHz.size() >= 3)
    {
        out.medianF0Hz = pct (voicedHz, 0.5f);
        const float medSt = (float) (12.0 * std::log2 (out.medianF0Hz / 440.0));

        // octave-error guard: fold isolated frames that sit ~12 st away back toward the median
        for (auto& v : st)
            if (! std::isnan (v))
            {
                if (v - medSt > 9.0f) v -= 12.0f;
                else if (medSt - v > 9.0f) v += 12.0f;
            }

        std::vector<float> vs;
        for (auto v : st) if (! std::isnan (v)) vs.push_back (v);
        double mean = std::accumulate (vs.begin(), vs.end(), 0.0) / (double) vs.size();
        double sq = 0.0;
        for (auto v : vs) sq += (v - mean) * (v - mean);
        out.pitchStabilitySemis = (float) std::sqrt (sq / (double) vs.size());

        // smoothed contour (5-frame median over voiced neighbours)
        std::vector<float> sm (st.size(), std::numeric_limits<float>::quiet_NaN());
        for (int i = 0; i < n; ++i)
        {
            if (std::isnan (st[(size_t) i])) continue;
            std::vector<float> w;
            for (int k = -2; k <= 2; ++k)
                if (i + k >= 0 && i + k < n && ! std::isnan (st[(size_t) (i + k)])) w.push_back (st[(size_t) (i + k)]);
            sm[(size_t) i] = pct (w, 0.5f);
        }
        std::vector<float> smv;
        for (auto v : sm) if (! std::isnan (v)) smv.push_back (v);
        out.pitchRangeSemis = pct (smv, 0.95f) - pct (smv, 0.05f);

        // slope: least squares over voiced frames
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int c = 0;
        for (int i = 0; i < n; ++i)
            if (! std::isnan (sm[(size_t) i]))
            {
                const double x = i * hop, y = sm[(size_t) i];
                sx += x; sy += y; sxx += x * x; sxy += x * y; ++c;
            }
        const double den = c * sxx - sx * sx;
        out.pitchSlope = (c > 2 && den > 1e-9) ? (float) ((c * sxy - sx * sy) / den) : 0.0f;

        // bends: largest glide within 200 ms
        const int bw = std::max (1, (int) std::lround (0.2 / hop));
        float bend = 0.0f;
        for (int i = 0; i + bw < n; ++i)
            if (! std::isnan (sm[(size_t) i]) && ! std::isnan (sm[(size_t) (i + bw)]))
                bend = std::max (bend, std::abs (sm[(size_t) (i + bw)] - sm[(size_t) i]));
        out.bendSemis = bend;

        // jumps between neighbouring voiced frames (<= 30 ms apart)
        int jumps = 0, last = -1;
        for (int i = 0; i < n; ++i)
        {
            if (std::isnan (sm[(size_t) i])) continue;
            if (last >= 0 && i - last <= 3 && std::abs (sm[(size_t) i] - sm[(size_t) last]) > 2.5f) ++jumps;
            last = i;
        }
        out.jumpsPerSec = out.durationSec > 0 ? (float) jumps / out.durationSec : 0.0f;

        // sustain: longest stretch of voiced frames staying within +-0.75 st of its running mean
        int bestRun = 0, runStart = -1;
        double runSum = 0.0;
        for (int i = 0; i <= n; ++i)
        {
            const bool v = i < n && ! std::isnan (sm[(size_t) i]);
            if (v && runStart >= 0 && std::abs (sm[(size_t) i] - runSum / (i - runStart)) <= 0.75)
            {
                runSum += sm[(size_t) i];
                continue;
            }
            if (runStart >= 0) bestRun = std::max (bestRun, i - runStart);
            runStart = v ? i : -1;
            runSum = v ? sm[(size_t) i] : 0.0;
        }
        out.sustainSec = (float) (bestRun * hop);

        // vibrato: autocorrelation of the detrended contour on the longest voiced run
        auto [ra, rb] = longestVoicedRun (st);
        const int len = rb - ra;
        if (len * hop >= 0.35)
        {
            const int mw = std::max (2, (int) std::lround (0.1 / hop)); // 200 ms moving average (~one vibrato cycle)
            std::vector<double> res ((size_t) len);
            for (int i = 0; i < len; ++i)
            {
                double s = 0; int k = 0;
                for (int j = std::max (0, i - mw); j <= std::min (len - 1, i + mw); ++j) { s += st[(size_t) (ra + j)]; ++k; }
                res[(size_t) i] = st[(size_t) (ra + i)] - s / k;
            }
            double var = 0.0;
            for (auto r : res) var += r * r;
            var /= len;
            if (var > 1e-6)
            {
                const int lagLo = std::max (2, (int) std::floor (1.0 / (9.0 * hop)));
                const int lagHi = std::min (len / 2, (int) std::ceil (1.0 / (3.5 * hop)));
                double best = 0.0; int bestLag = 0;
                for (int lag = lagLo; lag <= lagHi; ++lag)
                {
                    double s = 0.0;
                    for (int i = 0; i + lag < len; ++i) s += res[(size_t) i] * res[(size_t) (i + lag)];
                    const double r = s / ((len - lag) * var);
                    if (r > best) { best = r; bestLag = lag; }
                }
                const double depth = std::sqrt (2.0 * var); // sinusoid peak deviation
                if (best > 0.45 && depth > 0.08)
                {
                    out.vibratoRateHz = (float) (1.0 / (bestLag * hop));
                    out.vibratoDepthSemis = (float) depth;
                }
            }
        }
    }

    frag.features = out;
    frag.detectedNote = out.medianF0Hz > 0.0f && out.voicedRatio >= 0.2f ? frequencyToMidiNote (out.medianF0Hz) : -1;
    frag.measured = true;
}

} // namespace ghostcut
