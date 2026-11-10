#include "Effects.h"

namespace ghostcut
{

static constexpr float twoPi = 6.283185307179586f;

juce::StringArray FxSettings::delayDivisionNames()
{
    return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1 bar" };
}

double FxSettings::delayDivisionBeats (int i)
{
    static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, 11, i)];
}

//==============================================================================
// Reverb
//==============================================================================
static const float fdnMs[8] = { 31.1f, 37.3f, 41.9f, 47.3f, 53.7f, 59.9f, 67.1f, 73.9f };
static const float diffMsL[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
static const float diffMsR[4] = { 5.3f, 3.1f, 11.9f, 8.7f };

void FdnReverb::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& l : lines) l.prepare ((int) (0.2 * sr));
    for (auto& d : diffL) d.prepare ((int) (0.03 * sr));
    for (auto& d : diffR) d.prepare ((int) (0.03 * sr));
    preL.prepare ((int) (0.3 * sr));
    preR.prepare ((int) (0.3 * sr));
    mix.setTime (sr, 30); width.setTime (sr, 30);
    for (int i = 0; i < N; ++i) lfoPhase[(size_t) i] = (float) i / N;
    reset();
}

void FdnReverb::reset()
{
    for (auto& l : lines) l.clear();
    for (auto& d : diffL) d.clear();
    for (auto& d : diffR) d.clear();
    preL.clear(); preR.clear();
    damp.fill (0.0f);
}

void FdnReverb::process (float* L, float* R, int n, const FxSettings& s) noexcept
{
    const float scale = 0.35f + 1.65f * juce::jlimit (0.0f, 1.0f, s.revSize);
    std::array<float, N> delay {}, gain {};
    const float t60 = std::max (0.1f, s.revDecay);
    for (int i = 0; i < N; ++i)
    {
        delay[(size_t) i] = fdnMs[i] * scale * 0.001f * (float) sr;
        gain[(size_t) i] = std::pow (10.0f, -3.0f * delay[(size_t) i] / (t60 * (float) sr));
    }
    // Frequency-dependent decay: lows/mids decay at revDecay, highs faster with damping.
    // A shelf (lp + k * (x - lp)) leaves the low band at unity gain, so damping never
    // shortens the main decay time.
    const float t60hf = std::max (0.05f, t60 * (1.0f - 0.85f * juce::jlimit (0.0f, 1.0f, s.revDamping)));
    std::array<float, N> hfK {};
    for (int i = 0; i < N; ++i)
        hfK[(size_t) i] = std::pow (10.0f, -3.0f * delay[(size_t) i] / (t60hf * (float) sr)) / gain[(size_t) i];
    const float dampA = 1.0f - std::exp (-twoPi * 3500.0f / (float) sr);
    const float pre = std::max (1.0f, s.revPreDelayMs * 0.001f * (float) sr);
    const float modDepth = 0.0004f * (float) sr * scale;
    mix.target = juce::jlimit (0.0f, 1.0f, s.revMix);
    width.target = juce::jlimit (0.0f, 1.0f, s.revWidth);
    const float diffG = 0.55f;
    std::array<int, 4> dLen {}, dRen {};
    for (int a = 0; a < 4; ++a)
    {
        dLen[(size_t) a] = std::max (1, (int) (diffMsL[a] * scale * 0.001 * sr));
        dRen[(size_t) a] = std::max (1, (int) (diffMsR[a] * scale * 0.001 * sr));
    }

    for (int k = 0; k < n; ++k)
    {
        preL.push (L[k]); preR.push (R[k]);
        float inL = preL.read (pre), inR = preR.read (pre);

        // input diffusion (allpass chains)
        for (int a = 0; a < 4; ++a)
        {
            const float zl = diffL[(size_t) a].readInt (dLen[(size_t) a]), zr = diffR[(size_t) a].readInt (dRen[(size_t) a]);
            const float vl = inL + diffG * zl, vr = inR + diffG * zr;
            diffL[(size_t) a].push (vl); diffR[(size_t) a].push (vr);
            inL = zl - diffG * vl; inR = zr - diffG * vr;
        }

        std::array<float, N> o {};
        float sum = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            auto& ph = lfoPhase[(size_t) i];
            ph += (0.07f + 0.03f * (float) i) / (float) sr;
            if (ph >= 1.0f) ph -= 1.0f;
            float v = lines[(size_t) i].read (delay[(size_t) i] + modDepth * std::sin (twoPi * ph));
            damp[(size_t) i] += dampA * (v - damp[(size_t) i]);
            v = damp[(size_t) i] + hfK[(size_t) i] * (v - damp[(size_t) i]);
            o[(size_t) i] = v;
            sum += v;
        }
        sum *= 2.0f / N;
        for (int i = 0; i < N; ++i)
        {
            const float in = (i & 1) ? inR : inL;
            lines[(size_t) i].push ((o[(size_t) i] - sum) * gain[(size_t) i] + in * 0.35f);
        }

        float wl = 0.5f * (o[0] - o[2] + o[4] - o[6]);
        float wr = 0.5f * (o[1] - o[3] + o[5] - o[7]);
        const float w = width.next();
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
        wl = mid + side; wr = mid - side;

        const float m = mix.next();
        L[k] = L[k] * (1.0f - m) + wl * m;
        R[k] = R[k] * (1.0f - m) + wr * m;
    }
}

