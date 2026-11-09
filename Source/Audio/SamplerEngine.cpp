#include "SamplerEngine.h"

namespace ghostcut
{

void SamplerEngine::prepare (double hostSampleRate, int)
{
    hostRate = hostSampleRate > 0 ? hostSampleRate : 44100.0;
    killAll();
}

void SamplerEngine::killAll() noexcept
{
    for (auto& v : voices) v.active = false;
    preview.active = false;
    previewPlaying.store (false);
}

void SamplerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p) noexcept
{
    out.clear();

    bool srcChanged = false, tblChanged = false;
    source = sources.acquire (srcChanged);
    table  = tables.acquire (tblChanged);
    if (srcChanged) killAll(); // voices point into the old audio; it may be released after this block

    EngineCommand c;
    while (commands.pop (c)) handleCommand (c);

    // sample-accurate MIDI: render up to each event, then apply it
    int cursor = 0;
    const int n = out.getNumSamples();
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, n, meta.samplePosition);
        if (pos > cursor) { render (out, cursor, pos - cursor); cursor = pos; }

        const auto m = meta.getMessage();
        if (m.isNoteOn())                         noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff())                   noteOff (m.getNoteNumber(), p);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) releaseAll (m.isAllSoundOff() ? 5.0f : p.releaseMs);
    }
    if (cursor < n) render (out, cursor, n - cursor);

    out.applyGain (p.gainLinear);

    int count = 0;
    uint64_t mask = 0;
    for (auto& v : voices)
        if (v.active)
        {
            ++count;
            if (v.fragmentIndex >= 0 && v.fragmentIndex < 64) mask |= (uint64_t) 1 << v.fragmentIndex;
        }
    activeVoices.store (count, std::memory_order_relaxed);
    soundingMask.store (mask, std::memory_order_relaxed);
    previewPlaying.store (preview.active, std::memory_order_relaxed);
    if (preview.active) previewPos.store ((int64_t) preview.pos, std::memory_order_relaxed);
}

void SamplerEngine::handleCommand (const EngineCommand& c) noexcept
{
    switch (c.type)
    {
        case EngineCommand::Type::PlaySource:
        case EngineCommand::Type::AuditionRegion:
        {
            if (source == nullptr) return;
            const int64_t len = source->numSamples();
            const int64_t s = juce::jlimit<int64_t> (0, len, c.a);
            const int64_t e = c.b > s ? juce::jlimit<int64_t> (s, len, c.b) : len;
            if (e - s < 2) return;
            preview.fragmentIndex = -1;
            startVoice (preview, s, e, 1.0, 1.0f, edgeFadeMs);
            preview.loop = c.loop;
            break;
        }
        case EngineCommand::Type::AuditionFragment:
        {
            if (source == nullptr || table == nullptr) return;
            if (c.index < 0 || c.index >= (int) table->fragments.size()) return;
            const auto& f = table->fragments[(size_t) c.index];
            preview.fragmentIndex = c.index;
            startVoice (preview, f.start, f.end, 1.0, 1.0f, edgeFadeMs);
            preview.loop = c.loop;
            break;
        }
        case EngineCommand::Type::StopPreview:
            if (preview.active) beginRelease (preview, 8.0f);
            break;
        case EngineCommand::Type::StopAll:
            releaseAll (8.0f);
            if (preview.active) beginRelease (preview, 8.0f);
            break;
    }
}

SamplerEngine::Voice* SamplerEngine::allocateVoice() noexcept
{
    for (auto& v : voices) if (! v.active) return &v;

    // steal: prefer the oldest releasing voice, otherwise the oldest voice
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.releasing && (oldest == nullptr || v.age < oldest->age)) oldest = &v;
    if (oldest == nullptr)
        for (auto& v : voices)
            if (oldest == nullptr || v.age < oldest->age) oldest = &v;
    return oldest;
}

void SamplerEngine::startVoice (Voice& v, int64_t s, int64_t e, double pitchRatio, float gain, float attackMs) noexcept
{
    v.active = true;
    v.releasing = false;
    v.loop = false;
    v.start = s;
    v.end = e;
    v.pos = (double) s;
    v.inc = (source != nullptr ? source->sampleRate / hostRate : 1.0) * pitchRatio;
    v.velGain = gain;
    v.env = 0.0f;
    v.envTarget = 1.0f;
    const float attackSamples = std::max (1.0f, attackMs * 0.001f * (float) hostRate);
    v.envStep = 1.0f / attackSamples;
    v.age = ++ageCounter;
}

