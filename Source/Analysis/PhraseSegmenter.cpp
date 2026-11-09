#include "PhraseSegmenter.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace ghostcut
{

namespace
{
    std::vector<float> mixToMono (const SourceSnapshot& src)
    {
        const int n = (int) src.numSamples();
        std::vector<float> mono ((size_t) n, 0.0f);
        const int nch = src.audio.getNumChannels();
        for (int ch = 0; ch < nch; ++ch)
        {
            const float* d = src.audio.getReadPointer (ch);
            for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i];
        }
        if (nch > 1)
            for (auto& v : mono) v /= (float) nch;
        return mono;
    }

    /** Windowed-sinc low-pass then integer decimation. */
    std::vector<float> decimate (const std::vector<float>& x, int factor)
    {
        if (factor <= 1) return x;
        constexpr int taps = 63;
        const double fc = 0.45 / factor; // normalised cutoff (cycles/sample)
        std::array<float, taps> h {};
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const double m = i - (taps - 1) / 2.0;
            const double sinc = std::abs (m) < 1.0e-9 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
            const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (taps - 1))
                                  + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * i / (taps - 1));
            h[(size_t) i] = (float) (sinc * w);
            sum += h[(size_t) i];
        }
        for (auto& v : h) v = (float) (v / sum);

        const int n = (int) x.size();
        std::vector<float> y ((size_t) (n / factor));
        for (size_t o = 0; o < y.size(); ++o)
        {
            const int c = (int) o * factor;
            float acc = 0.0f;
            for (int k = 0; k < taps; ++k)
            {
                const int idx = c + k - (taps - 1) / 2;
                if (idx >= 0 && idx < n) acc += h[(size_t) k] * x[(size_t) idx];
            }
            y[o] = acc;
        }
        return y;
    }

    /** YIN (de Cheveigné & Kawahara 2002) on one frame. Returns f0 or 0. */
    float yin (const float* x, int w, double sr, float minHz, float maxHz,
               std::vector<float>& d, float& aperiodicity)
    {
        const int minLag = std::max (2, (int) (sr / maxHz));
        const int maxLag = std::min (w - 1, (int) (sr / minHz));
        aperiodicity = 1.0f;
        if (maxLag <= minLag + 2) return 0.0f;

        d.assign ((size_t) maxLag + 2, 0.0f);
        const int len = w - maxLag - 1;
        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            float s = 0.0f;
            for (int j = 0; j < len; ++j) { const float diff = x[j] - x[j + tau]; s += diff * diff; }
            d[(size_t) tau] = s;
        }
        // cumulative mean normalised difference
        d[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            running += d[(size_t) tau];
            d[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }

        constexpr float threshold = 0.15f;
        int best = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (d[(size_t) tau] < threshold)
            {
                while (tau + 1 <= maxLag && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // no dip under threshold: take the global minimum, report its aperiodicity
            best = minLag;
            for (int tau = minLag; tau <= maxLag; ++tau)
                if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        }
        aperiodicity = d[(size_t) best];

        // parabolic interpolation
        double refined = best;
        if (best > 1 && best < maxLag)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1e-12) refined = best + 0.5 * (a - c) / den;
        }
        return refined > 0.0 ? (float) (sr / refined) : 0.0f;
    }

    float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) juce::jlimit (0.0f, (float) (v.size() - 1), p * (float) (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    }
}

