#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ghostcut;

GhostcutProcessor::GhostcutProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pGain     = apvts.getRawParameterValue (param::gain);
    pAttack   = apvts.getRawParameterValue (param::attack);
    pRelease  = apvts.getRawParameterValue (param::release);
    pKeyMode  = apvts.getRawParameterValue (param::keyMode);
    pTrigger  = apvts.getRawParameterValue (param::trigger);
    pBaseNote = apvts.getRawParameterValue (param::baseNote);
    pVelSens  = apvts.getRawParameterValue (param::velSens);
    pSeqMode  = apvts.getRawParameterValue (param::seqMode);
    pTempo    = apvts.getRawParameterValue (param::tempo);
    pPatLevel = apvts.getRawParameterValue (param::patLevel);

    fx = std::make_unique<fxp::Reader> (apvts);
    session.fxProvider = [this] { return fx->fx(); };
    session.onGenerated = [this] (GenMode m) { if (fx->followMode()) fxp::applyModePreset (apvts, m); };

    startTimerHz (10); // frees retired objects, forwards render-time FX settings
}

void GhostcutProcessor::timerCallback()
{
    engine.collectGarbage();
    session.setRenderOptions (fx->render()); // re-renders the pattern only when something changed
}

GhostcutProcessor::~GhostcutProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout GhostcutProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::gain, 1 }, "Output",
                                                  NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::attack, 1 }, "Attack",
                                                  NormalisableRange<float> (0.0f, 2000.0f, 0.1f, 0.3f), 2.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::release, 1 }, "Release",
                                                  NormalisableRange<float> (5.0f, 5000.0f, 0.1f, 0.3f), 120.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { param::keyMode, 1 }, "Key Mode",
                                                   StringArray { "Slices", "Chromatic" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { param::trigger, 1 }, "Trigger",
                                                   StringArray { "One-shot", "Gate" }, 0));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { param::baseNote, 1 }, "Base Note", 24, 96, 60,
                                                AudioParameterIntAttributes().withStringFromValueFunction (
                                                    [] (int v, int) { return ghostcut::noteName (v); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::velSens, 1 }, "Velocity",
                                                  NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { param::seqMode, 1 }, "Pattern Sync",
                                                   StringArray { "Off", "Host transport" }, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::tempo, 1 }, "Tempo (no host)",
                                                  NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f,
                                                  AudioParameterFloatAttributes().withLabel ("bpm")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { param::patLevel, 1 }, "Pattern Level",
                                                  NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    fxp::addParameters (l);
    return l;
}

void GhostcutProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    chain.prepare (sampleRate, std::max (64, samplesPerBlock));
}

void GhostcutProcessor::releaseResources() {}

bool GhostcutProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void GhostcutProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    SamplerEngine::Params p;
    p.gainLinear   = juce::Decibels::decibelsToGain (pGain->load());
    p.attackMs     = pAttack->load();
    p.releaseMs    = pRelease->load();
    p.keyMode      = pKeyMode->load() > 0.5f ? SamplerEngine::KeyMode::Chromatic : SamplerEngine::KeyMode::Slices;
    p.trigger      = pTrigger->load() > 0.5f ? SamplerEngine::TriggerMode::Gate : SamplerEngine::TriggerMode::OneShot;
    p.baseNote     = (int) pBaseNote->load();
    p.velocitySens = pVelSens->load();
    p.seqMode      = pSeqMode->load() > 0.5f ? SamplerEngine::SeqMode::HostSync : SamplerEngine::SeqMode::Off;
    p.fallbackBpm  = pTempo->load();
    p.patternGain  = juce::Decibels::decibelsToGain (pPatLevel->load(), -36.0f);
    p.keyPitchRatio = fx->pitchRatioForKeys();

    HostTransport host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.valid = true;
            host.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) { host.ppq = *ppq; host.hasPpq = true; }
            if (auto bpm = pos->getBpm()) host.bpm = *bpm;
        }

    engine.process (buffer, midi, p, host);
    chain.process (buffer, fx->fx(), engine.getLastBpm());
}

juce::AudioProcessorEditor* GhostcutProcessor::createEditor()
{
    return new GhostcutEditor (*this);
}

//==============================================================================
// State layout:
//   <GHOSTCUT version="1">
//     <PARAMS .../>      automatable parameters (APVTS)
//     <SESSION ...>      source reference, trim, fragments, selection, settings
//   </GHOSTCUT>
// Audio itself is NOT embedded: the project stores the absolute path, size and a
// content fingerprint of the source file. See README "Project portability".
void GhostcutProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GHOSTCUT");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (session.getStateTree(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void GhostcutProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("GHOSTCUT")) return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);

    const auto sess = root.getChildWithName (ghostcut::Session::stateType);
    if (sess.isValid()) session.setStateTreeFromAnyThread (sess);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GhostcutProcessor();
}
