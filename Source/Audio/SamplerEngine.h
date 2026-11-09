#pragma once

#include "../Core/AudioThreadExchange.h"
#include "../Core/Fragment.h"
#include "../Core/SourceSnapshot.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace ghostcut
{

/** Commands from the UI to the audio thread. Trivially copyable. */
struct EngineCommand
{
    enum class Type : uint8_t { PlaySource, StopPreview, AuditionFragment, AuditionRegion, StopAll };
    Type type = Type::StopAll;
    int index = -1;
    int64_t a = 0, b = 0; // positions in source samples
    bool loop = false;
};

/** Real-time playback engine. All public "audio thread" methods are lock-free and
    never allocate. Voices read directly from the immutable SourceSnapshot. */
class SamplerEngine
{
public:
    enum class KeyMode { Slices = 0, Chromatic = 1 };
    enum class TriggerMode { OneShot = 0, Gate = 1 };

    struct Params
    {
        KeyMode keyMode = KeyMode::Slices;
        TriggerMode trigger = TriggerMode::OneShot;
        int baseNote = 60;
        float attackMs = 2.0f, releaseMs = 120.0f;
        float velocitySens = 1.0f;
        float gainLinear = 1.0f;
    };

    static constexpr int maxVoices = 32;
    static constexpr float edgeFadeMs = 3.0f; // de-click fade at fragment edges

    SamplerEngine() = default;

    // ---- message thread ----
    void publishSource (SnapshotPtr s)                         { sources.publish (std::move (s)); }
    void publishFragments (std::shared_ptr<const FragmentTable> t) { tables.publish (std::move (t)); }
    void collectGarbage()                                      { sources.collectGarbage(); tables.collectGarbage(); }
    bool sendCommand (const EngineCommand& c) noexcept         { return commands.push (c); }
    void setSelectedFragment (int i) noexcept                  { selectedFragment.store (i); }
    void resetWhileAudioStopped()                              { sources.resetWhileAudioStopped(); tables.resetWhileAudioStopped(); }

    // ---- read from any thread (UI polling) ----
    int64_t getPreviewPosition() const noexcept { return previewPos.load (std::memory_order_relaxed); }
    bool isPreviewPlaying() const noexcept      { return previewPlaying.load (std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept    { return activeVoices.load (std::memory_order_relaxed); }
    /** bitmask of fragments sounding right now (first 64), for UI highlighting */
    uint64_t getSoundingMask() const noexcept   { return soundingMask.load (std::memory_order_relaxed); }

    // ---- audio thread ----
    void prepare (double hostSampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Params& p) noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        int fragmentIndex = -1;
        double pos = 0.0, inc = 1.0;
        int64_t start = 0, end = 0;
        float velGain = 1.0f;
        float env = 0.0f, envTarget = 1.0f, envStep = 0.0f;
        bool releasing = false;
        bool loop = false;
        uint64_t age = 0;
    };

    void handleCommand (const EngineCommand& c) noexcept;
    void noteOn (int note, float velocity, const Params& p) noexcept;
    void noteOff (int note, const Params& p) noexcept;
    void releaseAll (float ms) noexcept;
    void killAll() noexcept;
    Voice* allocateVoice() noexcept;
    void startVoice (Voice& v, int64_t s, int64_t e, double pitchRatio, float gain, float attackMs) noexcept;
    void beginRelease (Voice& v, float ms) noexcept;
    void render (juce::AudioBuffer<float>& out, int start, int num) noexcept;
    float edgeGain (const Voice& v) const noexcept;

    AudioThreadExchange<SourceSnapshot> sources;
    AudioThreadExchange<FragmentTable> tables;
    SpscQueue<EngineCommand, 64> commands;

    const SourceSnapshot* source = nullptr; // audio thread
    const FragmentTable* table = nullptr;   // audio thread

    std::array<Voice, maxVoices> voices {};
    Voice preview;                          // source transport / audition voice
    uint64_t ageCounter = 0;
    double hostRate = 44100.0;

    std::atomic<int> selectedFragment { -1 };
    std::atomic<int64_t> previewPos { 0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<int> activeVoices { 0 };
    std::atomic<uint64_t> soundingMask { 0 };
};

} // namespace ghostcut