//==============================================================================
// Delay
//==============================================================================
void SyncDelay::prepare (double sampleRate, int)
{
    sr = sampleRate;
    dl.prepare ((int) (4.1 * sr));
    dr.prepare ((int) (4.1 * sr));
    time.setTime (sr, 120); mix.setTime (sr, 30); fb.setTime (sr, 30);
    time.reset (0.25f * (float) sr);
    reset();
}

void SyncDelay::reset()
{
    dl.clear(); dr.clear();
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
}

void SyncDelay::process (float* L, float* R, int n, const FxSettings& s, double bpm) noexcept
{
    const double seconds = std::min (4.0, FxSettings::delayDivisionBeats (s.dlyDivision) * 60.0 / std::max (20.0, bpm));
    time.target = (float) (seconds * sr);
    mix.target = juce::jlimit (0.0f, 1.0f, s.dlyMix);
    fb.target = juce::jlimit (0.0f, 0.95f, s.dlyFeedback);
    hpL.set (dsp::Biquad::Type::HighPass, sr, s.dlyLowCut); hpR.b0 = hpL.b0; hpR.b1 = hpL.b1; hpR.b2 = hpL.b2; hpR.a1 = hpL.a1; hpR.a2 = hpL.a2;
    lpL.set (dsp::Biquad::Type::LowPass, sr, s.dlyHighCut); lpR.b0 = lpL.b0; lpR.b1 = lpL.b1; lpR.b2 = lpL.b2; lpR.a1 = lpL.a1; lpR.a2 = lpL.a2;

    for (int k = 0; k < n; ++k)
    {
        const float t = time.next();
        float yl = lpL.process (hpL.process (dl.read (t)));
        float yr = lpR.process (hpR.process (dr.read (t)));
        const float f = fb.next();
        if (s.dlyPingPong)
        {
            dl.push (0.5f * (L[k] + R[k]) + f * yr);
            dr.push (f * yl);
        }
        else
        {
            dl.push (L[k] + f * yl);
            dr.push (R[k] + f * yr);
        }
        const float m = mix.next();
        L[k] = L[k] * (1.0f - 0.5f * m) + yl * m;
        R[k] = R[k] * (1.0f - 0.5f * m) + yr * m;
    }
}

//==============================================================================
// Texture
//==============================================================================
void Texture::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& m : mod) m.prepare ((int) (0.05 * sr));
    drive.setTime (sr, 30); noise.setTime (sr, 30, 0.1f);
    hissTone.set (dsp::Biquad::Type::HighShelf, sr, 3000, 0.7, 6.0);
    last.hpHz = -1; // force coefficient update
    reset();
}

void Texture::reset()
{
    for (auto& m : mod) m.clear();
    for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); lowShelf[c].reset(); highShelf[c].reset(); }
}

