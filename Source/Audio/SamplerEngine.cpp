#include "SamplerEngine.h"

namespace ghostcut
{

void SamplerEngine::prepare (double hostSampleRate, int)
{
    hostRate = hostSampleRate > 0 ? hostSampleRate : 44100.0;
    killAll();
    expectedPpq = -1.0e9;
}

void SamplerEngine::killAll() noexcept
{
    for (auto& v : voices) v.active = false;
    preview.active = false;
    previewPlaying.store (false);
}

//==============================================================================
void SamplerEngine::retireUnusedPatterns() noexcept
{
    for (auto& old : oldPatterns)
    {
        if (old == nullptr) continue;
        bool used = false;
        for (auto& v : voices) if (v.active && v.owner == old) { used = true; break; }
        if (! used) { patterns.retire (old); old = nullptr; }
    }
}

void SamplerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p,
                             const HostTransport& host) noexcept
{
    out.clear();

    bool srcChanged = false, tblChanged = false, patChanged = false;
    source = sources.acquire (srcChanged);
    table  = tables.acquire (tblChanged);
    if (srcChanged)
        for (auto& v : voices) if (v.owner == nullptr) v.active = false; // source voices point into old audio
    if (srcChanged) preview.active = false;

    const RenderedPattern* previous = nullptr;
    pattern = patterns.acquireKeepPrevious (patChanged, previous);
    if (previous != nullptr)
    {
        // let its voices ring out briefly; free it once none are left
        for (auto& v : voices) if (v.active && v.owner == previous && ! v.releasing) beginRelease (v, 20.0f);
        bool stored = false;
        for (auto& old : oldPatterns) if (old == nullptr) { old = previous; stored = true; break; }
        if (! stored)
        {
            for (auto& v : voices) if (v.owner == previous) v.active = false;
            patterns.retire (previous);
        }
    }
    retireUnusedPatterns();

    EngineCommand c;
    while (commands.pop (c)) handleCommand (c);

    // gather sample-accurate events: MIDI + sequencer steps
    const int n = out.getNumSamples();
    std::array<Trigger, 64> trig;
    const int numTrig = collectSequencerTriggers (n, p, host, trig.data(), (int) trig.size());
    const double bpm = lastBpm.load (std::memory_order_relaxed);

    auto midiIt = midi.cbegin();
    int ti = 0, cursor = 0;
    while (true)
    {
        const int nextMidi = midiIt != midi.cend() ? juce::jlimit (0, n, (*midiIt).samplePosition) : n + 1;
        const int nextTrig = ti < numTrig ? trig[(size_t) ti].offset : n + 1;
        const int next = std::min (nextMidi, nextTrig);
        if (next > n) break;
        if (next > cursor) { render (out, cursor, next - cursor); cursor = next; }

        if (nextTrig <= nextMidi)
        {
            const auto& t = trig[(size_t) ti++];
            triggerStep (t.step, t.subs, bpm, p);
        }
        else
        {
            const auto m = (*midiIt).getMessage();
            ++midiIt;
            if (m.isNoteOn())                               noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
            else if (m.isNoteOff())                         noteOff (m.getNoteNumber(), p);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) releaseAll (m.isAllSoundOff() ? 5.0f : p.releaseMs);
        }
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

//==============================================================================
int SamplerEngine::collectSequencerTriggers (int n, const Params& p, const HostTransport& host, Trigger* out, int maxOut) noexcept
{
    const double bpm = (host.valid && host.bpm > 1.0) ? host.bpm : p.fallbackBpm;
    lastBpm.store (bpm, std::memory_order_relaxed);

    const bool hostRunning = p.seqMode == SeqMode::HostSync && host.valid && host.playing && host.hasPpq;
    const bool internal = internalRun.load (std::memory_order_relaxed) && ! hostRunning;
    const bool running = (hostRunning || internal) && pattern != nullptr && pattern->pattern.length > 0;

    if (! running)
    {
        if (wasRunning)
            for (auto& v : voices) if (v.active && v.owner != nullptr && ! v.releasing) beginRelease (v, 30.0f);
        wasRunning = false;
        seqRunning.store (false, std::memory_order_relaxed);
        currentStep.store (-1, std::memory_order_relaxed);
        return 0;
    }

    const double beatsPerSample = bpm / 60.0 / hostRate;
    double ppq0;
    if (hostRunning) ppq0 = host.ppq;
    else
    {
        if (! wasRunning) internalPpq = 0.0;
        ppq0 = internalPpq;
        internalPpq += n * beatsPerSample;
    }
    const double ppq1 = ppq0 + n * beatsPerSample;

    // jumps (loop braces, relocation, first block): resync without double-firing
    if (! wasRunning || std::abs (ppq0 - expectedPpq) > 1.0e-3)
        lastFiredPpq = ppq0 - 1.0e-9;
    expectedPpq = ppq1;
    wasRunning = true;
    seqRunning.store (true, std::memory_order_relaxed);

    const auto& pat = pattern->pattern;
    const double stepBeats = 1.0 / std::max (1, pat.stepDiv);
    const double swingBeats = juce::jlimit (0.0, 0.75, (double) pat.swing) * stepBeats;
    int count = 0;
    const long long k0 = (long long) std::floor (ppq0 / stepBeats) - 1;
    const long long k1 = (long long) std::ceil (ppq1 / stepBeats);
    for (long long k = k0; k <= k1 && count < maxOut; ++k)
    {
        const int step = (int) (((k % pat.length) + pat.length) % pat.length);
        const double tStep = (double) k * stepBeats + ((k & 1) ? swingBeats : 0.0);
        const int subs = pat.active[(size_t) step] ? juce::jlimit (1, 4, pat.events[(size_t) step].ratchet) : 1;
        for (int j = 0; j < subs && count < maxOut; ++j)
        {
            const double t = tStep + j * stepBeats / subs;
            if (t < ppq0 || t >= ppq1 || t <= lastFiredPpq) continue;
            lastFiredPpq = t;
            out[count++] = { juce::jlimit (0, n - 1, (int) ((t - ppq0) / beatsPerSample)), step, j, subs };
        }
    }
    // keep the UI step indicator moving even on empty steps
    const long long kNow = (long long) std::floor (ppq0 / stepBeats);
    currentStep.store ((int) (((kNow % pat.length) + pat.length) % pat.length), std::memory_order_relaxed);
    std::sort (out, out + count, [] (const Trigger& a, const Trigger& b) { return a.offset < b.offset; });
    return count;
}

void SamplerEngine::triggerStep (int step, int subs, double bpm, const Params& p) noexcept
{
    if (pattern == nullptr || step < 0 || step >= pattern->pattern.length) return;
    if (! pattern->pattern.active[(size_t) step]) return;
    const auto& e = pattern->pattern.events[(size_t) step];
    const auto& hits = pattern->hits[(size_t) step];
    if (hits.empty()) return;

    if (e.choke)
        for (auto& v : voices) if (v.active && v.chokeable && ! v.releasing) beginRelease (v, 8.0f);

    const double stepSec = 60.0 / std::max (20.0, bpm) / std::max (1, pattern->pattern.stepDiv);
    const int64_t gateSamples = std::max<int64_t> (1, (int64_t) (e.gateSteps / subs * stepSec * hostRate));
    const float gain = e.velocity * e.velocity * p.patternGain;

    for (auto& h : hits)
    {
        if (h.clip < 0 || h.clip >= (int) pattern->clips.size()) continue;
        const auto& clip = pattern->clips[(size_t) h.clip];
        if (clip.getNumSamples() < 2) continue;
        auto* v = allocateVoice();
        if (v == nullptr) return;
        v->active = true;
        v->releasing = false;
        v->loop = false;
        v->edgeFades = false;                 // clips carry their own fades
        v->L = clip.getReadPointer (0);
        v->R = clip.getReadPointer (clip.getNumChannels() > 1 ? 1 : 0);
        v->start = 0;
        v->end = clip.getNumSamples();
        v->pos = 0.0;
        v->inc = pattern->sampleRate / hostRate;
        v->velGain = gain * h.gain;
        v->env = 1.0f; v->envTarget = 1.0f; v->envStep = 0.0f;
        v->owner = pattern;
        v->samplesToRelease = gateSamples;
        v->gateReleaseMs = std::max (5.0f, e.fadeOutMs);
        v->chokeable = e.choke;
        v->note = -1;
        v->fragmentIndex = -1;
        v->age = ++ageCounter;
    }
}

//==============================================================================
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
            startSourceVoice (preview, s, e, 1.0, 1.0f, edgeFadeMs);
            preview.loop = c.loop;
            break;
        }
        case EngineCommand::Type::AuditionFragment:
        {
            if (source == nullptr || table == nullptr) return;
            if (c.index < 0 || c.index >= (int) table->fragments.size()) return;
            const auto& f = table->fragments[(size_t) c.index];
            preview.fragmentIndex = c.index;
            startSourceVoice (preview, f.start, f.end, 1.0, 1.0f, edgeFadeMs);
            preview.loop = c.loop;
            break;
        }
        case EngineCommand::Type::StopPreview:
            if (preview.active) beginRelease (preview, 8.0f);
            break;
        case EngineCommand::Type::StopAll:
            releaseAll (8.0f);
            if (preview.active) beginRelease (preview, 8.0f);
            internalRun.store (false);
            break;
        case EngineCommand::Type::SeqStart: internalRun.store (true); break;
        case EngineCommand::Type::SeqStop:  internalRun.store (false); break;
    }
}