void SamplerEngine::beginRelease (Voice& v, float ms) noexcept
{
    v.releasing = true;
    v.envTarget = 0.0f;
    const float samples = std::max (1.0f, ms * 0.001f * (float) hostRate);
    v.envStep = std::max (v.env, 1.0e-4f) / samples;
}

void SamplerEngine::noteOn (int note, float velocity, const Params& p) noexcept
{
    if (source == nullptr || table == nullptr || table->fragments.empty()) return;

    int idx = -1;
    double ratio = 1.0;
    if (p.keyMode == KeyMode::Slices)
    {
        idx = note - p.baseNote;
    }
    else
    {
        idx = selectedFragment.load (std::memory_order_relaxed);
        if (idx >= 0 && idx < (int) table->fragments.size())
        {
            const int root = table->fragments[(size_t) idx].root >= 0 ? table->fragments[(size_t) idx].root : p.baseNote;
            ratio = std::pow (2.0, (note - root) / 12.0);
        }
    }
    if (idx < 0 || idx >= (int) table->fragments.size()) return;

    const auto& f = table->fragments[(size_t) idx];
    const float velGain = 1.0f - p.velocitySens + p.velocitySens * velocity * velocity;

    if (auto* v = allocateVoice())
    {
        startVoice (*v, f.start, f.end, ratio, velGain, std::max (p.attackMs, edgeFadeMs));
        v->note = note;
        v->fragmentIndex = idx;
    }
}

void SamplerEngine::noteOff (int note, const Params& p) noexcept
{
    if (p.trigger != TriggerMode::Gate) return; // one-shot: play to the end
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.note == note)
            beginRelease (v, p.releaseMs);
}

void SamplerEngine::releaseAll (float ms) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.releasing) beginRelease (v, ms);
}

float SamplerEngine::edgeGain (const Voice& v) const noexcept
{
    // short fades at the fragment's own edges so any cut point is click-free
    const double fade = edgeFadeMs * 0.001 * (source != nullptr ? source->sampleRate : 44100.0);
    const double fromStart = v.pos - (double) v.start;
    const double toEnd = (double) v.end - v.pos;
    double g = 1.0;
    if (! v.loop && fromStart < fade) g = std::min (g, fromStart / fade);
    if (toEnd < fade) g = std::min (g, toEnd / fade);
    return (float) juce::jlimit (0.0, 1.0, g);
}

void SamplerEngine::render (juce::AudioBuffer<float>& out, int start, int num) noexcept
{
    if (source == nullptr || num <= 0) return;

    const int srcCh = source->audio.getNumChannels();
    const float* L = source->audio.getReadPointer (0);
    const float* R = source->audio.getReadPointer (srcCh > 1 ? 1 : 0);
    float* oL = out.getWritePointer (0, start);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    auto renderVoice = [&] (Voice& v)
    {
        if (! v.active) return;
        for (int i = 0; i < num; ++i)
        {
            if (v.pos >= (double) v.end - 1.0)
            {
                if (v.loop && ! v.releasing) v.pos = (double) v.start + (v.pos - (double) (v.end - 1));
                else { v.active = false; return; }
            }

            // 4-point Hermite interpolation
            const int64_t i1 = (int64_t) v.pos;
            const float t = (float) (v.pos - (double) i1);
            const int64_t i0 = std::max (v.start, i1 - 1);
            const int64_t i2 = std::min (v.end - 1, i1 + 1);
            const int64_t i3 = std::min (v.end - 1, i1 + 2);
            auto herm = [t] (float y0, float y1, float y2, float y3)
            {
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * t + c2) * t + c1) * t + y1;
            };
            const float sl = herm (L[i0], L[i1], L[i2], L[i3]);
            const float sr = herm (R[i0], R[i1], R[i2], R[i3]);

            // envelope
            if (v.env < v.envTarget)      v.env = std::min (v.envTarget, v.env + v.envStep);
            else if (v.env > v.envTarget) v.env = std::max (v.envTarget, v.env - v.envStep);
            if (v.releasing && v.env <= 0.0f) { v.active = false; return; }

            const float g = v.env * v.velGain * edgeGain (v);
            oL[i] += sl * g;
            if (oR != nullptr) oR[i] += sr * g;
            v.pos += v.inc;
        }
    };

    for (auto& v : voices) renderVoice (v);
    renderVoice (preview);
}

} // namespace ghostcut