void Texture::process (float* L, float* R, int n, const FxSettings& s) noexcept
{
    if (! juce::exactlyEqual (s.hpHz, last.hpHz) || ! juce::exactlyEqual (s.lpHz, last.lpHz) || ! juce::exactlyEqual (s.tilt, last.tilt))
    {
        for (int c = 0; c < 2; ++c)
        {
            hp[c].set (dsp::Biquad::Type::HighPass, sr, s.hpHz);
            lp[c].set (dsp::Biquad::Type::LowPass, sr, s.lpHz);
            lowShelf[c].set (dsp::Biquad::Type::LowShelf, sr, 300.0, 0.7, -6.0 * s.tilt);
            highShelf[c].set (dsp::Biquad::Type::HighShelf, sr, 3000.0, 0.7, 6.0 * s.tilt);
        }
        last = s;
    }
    drive.target = juce::jlimit (0.0f, 1.0f, s.drive);
    noise.target = s.noiseDb <= -79.0f ? 0.0f : juce::Decibels::decibelsToGain (s.noiseDb);
    const float baseDelay = 0.006f * (float) sr;
    // Delay modulation A*sin(2 pi f t) bends pitch by 2 pi f A / sr. Depths are set from
    // the target deviation: wow up to +-1.2 % (~20 cents) at 0.8 Hz, flutter up to
    // +-0.15 % at 9.5 Hz.
    const float wowDepth = s.wow * 0.012f * (float) sr / (twoPi * 0.8f);
    const float flutterDepth = s.flutter * 0.0015f * (float) sr / (twoPi * 9.5f);
    const float chorusDepth = 0.003f * (float) sr;

    float* ch[2] = { L, R };
    for (int k = 0; k < n; ++k)
    {
        // wow: ~0.8 Hz sine whose rate drifts randomly (tape speed is never perfectly periodic)
        driftClock += 1.0 / sr;
        if (driftClock >= 1.3) { driftClock -= 1.3; wowDriftTarget = rng.rangef (-0.3f, 0.3f); }
        wowDrift += (wowDriftTarget - wowDrift) * (float) (1.0 / sr);
        wowPhase += (0.8 * (1.0 + wowDrift)) / sr;     if (wowPhase >= 1.0) wowPhase -= 1.0;
        flutterPhase += 9.5 / sr;                      if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        chorusPhase += 0.7 / sr;                       if (chorusPhase >= 1.0) chorusPhase -= 1.0;
        const float wobble = wowDepth * std::sin (twoPi * (float) wowPhase) + flutterDepth * std::sin (twoPi * (float) flutterPhase);
        const float d = drive.next();
        const float k_ = 1.0f + 9.0f * d;
        const float norm = 1.0f / std::tanh (k_);
        const float nz = noise.next(); // reaches exactly 0 when switched off

        for (int c = 0; c < 2; ++c)
        {
            float x = ch[c][k];
            x = hp[c].process (lp[c].process (x));
            x = highShelf[c].process (lowShelf[c].process (x));
            if (d > 0.0005f) x = x + d * (std::tanh (k_ * x) * norm - x);

            mod[c].push (x);
            float y = mod[c].read (baseDelay + wobble);
            if (s.chorus > 0.001f)
            {
                const float cph = twoPi * (float) chorusPhase + (c ? 1.5707963f : 0.0f);
                y = y * (1.0f - 0.35f * s.chorus) + 0.5f * s.chorus * mod[c].read (baseDelay * 2.0f + chorusDepth * (1.0f + std::sin (cph)));
            }
            if (nz > 0.0f)
            {
                float hiss = hissTone.process (rng.rangef (-1.0f, 1.0f)) * 0.3f;
                if (rng.chance (2.5 / sr)) hiss += rng.rangef (-1.0f, 1.0f) * 4.0f; // crackle
                y += hiss * nz;
            }
            ch[c][k] = y;
        }
    }
}

//==============================================================================
// Lo-fi
//==============================================================================
void LoFi::prepare (double sampleRate, int)
{
    sr = sampleRate;
    mix.setTime (sr, 30);
    last.lofiBandHz = -1;
    reset();
}

void LoFi::reset()
{
    phase = 0; held[0] = held[1] = 0;
    for (int c = 0; c < 2; ++c) { band[c].reset(); hp[c].reset(); }
}