FrameFeatures PhraseSegmenter::computeFrames (const SourceSnapshot& src, const Progress& progress,
                                              const ShouldContinue& keepGoing)
{
    FrameFeatures ff;
    const double sr = src.sampleRate;
    const auto mono = mixToMono (src);
    const int n = (int) mono.size();
    const int hop = std::max (1, (int) std::lround (sr * 0.010));
    ff.hopSeconds = hop / sr;

    // FFT frame ~46 ms
    int order = 8;
    while ((1 << order) < (int) (sr * 0.046) && order < 14) ++order;
    const int fftSize = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));

    // pitch path at ~11 kHz
    const int decim = std::max (1, (int) std::floor (sr / 11025.0));
    const auto low = decimate (mono, decim);
    const double lowSr = sr / decim;
    const int yinWin = (int) std::lround (lowSr * 0.040); // 40 ms window (>= 2 periods of 70 Hz... ~2.8)
    std::vector<float> yinScratch;

    const int numFrames = n / hop + 1;
    ff.db.resize ((size_t) numFrames);
    ff.flux.resize ((size_t) numFrames);
    ff.centroid.resize ((size_t) numFrames);
    ff.f0.resize ((size_t) numFrames);
    ff.clarity.resize ((size_t) numFrames);

    std::vector<float> fftBuf ((size_t) fftSize * 2);
    std::vector<float> prevMag ((size_t) fftSize / 2 + 1, 0.0f);
    std::vector<float> frameLow ((size_t) yinWin, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        if ((f & 255) == 0)
        {
            if (keepGoing && ! keepGoing()) return {};
            if (progress) progress ((float) f / (float) numFrames);
        }

        const int centre = f * hop;
        const int s0 = centre - fftSize / 2;

        // energy over the FFT frame
        double e = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = s0 + i;
            const float v = (idx >= 0 && idx < n) ? mono[(size_t) idx] : 0.0f;
            fftBuf[(size_t) i] = v * window[(size_t) i];
            e += (double) v * v;
        }
        std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
        const double rms = std::sqrt (e / fftSize);
        ff.db[(size_t) f] = (float) juce::Decibels::gainToDecibels (rms, -100.0);

        fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
        double fluxSum = 0.0, magSum = 0.0, weighted = 0.0;
        for (int k = 0; k <= fftSize / 2; ++k)
        {
            const float m = fftBuf[(size_t) k];
            const float diff = m - prevMag[(size_t) k];
            if (diff > 0.0f) fluxSum += diff;
            magSum += m;
            weighted += m * (k * sr / fftSize);
            prevMag[(size_t) k] = m;
        }
        ff.flux[(size_t) f] = magSum > 1e-9 ? (float) (fluxSum / (magSum + 1e-9)) : 0.0f;
        ff.centroid[(size_t) f] = magSum > 1e-9 ? (float) (weighted / magSum) : 0.0f;

        // pitch
        const int lc = centre / decim - yinWin / 2;
        for (int i = 0; i < yinWin; ++i)
        {
            const int idx = lc + i;
            frameLow[(size_t) i] = (idx >= 0 && idx < (int) low.size()) ? low[(size_t) idx] : 0.0f;
        }
        float ap = 1.0f;
        float hz = 0.0f;
        if (ff.db[(size_t) f] > -60.0f)
            hz = yin (frameLow.data(), yinWin, lowSr, 70.0f, 1100.0f, yinScratch, ap);
        const bool voiced = hz > 0.0f && ap < 0.25f;
        ff.f0[(size_t) f] = voiced ? hz : 0.0f;
        ff.clarity[(size_t) f] = juce::jlimit (0.0f, 1.0f, 1.0f - ap);
    }

    if (progress) progress (1.0f);
    return ff;
}

void PhraseSegmenter::measure (Fragment& frag, const SourceSnapshot& src, const FrameFeatures& ff)
{
    FragmentFeatures out;
    const int nf = ff.numFrames();
    if (nf == 0 || frag.length() <= 0) { frag.features = out; frag.detectedNote = -1; return; }

    const double hopSamples = ff.hopSeconds * src.sampleRate;
    const int f0i = juce::jlimit (0, nf - 1, (int) std::floor (frag.start / hopSamples));
    const int f1i = juce::jlimit (f0i + 1, nf, (int) std::ceil (frag.end / hopSamples));

    std::vector<float> voicedHz;
    double dbSum = 0.0, cent = 0.0;
    float peak = -100.0f;
    int counted = 0;
    for (int f = f0i; f < f1i; ++f)
    {
        dbSum += ff.db[(size_t) f];
        peak = std::max (peak, ff.db[(size_t) f]);
        cent += ff.centroid[(size_t) f];
        ++counted;
        if (ff.f0[(size_t) f] > 0.0f) voicedHz.push_back (ff.f0[(size_t) f]);
    }
    out.meanDb = counted > 0 ? (float) (dbSum / counted) : -100.0f;
    out.peakDb = peak;
    out.centroidHz = counted > 0 ? (float) (cent / counted) : 0.0f;
    out.voicedRatio = counted > 0 ? (float) voicedHz.size() / (float) counted : 0.0f;

    if (voicedHz.size() >= 3)
    {
        out.medianF0Hz = percentile (voicedHz, 0.5f);
        double mean = 0.0, sq = 0.0;
        for (float hz : voicedHz)
        {
            const double st = 12.0 * std::log2 (hz / out.medianF0Hz);
            mean += st; sq += st * st;
        }
        mean /= (double) voicedHz.size();
        out.pitchStabilitySemis = (float) std::sqrt (std::max (0.0, sq / (double) voicedHz.size() - mean * mean));
    }

    frag.features = out;
    frag.detectedNote = out.medianF0Hz > 0.0f && out.voicedRatio >= 0.2f ? frequencyToMidiNote (out.medianF0Hz) : -1;
}

