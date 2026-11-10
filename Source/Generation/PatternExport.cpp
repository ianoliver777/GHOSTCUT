#include "PatternExport.h"

namespace ghostcut
{

double PatternExport::loopSeconds (const Pattern& p, double bpm)
{
    return p.length * (60.0 / std::max (20.0, bpm)) / std::max (1, p.stepDiv);
}

juce::AudioBuffer<float> PatternExport::renderLoop (const RenderedPattern& rp, double bpm, int loops, bool wrapTail)
{
    const auto& p = rp.pattern;
    const double sr = rp.sampleRate;
    const double stepSec = 60.0 / std::max (20.0, bpm) / std::max (1, p.stepDiv);
    const double swingSec = juce::jlimit (0.0, 0.75, (double) p.swing) * stepSec;
    const int loopLen = (int) std::lround (loopSeconds (p, bpm) * sr);
    const int total = loopLen * std::max (1, loops);
    const int maxTail = (int) (ChopRenderer::maxOutputSeconds * sr);

    juce::AudioBuffer<float> out (2, total + maxTail);
    out.clear();

    struct Placed { int clip; float gain; int64_t start; int64_t gateEnd; float releaseMs; bool chokeable; int64_t stop; };
    std::vector<Placed> placed;

    for (int lp = 0; lp < std::max (1, loops); ++lp)
        for (int k = 0; k < p.length; ++k)
        {
            if (! p.active[(size_t) k]) continue;
            const auto& e = p.events[(size_t) k];
            const int subs = juce::jlimit (1, 4, e.ratchet);
            for (int j = 0; j < subs; ++j)
            {
                const double t = lp * loopSeconds (p, bpm) + k * stepSec + ((k & 1) ? swingSec : 0.0) + j * stepSec / subs;
                const int64_t s0 = (int64_t) std::lround (t * sr);
                if (e.choke)
                    for (auto& pl : placed) if (pl.chokeable && pl.stop > s0) pl.stop = s0 + (int64_t) (0.008 * sr);
                const int64_t gate = s0 + (int64_t) (e.gateSteps / subs * stepSec * sr);
                for (auto& h : rp.hits[(size_t) k])
                    placed.push_back ({ h.clip, e.velocity * e.velocity * h.gain, s0, gate, std::max (5.0f, e.fadeOutMs), e.choke,
                                        std::numeric_limits<int64_t>::max() });
            }
        }

    for (auto& pl : placed)
    {
        if (pl.clip < 0 || pl.clip >= (int) rp.clips.size()) continue;
        const auto& clip = rp.clips[(size_t) pl.clip];
        const int64_t releaseAt = std::min (pl.gateEnd, pl.stop);
        const float relSamples = pl.stop < pl.gateEnd ? (float) (0.008 * sr) : pl.releaseMs * 0.001f * (float) sr;
        for (int64_t i = 0; i < clip.getNumSamples(); ++i)
        {
            const int64_t o = pl.start + i;
            if (o >= out.getNumSamples()) break;
            float env = 1.0f;
            if (o >= releaseAt)
            {
                env = 1.0f - (float) (o - releaseAt) / relSamples;
                if (env <= 0.0f) break;
            }
            for (int c = 0; c < 2; ++c)
                out.addSample (c, (int) o, clip.getSample (std::min (c, clip.getNumChannels() - 1), (int) i) * pl.gain * env);
        }
    }

    juce::AudioBuffer<float> result (2, total);
    for (int c = 0; c < 2; ++c) result.copyFrom (c, 0, out, c, 0, total);
    if (wrapTail)
        for (int c = 0; c < 2; ++c)
            for (int i = total; i < out.getNumSamples(); ++i)
                result.addSample (c, (i - total) % total, out.getSample (c, i));
    return result;
}

juce::MidiFile PatternExport::toMidi (const Pattern& p, double bpm)
{
    constexpr int ppq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / std::max (20.0, bpm))), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const double stepTicks = (double) ppq / std::max (1, p.stepDiv);
    for (int k = 0; k < p.length; ++k)
    {
        if (! p.active[(size_t) k]) continue;
        const auto& e = p.events[(size_t) k];
        const int subs = juce::jlimit (1, 4, e.ratchet);
        for (int j = 0; j < subs; ++j)
        {
            const double t0 = k * stepTicks + ((k & 1) ? p.swing * stepTicks : 0.0) + j * stepTicks / subs;
            const double len = std::max (stepTicks / 8.0, e.gateSteps * stepTicks / subs);
            auto add = [&] (int note, float vel)
            {
                note = juce::jlimit (0, 127, note);
                const auto v = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (vel * 127.0f));
                seq.addEvent (juce::MidiMessage::noteOn (1, note, v), t0);
                seq.addEvent (juce::MidiMessage::noteOff (1, note), t0 + len - 1);
            };
            add (e.note, e.velocity);
            for (auto& l : e.layers) add (e.note + l.semitones, e.velocity * l.gain);
        }
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), p.length * stepTicks);
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);
    return mf;
}

} // namespace ghostcut