void LoFi::process (float* L, float* R, int n, const FxSettings& s) noexcept
{
    if (! juce::exactlyEqual (s.lofiBandHz, last.lofiBandHz))
    {
        for (int c = 0; c < 2; ++c)
        {
            band[c].set (dsp::Biquad::Type::LowPass, sr, s.lofiBandHz, 0.8);
            hp[c].set (dsp::Biquad::Type::HighPass, sr, 60.0);
        }
        last.lofiBandHz = s.lofiBandHz;
    }
    mix.target = juce::jlimit (0.0f, 1.0f, s.lofiMix);
    const double step = std::min (1.0, (double) s.lofiRateHz / sr);
    const float q = std::pow (2.0f, juce::jlimit (2.0f, 16.0f, s.lofiBits) - 1.0f);
    float* ch[2] = { L, R };

    for (int k = 0; k < n; ++k)
    {
        phase += step;
        const bool take = phase >= 1.0;
        if (take) phase -= 1.0;
        const float m = mix.next();
        for (int c = 0; c < 2; ++c)
        {
            const float dry = ch[c][k];
            float x = hp[c].process (band[c].process (dry));
            if (take || step >= 1.0) held[c] = x;
            x = held[c];
            const float dither = (rng.uniformf() - rng.uniformf()) / q * 0.5f;
            x = std::round ((x + dither) * q) / q;
            if (s.lofiDist > 0.0f) x += s.lofiDist * (0.3f * (std::tanh (2.0f * x) - x) + 0.05f * x * x);
            ch[c][k] = dry + m * (x - dry);
        }
    }
}

//==============================================================================
// Chain
//==============================================================================
void EffectsChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    texture.prepare (sr, maxBlock);
    lofi.prepare (sr, maxBlock);
    delay.prepare (sr, maxBlock);
    reverb.prepare (sr, maxBlock);
    for (auto& e : enable) { e.reset (sr, 0.02); e.setCurrentAndTargetValue (0.0f); }
    scratch.setSize (2, std::max (1, maxBlock));
}

void EffectsChain::reset()
{
    texture.reset(); lofi.reset(); delay.reset(); reverb.reset();
}

double EffectsChain::tailSeconds (const FxSettings& s) const
{
    double t = 0.0;
    if (s.revOn) t = std::max (t, (double) s.revDecay * 1.2 + s.revPreDelayMs * 0.001);
    if (s.dlyOn && s.dlyFeedback > 0.01f)
        t = std::max (t, FxSettings::delayDivisionBeats (s.dlyDivision) * 0.5 * std::log (0.001) / std::log (s.dlyFeedback));
    return std::min (t, 30.0);
}

void EffectsChain::process (juce::AudioBuffer<float>& buffer, const FxSettings& s, double bpm) noexcept
{
    const int total = buffer.getNumSamples();
    const bool on[4] = { s.texOn, s.lofiOn, s.dlyOn, s.revOn };
    for (int i = 0; i < 4; ++i) enable[(size_t) i].setTargetValue (on[i] ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    for (int start = 0; start < total; start += scratch.getNumSamples())
    {
        const int n = std::min (scratch.getNumSamples(), total - start);
        for (int sec = 0; sec < 4; ++sec)
        {
            auto& en = enable[(size_t) sec];
            if (! en.isSmoothing() && en.getTargetValue() <= 0.0f) continue; // fully off: skip, no CPU

            float* wl = scratch.getWritePointer (0);
            float* wr = scratch.getWritePointer (1);
            std::copy (L + start, L + start + n, wl);
            std::copy (R + start, R + start + n, wr);
            switch (sec)
            {
                case 0: texture.process (wl, wr, n, s); break;
                case 1: lofi.process (wl, wr, n, s); break;
                case 2: delay.process (wl, wr, n, s, bpm); break;
                default: reverb.process (wl, wr, n, s); break;
            }
            for (int k = 0; k < n; ++k)
            {
                const float g = en.getNextValue();
                L[start + k] += g * (wl[k] - L[start + k]);
                if (R != L) R[start + k] += g * (wr[k] - R[start + k]);
            }
        }
    }
}

} // namespace ghostcut
