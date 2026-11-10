#pragma once

#include "Biquad.h"
#include "../Generation/Random.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace ghostcut
{

/** Plain-data snapshot of every effect parameter, read once per block. */
struct FxSettings
{
    // TEXTURE
    bool texOn = false;
    float drive = 0.0f;        // 0..1 saturation
    float noiseDb = -80.0f;    // hiss + crackle level
    float tilt = 0.0f;         // -1 dark .. +1 bright
    float hpHz = 20.0f, lpHz = 20000.0f;
    float chorus = 0.0f;       // 0..1
    float wow = 0.0f, flutter = 0.0f; // 0..1 pitch instability
    // LO-FI
    bool lofiOn = false;
    float lofiRateHz = 44100.0f;
    float lofiBits = 16.0f;
    float lofiBandHz = 20000.0f;
    float lofiDist = 0.0f;
    float lofiMix = 1.0f;
    // DELAY
    bool dlyOn = false;
    int dlyDivision = 5;       // index into delayDivisions()
    float dlyFeedback = 0.35f;
    bool dlyPingPong = true;
    float dlyLowCut = 150.0f, dlyHighCut = 6000.0f;
    float dlyMix = 0.25f;
    // REVERB
    bool revOn = false;
    float revSize = 0.6f;      // 0..1
    float revDecay = 3.0f;     // seconds (RT60)
    float revPreDelayMs = 20.0f;
    float revDamping = 0.4f;   // 0..1
    float revWidth = 1.0f;     // 0..1
    float revMix = 0.3f;

    static juce::StringArray delayDivisionNames();
    static double delayDivisionBeats (int index);
};

/** Feedback-delay-network reverb: 8 lines, Householder mixing, per-line damping,
    slow modulation against metallic ringing, input diffusion and pre-delay. */
class FdnReverb
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const FxSettings&) noexcept;
private:
    static constexpr int N = 8;
    double sr = 44100;
    std::array<dsp::DelayLine, N> lines;
    std::array<float, N> damp {};      // shelf low-pass state per line
    std::array<dsp::DelayLine, 4> diffL, diffR;
    dsp::DelayLine preL, preR;
    std::array<float, N> lfoPhase {};
    dsp::Smooth mix, width;
};

/** Tempo-synced stereo delay with ping-pong and filters in the feedback path. */
class SyncDelay
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const FxSettings&, double bpm) noexcept;
private:
    double sr = 44100;
    dsp::DelayLine dl, dr;
    dsp::Biquad hpL, hpR, lpL, lpR;
    dsp::Glide time;
    dsp::Smooth mix, fb;
};

/** Saturation, tilt EQ, HP/LP, chorus, wow/flutter and noise. */
class Texture
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const FxSettings&) noexcept;
private:
    double sr = 44100;
    dsp::Biquad hp[2], lp[2], lowShelf[2], highShelf[2], hissTone;
    dsp::DelayLine mod[2];
    double wowPhase = 0, flutterPhase = 0, chorusPhase = 0;
    float wowDrift = 0, wowDriftTarget = 0;
    double driftClock = 0;
    Random32 rng { 0x6705 };
    dsp::Smooth drive, noise;
    FxSettings last;
};

/** Sample-rate + bit reduction, band limiting and subtle distortion. */
class LoFi
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const FxSettings&) noexcept;
private:
    double sr = 44100;
    double phase = 0;
    float held[2] {};
    dsp::Biquad band[2], hp[2];
    Random32 rng { 0x10f1 };
    FxSettings last;
    dsp::Smooth mix;
};

/** TEXTURE -> LO-FI -> DELAY -> REVERB. Real-time safe after prepare(). Sections
    fade in/out when toggled so switching never clicks. */
class EffectsChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const FxSettings&, double bpm) noexcept;
    double tailSeconds (const FxSettings& s) const;

private:
    double sr = 44100;
    Texture texture;
    LoFi lofi;
    SyncDelay delay;
    FdnReverb reverb;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 4> enable; // per-section wet gate, exact 0/1
    juce::AudioBuffer<float> scratch;
};

} // namespace ghostcut