SamplerEngine::Voice* SamplerEngine::allocateVoice() noexcept
{
    for (auto& v : voices) if (! v.active) return &v;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.releasing && (oldest == nullptr || v.age < oldest->age)) oldest = &v;
    if (oldest == nullptr)
        for (auto& v : voices)
            if (oldest == nullptr || v.age < oldest->age) oldest = &v;
    return oldest;
}

void SamplerEngine::startSourceVoice (Voice& v, int64_t s, int64_t e, double pitchRatio, float gain, float attackMs) noexcept
{
    v.active = true;
    v.releasing = false;
    v.loop = false;
    v.edgeFades = true;
    v.L = source->audio.getReadPointer (0);
    v.R = source->audio.getReadPointer (source->audio.getNumChannels() > 1 ? 1 : 0);
    v.start = s;
    v.end = e;
    v.pos = (double) s;
    v.inc = source->sampleRate / hostRate * pitchRatio;
    v.velGain = gain;
    v.env = 0.0f;
    v.envTarget = 1.0f;
    v.envStep = 1.0f / std::max (1.0f, attackMs * 0.001f * (float) hostRate);
    v.owner = nullptr;
    v.samplesToRelease = -1;
    v.chokeable = false;
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
    if (p.keyMode == KeyMode::Slices) idx = note - p.baseNote;
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
    ratio *= p.keyPitchRatio;
    const float velGain = 1.0f - p.velocitySens + p.velocitySens * velocity * velocity;
    if (auto* v = allocateVoice())
    {
        startSourceVoice (*v, f.start, f.end, ratio, velGain, std::max (p.attackMs, edgeFadeMs));
        v->note = note;
        v->fragmentIndex = idx;
    }
}

