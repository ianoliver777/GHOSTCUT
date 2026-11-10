#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/SamplerEngine.h"
#include "Core/Session.h"

namespace ghostcut::param
{
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* keyMode   = "keyMode";
    inline constexpr const char* trigger   = "trigger";
    inline constexpr const char* baseNote  = "baseNote";
    inline constexpr const char* velSens   = "velSens";
    inline constexpr const char* seqMode   = "seqMode";
    inline constexpr const char* tempo     = "tempo";
    inline constexpr const char* patLevel  = "patLevel";
}

class GhostcutProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    GhostcutProcessor();
    ~GhostcutProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return apvts; }
    ghostcut::Session& getSession() noexcept { return session; }
    ghostcut::SamplerEngine& getEngine() noexcept { return engine; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override { engine.collectGarbage(); }

    ghostcut::SamplerEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    ghostcut::Session session { engine };

    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pKeyMode = nullptr;
    std::atomic<float>* pTrigger = nullptr;
    std::atomic<float>* pBaseNote = nullptr;
    std::atomic<float>* pVelSens = nullptr;
    std::atomic<float>* pSeqMode = nullptr;
    std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pPatLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostcutProcessor)
};
