#pragma once

#include "../Presets/PresetManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** Header preset strip:  [<]  CATEGORY / NAME  [>]  [SAVE]  */
class PresetBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit PresetBar (PresetManager&);
    ~PresetBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void showMenu();
    void showSaveDialog();
    juce::Rectangle<int> nameArea;
    PresetManager& presets;
    juce::TextButton prev { "<" }, next { ">" }, save { "Save" };
    std::unique_ptr<juce::AlertWindow> dialog;
};

} // namespace ghostcut
