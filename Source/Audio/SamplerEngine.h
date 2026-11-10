#pragma once

#include "../Core/AudioThreadExchange.h"
#include "../Core/Fragment.h"
#include "../Core/SourceSnapshot.h"
#include "../Generation/ChopRenderer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace ghostcut
{

/** Commands from the UI to the audio thread. Trivially copyable. */
struct EngineCommand
{
    enum class Type : uint8_t { PlaySource, StopPreview, AuditionFragment, AuditionRegion, StopAll, SeqStart, SeqStop };
    Type type = Type::StopAll;
    int index = -1;
    int64_t a = 0, b = 0; // positions in source samples
    bool loop = false;
};

/** Host transport as seen at the start of a block. */
struct HostTransport
{
    bool valid = false, playing = false, hasPpq = false;
    double ppq = 0.0, bpm = 0.0;
};

/**
    Real-time playback engine. All public audio-thread methods are lock-free and
    never allocate. Three kinds of voice read immutable buffers:
      - keyboard voices (MIDI notes)  -> SourceSnapshot regions
      - the preview voice (transport, auditions)
      - pattern voices (step sequencer) -> pre-rendered RenderedPattern clips
*/
class SamplerEngine
{
public:
    enum class KeyMode { Slices = 0, Chromatic = 1 };
    enum class TriggerMode { OneShot = 0, Gate = 1 };
    enum class SeqMode { Off = 0, HostSync = 1 };

    struct Params
    {
        KeyMode keyMode = KeyMode::Slices;
        TriggerMode trigger = TriggerMode::OneShot;
        int baseNote = 60;
        float attackMs = 2.0f, releaseMs = 120.0f;
        float velocitySens = 1.0f;
        float gainLinear = 1.0f;
        SeqMode seqMode = SeqMode::HostSync;
        double fallbackBpm = 120.0;   // when the host gives no tempo
        float patternGain = 1.0f;
    };

    static constexpr int maxVoices = 48;
    static constexpr float edgeFadeMs = 3.0f; // de-click fade at fragment edges

    SamplerEngine() = default;

    // ---- message thread ----
    void publishSource (SnapshotPtr s)                         { sources.publish (std::move (s)); }
    void publishFragments (std::shared_ptr<const FragmentTable> t) { tables.publish (std::move (t)); }
    void publishPattern (std::shared_ptr<const RenderedPattern> p) { patterns.publish (std::move (p)); }
    void collectGarbage()                                      { sources.collectGarbage(); tables.collectGarbage(); patterns.collectGarbage(); }
    bool sendCommand (const EngineCommand& c) noexcept         { return commands.push (c); }
    void setSelectedFragment (int i) noexcept                  { selectedFragment.store (i); }
    void resetWhileAudioStopped()                              { sources.resetWhileAudioStopped(); tables.resetWhileAudioStopped(); patterns.resetWhileAudioStopped(); }

    // ---- read from any thread (UI polling) ----
    int64_t getPreviewPosition() const noexcept { return previewPos.load (std::memory_order_relaxed); }
    bool isPreviewPlaying() const noexcept      { return previewPlaying.load (std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept    { return activeVoices.load (std::memory_order_relaxed); }
    uint64_t getSoundingMask() const noexcept   { return soundingMask.load (std::memory_order_relaxed); }
    int getCurrentStep() const noexcept         { return currentStep.load (std::memory_order_relaxed); }
    bool isSequencerRunning() const noexcept    { return seqRunning.load (std::memory_order_relaxed); }
    bool isInternalRunRequested() const noexcept { return internalRun.load (std::memory_order_relaxed); }
    double getLastBpm() const noexcept          { return lastBpm.load (std::memory_order_relaxed); }

    // ---- audio thread ----
    void prepare (double hostSampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p,
                  const HostTransport& host = {}) noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        int fragmentIndex = -1;
        const float* L = nullptr;
        const float* R = nullptr;
        double pos = 0.0, inc = 1.0;
        int64_t start = 0, end = 0;
        float velGain = 1.0f;
        float env = 0.0f, envTarget = 1.0f, envStep = 0.0f;
        bool releasing = false;
        bool loop = false;
        bool edgeFades = true;
        uint64_t age = 0;
        // pattern voices
        const RenderedPattern* owner = nullptr;
        int64_t samplesToRelease = -1;
        float gateReleaseMs = 10.0f;
        bool chokeable = false;
    };

    struct Trigger { int offset; int step; int sub; int subs; };

    void handleCommand (const EngineCommand& c) noexcept;
    void noteOn (int note, float velocity, const Params& p) noexcept;
    void noteOff (int note, const Params& p) noexcept;
    void releaseAll (float ms) noexcept;
    void killAll() noexcept;
    Voice* allocateVoice() noexcept;
    void startSourceVoice (Voice& v, int64_t s, int64_t e, double pitchRatio, float gain, float attackMs) noexcept;
    void beginRelease (Voice& v, float ms) noexcept;
    void triggerStep (int step, int subs, double bpm, const Params& p) noexcept;
    int collectSequencerTriggers (int numSamples, const Params& p, const HostTransport& host, Trigger* out, int maxOut) noexcept;
    void render (juce::AudioBuffer<float>& out, int start, int num) noexcept;
    void renderVoice (Voice& v, float* oL, float* oR, int num) noexcept;
    float edgeGain (const Voice& v) const noexcept;
    void retireUnusedPatterns() noexcept;

    AudioThreadExchange<SourceSnapshot> sources;
    AudioThreadExchange<FragmentTable> tables;
    AudioThreadExchange<RenderedPattern> patterns;
    SpscQueue<EngineCommand, 64> commands;

    const SourceSnapshot* source = nullptr;   // audio thread
    const FragmentTable* table = nullptr;     // audio thread
    const RenderedPattern* pattern = nullptr; // audio thread
    std::array<const RenderedPattern*, 8> oldPatterns {}; // waiting for their voices to finish

    std::array<Voice, maxVoices> voices {};
    Voice preview;
    uint64_t ageCounter = 0;
    double hostRate = 44100.0;

    // sequencer clock (audio thread)
    bool wasRunning = false;
    double internalPpq = 0.0;
    double lastFiredPpq = -1.0e9;
    double expectedPpq = -1.0e9;

    std::atomic<int> selectedFragment { -1 };
    std::atomic<int64_t> previewPos { 0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<int> activeVoices { 0 };
    std::atomic<uint64_t> soundingMask { 0 };
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> seqRunning { false }, internalRun { false };
    std::atomic<double> lastBpm { 120.0 };
};

} // namespace ghostcut