std::vector<Fragment> PhraseSegmenter::segment (const SourceSnapshot& src, const FrameFeatures& ff,
                                                const Settings& s)
{
    std::vector<Fragment> result;
    const int nf = ff.numFrames();
    if (nf == 0) return result;

    // smooth the level curve a little (50 ms moving average in dB)
    std::vector<float> lvl ((size_t) nf);
    const int halfWin = 2;
    for (int f = 0; f < nf; ++f)
    {
        double acc = 0.0; int c = 0;
        for (int k = -halfWin; k <= halfWin; ++k)
            if (f + k >= 0 && f + k < nf) { acc += ff.db[(size_t) (f + k)]; ++c; }
        lvl[(size_t) f] = (float) (acc / c);
    }

    std::vector<float> audible;
    for (float v : lvl) if (v > -95.0f) audible.push_back (v);
    if (audible.empty()) return result;

    const float floorDb = percentile (audible, 0.10f);
    const float topDb   = percentile (audible, 0.98f);
    // sensitivity 0 -> needs to be 22 dB above floor, 1 -> 8 dB above floor
    const float margin  = juce::jmap (juce::jlimit (0.0f, 1.0f, s.sensitivity), 22.0f, 8.0f);
    // with no real silence (continuous vocal, full mix) the floor estimate sits near
    // the top, so cap the gate below the loud parts: everything active, then split.
    const float onDb    = std::min (std::max ({ floorDb + margin, topDb - 40.0f, -60.0f }), topDb - 10.0f);
    const float offDb   = onDb - 6.0f;

    // hysteresis gate
    struct Span { int a, b; };
    std::vector<Span> spans;
    bool on = false; int startF = 0;
    for (int f = 0; f < nf; ++f)
    {
        if (! on && lvl[(size_t) f] >= onDb) { on = true; startF = f; }
        else if (on && lvl[(size_t) f] < offDb) { on = false; spans.push_back ({ startF, f }); }
    }
    if (on) spans.push_back ({ startF, nf });

    const double hop = ff.hopSeconds;
    auto msToFrames = [hop] (float ms) { return std::max (1, (int) std::lround (ms / 1000.0 / hop)); };

    // merge short gaps (breaths, consonant dips)
    std::vector<Span> merged;
    const int gapF = msToFrames (s.minGapMs);
    for (auto sp : spans)
    {
        if (! merged.empty() && sp.a - merged.back().b < gapF) merged.back().b = sp.b;
        else merged.push_back (sp);
    }

    // split long spans at their quietest point (middle half only)
    const int maxF = msToFrames (s.maxLengthMs);
    std::vector<Span> split;
    std::function<void (Span)> splitRec = [&] (Span sp)
    {
        if (sp.b - sp.a <= maxF) { split.push_back (sp); return; }
        const int lo = sp.a + (sp.b - sp.a) / 4, hi = sp.b - (sp.b - sp.a) / 4;
        int best = lo;
        for (int f = lo; f < hi; ++f)
        {
            // prefer quiet frames, then onset frames (a new syllable starting)
            const float score = lvl[(size_t) f] - 20.0f * ff.flux[(size_t) f];
            const float bestScore = lvl[(size_t) best] - 20.0f * ff.flux[(size_t) best];
            if (score < bestScore) best = f;
        }
        splitRec ({ sp.a, best });
        splitRec ({ best, sp.b });
    };
    for (auto sp : merged) splitRec (sp);

    const int minF = msToFrames (s.minLengthMs);
    const double hopSamples = hop * src.sampleRate;
    const int64_t pre  = (int64_t) (s.preRollMs / 1000.0 * src.sampleRate);
    const int64_t tail = (int64_t) (s.tailMs / 1000.0 * src.sampleRate);
    int64_t uid = 1;

    for (auto sp : split)
    {
        if (sp.b - sp.a < minF) continue;
        Fragment fr;
        fr.uid = uid++;
        fr.start = std::max<int64_t> (0, (int64_t) (sp.a * hopSamples) - pre);
        fr.end   = std::min<int64_t> (src.numSamples(), (int64_t) (sp.b * hopSamples) + tail);
        if (! result.empty() && fr.start < result.back().end) fr.start = result.back().end; // no overlap
        if (fr.length() <= 0) continue;
        measure (fr, src, ff);
        if (fr.features.voicedRatio < s.minVoicedRatio) continue;
        result.push_back (fr);
        if ((int) result.size() >= s.maxFragments) break;
    }
    return result;
}

} // namespace ghostcut
