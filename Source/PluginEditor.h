#pragma once

#include "PluginProcessor.h"
#include "UI/GhostLookAndFeel.h"
#include "UI/DiscoverPanel.h"

/** Simple honest panel for pages that are scheduled for a later phase. */
class PlannedPanel : public juce::Component
{
public:
    PlannedPanel (juce::String title, juce::String phase, juce::StringArray items)
        : heading (std::move (title)), phaseText (std::move (phase)), bullets (std::move (items)) {}
    void paint (juce::Graphics&) override;
private:
    juce::String heading, phaseText;
    juce::StringArray bullets;
};

class SettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit SettingsPanel (ghostcut::Session&);
    ~SettingsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ghostcut::Session& session;
    juce::ComboBox bitDepth;
    juce::TextButton openFolder { "Open drag folder" };
};

class GhostcutEditor final : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropContainer,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit GhostcutEditor (GhostcutProcessor&);
    ~GhostcutEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;
    void showTab (int index);

    GhostcutProcessor& processor;
    ghostcut::Session& session;
    ghostcut::GhostLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::OwnedArray<juce::TextButton> tabs;
    ghostcut::DiscoverPanel discover;
    PlannedPanel generate, sequence, effects;
    SettingsPanel settings;
    int currentTab = 0;

    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    bool dragHover = false;
    juce::Rectangle<int> headerArea, statusArea, brandArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostcutEditor)
};
