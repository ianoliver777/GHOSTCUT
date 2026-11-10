#pragma once

#include "PatternView.h"
#include "GeneratePanel.h"

namespace ghostcut
{

/** SEQUENCE page: pattern bank A-H, slot-lane step editor and a step inspector. */
class SequencePanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    SequencePanel (Session&, SamplerEngine&);
    ~SequencePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncStep(); syncBank(); repaint(); }
    void timerCallback() override;
    void syncStep();
    void syncBank();
    template <typename Fn> void editStep (Fn&& fn);

    Session& session;
    SamplerEngine& engine;
    juce::OwnedArray<juce::TextButton> bankButtons;
    juce::ComboBox length;
    LabelledKnob swing;
    juce::TextButton playBtn { "Play" }, clearBtn { "Clear" }, randomBtn { "Randomize" };
    PatternView lanes;

    // step inspector
    juce::TextButton noteDown { "-1" }, noteUp { "+1" }, octDown { "-12" }, octUp { "+12" };
    juce::Slider velocity, gate, startFrac, endFrac;
    juce::ToggleButton reverse { "Reverse" }, choke { "Choke (cut by next hit)" };
    juce::ComboBox ratchet, layer;
    juce::TextButton deleteStep { "Delete step" };
    juce::Rectangle<int> inspectorArea;
    bool syncing = false;
};

} // namespace ghostcut
