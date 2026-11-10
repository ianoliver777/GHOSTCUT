#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Session.h"

namespace ghostcut
{

/** EFFECTS page: TEXTURE, LO-FI, DELAY, REVERB (live chain on the output) and
    PITCH / FORMANT + REVERSE (applied when pattern chops are rendered).
    Every control is an automatable parameter. */
class EffectsPanel : public juce::Component
{
public:
    EffectsPanel (Session&, juce::AudioProcessorValueTreeState&);
    ~EffectsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title, note;
        juce::Rectangle<int> area;
        std::unique_ptr<juce::ToggleButton> onButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
        struct Knob { std::unique_ptr<juce::Slider> s; juce::String label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
        std::vector<Knob> knobs;
        struct Combo { std::unique_ptr<juce::ComboBox> c; juce::String label; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att; };
        std::vector<Combo> combos;
        struct Toggle { std::unique_ptr<juce::ToggleButton> b; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
        std::vector<Toggle> toggles;
    };

    Section& addSection (const juce::String& title, const char* onParam, const juce::String& note = {});
    void addKnob (Section&, const char* param, const juce::String& label);
    void addCombo (Section&, const char* param, const juce::String& label);
    void addToggle (Section&, const char* param, const juce::String& label);

    Session& session;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<Section>> sections;
    juce::OwnedArray<juce::TextButton> modeFx;
    juce::ToggleButton follow { "FX follow the GENERATE mode" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAtt;
    juce::TextButton reversePattern { "Reverse step order" };
};

} // namespace ghostcut
