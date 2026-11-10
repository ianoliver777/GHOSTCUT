#pragma once

#include "PatternView.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghostcut
{

/** Shared little helpers for the generation pages. */
struct LabelledKnob
{
    juce::Slider slider;
    juce::String name;
    void setup (juce::Component& parent, const juce::String& label, double lo, double hi, double step, double value);
};

/** Drags a file produced on demand (loop WAV / MIDI) out to the DAW. */
class FileDragButton : public juce::Component
{
public:
    FileDragButton (juce::String text, std::function<juce::File (juce::String&)> makeFile);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    juce::String text;
    std::function<juce::File (juce::String&)> make;
    bool started = false;
};

class GeneratePanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    GeneratePanel (Session&, SamplerEngine&, juce::AudioProcessorValueTreeState&);
    ~GeneratePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncFromSession(); repaint(); }
    void timerCallback() override;
    void syncFromSession();
    void pushSettings();
    void exportWith (bool midi);

    Session& session;
    SamplerEngine& engine;
    juce::AudioProcessorValueTreeState& apvts;

    juce::OwnedArray<juce::TextButton> modeButtons;
    juce::ComboBox key, scale, lowNote, octaves, steps, rate, syncMode;
    LabelledKnob density, complexity, randomness, swing;
    juce::Slider tempo, patLevel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAtt, levelAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAtt;
    juce::TextEditor seed;
    juce::TextButton dice { "New seed" }, generateBtn { "Generate" }, rebuildBtn { "Rebuild (same seed)" },
                     playBtn { "Play" }, exportWav { "Export loop WAV" }, exportMid { "Export MIDI" };
    FileDragButton dragWav, dragMid;
    PatternView roll;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> modeArea, slotsArea, musicArea, outArea;
    bool syncing = false;
};

} // namespace ghostcut