void SamplerEngine::noteOff (int note, const Params& p) noexcept
{
    if (p.trigger != TriggerMode::Gate) return;
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.owner == nullptr && v.note == note)
            beginRelease (v, p.releaseMs);
}

void SamplerEngine::releaseAll (float ms) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.releasing) beginRelease (v, ms);
}

float SamplerEngine::edgeGain (const Voice& v) const noexcept
{
    if (! v.edgeFades) return 1.0f;
    const double fade = edgeFadeMs * 0.001 * (source != nullptr ? source->sampleRate : 44100.0);
    const double fromStart = v.pos - (double) v.start;
    const double toEnd = (double) v.end - v.pos;
    double g = 1.0;
    if (! v.loop && fromStart < fade) g = std::min (g, fromStart / fade);
    if (toEnd < fade) g = std::min (g, toEnd / fade);
    return (float) juce::jlimit (0.0, 1.0, g);
}

void SamplerEngine::renderVoice (Voice& v, float* oL, float* oR, int num) noexcept
{
    if (! v.active || v.L == nullptr) return;
    for (int i = 0; i < num; ++i)
    {
        if (v.samplesToRelease >= 0 && --v.samplesToRelease < 0 && ! v.releasing)
            beginRelease (v, v.gateReleaseMs);

        if (v.pos >= (double) v.end - 1.0)
        {
            if (v.loop && ! v.releasing) v.pos = (double) v.start + (v.pos - (double) (v.end - 1));
            else { v.active = false; return; }
        }

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
        const float sl = herm (v.L[i0], v.L[i1], v.L[i2], v.L[i3]);
        const float sr = herm (v.R[i0], v.R[i1], v.R[i2], v.R[i3]);

        if (v.env < v.envTarget)      v.env = std::min (v.envTarget, v.env + v.envStep);
        else if (v.env > v.envTarget) v.env = std::max (v.envTarget, v.env - v.envStep);
        if (v.releasing && v.env <= 0.0f) { v.active = false; return; }

        const float g = v.env * v.velGain * edgeGain (v);
        oL[i] += sl * g;
        if (oR != nullptr) oR[i] += sr * g;
        v.pos += v.inc;
    }
}

void SamplerEngine::render (juce::AudioBuffer<float>& out, int start, int num) noexcept
{
    if (num <= 0) return;
    float* oL = out.getWritePointer (0, start);
    float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;
    for (auto& v : voices) renderVoice (v, oL, oR, num);
    renderVoice (preview, oL, oR, num);
}

} // namespace ghostcut
