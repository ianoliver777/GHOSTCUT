#include "ChopRenderer.h"

#if JUCE_MSVC
 #pragma warning (push, 0)
#elif JUCE_CLANG
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif JUCE_GCC
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wfloat-conversion"
 #pragma GCC diagnostic ignored "-Wdouble-promotion"
 #pragma GCC diagnostic ignored "-Wswitch-enum"
 #pragma GCC diagnostic ignored "-Wcast-align"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wredundant-decls"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
 #pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../../ThirdParty/signalsmith-stretch/signalsmith-stretch.h"
#if JUCE_MSVC
 #pragma warning (pop)
#elif JUCE_CLANG
 #pragma clang diagnostic pop
#elif JUCE_GCC
 #pragma GCC diagnostic pop
#endif

#include <map>

namespace ghostcut
{

juce::AudioBuffer<float> ChopRenderer::render (const SourceSnapshot& src, const Request& r)
{
    const int nch = src.audio.getNumChannels();
    const int64_t len = r.end - r.start;
    const int64_t a = r.start + (int64_t) (juce::jlimit (0.0f, 1.0f, r.startFrac) * (float) len);
    const int64_t b = r.start + (int64_t) (juce::jlimit (0.0f, 1.0f, std::max (r.endFrac, r.startFrac)) * (float) len);
    const int inLen = (int) juce::jlimit<int64_t> (0, src.numSamples() - a, b - a);
    if (inLen < 32) return {};

    juce::AudioBuffer<float> in (nch, inLen);
    for (int c = 0; c < nch; ++c) in.copyFrom (c, 0, src.audio, c, (int) a, inLen);
    if (r.reverse) in.reverse (0, inLen);

    const int maxOut = (int) (maxOutputSeconds * src.sampleRate);
    int outLen = r.fixedOutputLength > 0 ? (int) std::min<int64_t> (maxOut, r.fixedOutputLength)
                                         : (int) std::min<double> (maxOut, std::lround (inLen * std::max (0.1f, r.stretch)));
    outLen = std::max (32, outLen);

    juce::AudioBuffer<float> out (nch, outLen);
    out.clear();
    const bool needsDsp = std::abs (r.semitones) > 0.001f || outLen != inLen || std::abs (r.formantSemis) > 0.001f;

    if (! needsDsp)
    {
        for (int c = 0; c < nch; ++c) out.copyFrom (c, 0, in, c, 0, inLen);
    }
    else
    {
        // Pad with silence so even very short chops are longer than the stretcher's
        // seek length, then cut the chop back out of the stretched result.
        const int pad = 8192;
        const double ratio = (double) outLen / inLen;
        const int padOut = (int) std::lround (pad * ratio);
        juce::AudioBuffer<float> pin (nch, inLen + 2 * pad);
        pin.clear();
        for (int c = 0; c < nch; ++c) pin.copyFrom (c, pad, in, c, 0, inLen);
        const int pOutLen = (int) std::lround ((inLen + 2 * pad) * ratio);
        juce::AudioBuffer<float> pout (nch, pOutLen);
        pout.clear();

        signalsmith::stretch::SignalsmithStretch<float> st;
        st.presetDefault (nch, (float) src.sampleRate);
        st.setTransposeSemitones (r.semitones);
        st.setFormantSemitones (r.formantSemis, r.preserveFormants);
        st.exact (pin.getArrayOfReadPointers(), pin.getNumSamples(), pout.getArrayOfWritePointers(), pOutLen);

        for (int c = 0; c < nch; ++c)
            out.copyFrom (c, 0, pout, c, juce::jlimit (0, std::max (0, pOutLen - outLen), padOut), std::min (outLen, pOutLen - padOut));
    }

    // fades (always at least 1 ms so any cut point is click-free)
    const int fi = std::min (outLen / 2, std::max (1, (int) (std::max (1.0f, r.fadeInMs) * 0.001 * src.sampleRate)));
    const int fo = std::min (outLen / 2, std::max (1, (int) (std::max (1.0f, r.fadeOutMs) * 0.001 * src.sampleRate)));
    for (int c = 0; c < nch; ++c)
    {
        out.applyGainRamp (c, 0, fi, 0.0f, 1.0f);
        out.applyGainRamp (c, outLen - fo, fo, 1.0f, 0.0f);
    }
    return out;
}

std::shared_ptr<RenderedPattern> renderPattern (const Pattern& p, const SourceSnapshot& src,
                                                const std::vector<std::pair<int64_t, int64_t>>& slotBounds,
                                                double bpm, const std::function<bool()>& keepGoing)
{
    auto rp = std::make_shared<RenderedPattern>();
    rp->pattern = p;
    rp->sampleRate = src.sampleRate;
    const double stepSec = 60.0 / std::max (20.0, bpm) / std::max (1, p.stepDiv);

    // identical requests share one clip (repeated chops are common)
    std::map<juce::String, int> cache;
    auto clipFor = [&] (const ChopRenderer::Request& rq) -> int
    {
        const juce::String key = juce::String (rq.start) + "|" + juce::String (rq.end) + "|" + juce::String (rq.startFrac, 4) + "|"
                               + juce::String (rq.endFrac, 4) + "|" + (rq.reverse ? "r" : "f") + "|" + juce::String (rq.semitones, 3) + "|"
                               + juce::String (rq.stretch, 4) + "|" + juce::String (rq.fixedOutputLength) + "|"
                               + juce::String (rq.formantSemis, 3) + "|" + juce::String (rq.fadeInMs, 1) + "|" + juce::String (rq.fadeOutMs, 1);
        if (auto it = cache.find (key); it != cache.end()) return it->second;
        auto buf = ChopRenderer::render (src, rq);
        if (buf.getNumSamples() == 0) return -1;
        rp->clips.push_back (std::move (buf));
        cache[key] = (int) rp->clips.size() - 1;
        return (int) rp->clips.size() - 1;
    };

    for (int i = 0; i < p.length; ++i)
    {
        if (keepGoing && ! keepGoing()) return nullptr;
        if (! p.active[(size_t) i]) continue;
        const auto& e = p.events[(size_t) i];
        if (e.slot < 0 || e.slot >= (int) slotBounds.size()) continue;
        const auto [s, en] = slotBounds[(size_t) e.slot];
        if (en <= s) continue;
        const int root = e.slot < (int) p.slotRoots.size() ? p.slotRoots[(size_t) e.slot] : 60;

        ChopRenderer::Request rq;
        rq.start = s; rq.end = en;
        rq.startFrac = e.startFrac; rq.endFrac = e.endFrac;
        rq.reverse = e.reverse;
        rq.semitones = (float) (e.note - root);
        rq.stretch = e.stretch;
        if (e.fitToGate) rq.fixedOutputLength = (int64_t) std::lround (e.gateSteps / std::max (1, e.ratchet) * stepSec * src.sampleRate);
        rq.formantSemis = e.formantSemis;
        rq.fadeInMs = e.fadeInMs;
        rq.fadeOutMs = e.fadeOutMs;

        const int main = clipFor (rq);
        if (main >= 0) rp->hits[(size_t) i].push_back ({ main, 1.0f });
        for (auto& l : e.layers)
        {
            auto lr = rq;
            lr.semitones = rq.semitones + (float) l.semitones + l.detuneCents / 100.0f;
            const int c = clipFor (lr);
            if (c >= 0) rp->hits[(size_t) i].push_back ({ c, l.gain });
        }
    }
    return rp;
}

} // namespace ghostcut
