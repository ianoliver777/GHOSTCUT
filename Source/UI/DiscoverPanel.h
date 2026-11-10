#pragma once

#include "WaveformView.h"
#include "FragmentBrowser.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghostcut
{

/** Phase 1 DISCOVER page: import, waveform, transport, fragment list + inspector,
    and the instrument (MIDI playback) controls. */
class DiscoverPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    DiscoverPanel (Session&, SamplerEngine&, juce::AudioProcessorValueTreeState&);
    ~DiscoverPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void openImportDialog();

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::String name;
        std::unique_ptr<SliderAttachment> att;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();
    void setupKnob (Knob&, const juce::String& name, const char* paramId);

    Session& session;
    SamplerEngine& engine;
    juce::AudioProcessorValueTreeState& apvts;

    juce::TextButton importBtn { "Import" }, locateBtn { "Locate" }, analyzeBtn { "Analyze" }, clearBtn { "Clear" };
    juce::Slider sensitivity;
    juce::ComboBox granularity;
    juce::TextButton separateBtn { "Separate" }, viewOriginal { "Original" }, viewVocals { "Vocals" };
    juce::TextEditor searchBox;
    juce::ComboBox sortBox;
    void stepSelection (int delta);

    WaveformView waveform;
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" }, loopBtn { "Loop" };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, zoomFit { "Fit" }, zoomSel { "Sel" };
    juce::TextButton trimBtn { "Trim" }, addFragBtn { "+ Fragment" };

    FragmentList list;
    FragmentInspector inspector;

    juce::ComboBox keyMode, trigger;
    std::unique_ptr<ComboAttachment> keyModeAtt, triggerAtt;
    Knob baseNote, attack, release, velocity, gain;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> controlsArea, toolbarArea, transportArea, searchInfoArea;
};

} // namespace ghostcut
