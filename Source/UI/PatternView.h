#pragma once

#include "../Core/Session.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** Draws a pattern either as a piano roll (pitch over time) or as slot lanes
    (one row per fragment). In lanes mode it is an editor: click a cell to place
    or remove that fragment on that step, click a hit to select it. */
class PatternView : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    enum class Style { Roll, Lanes };
    PatternView (Session&, SamplerEngine&, Style, bool editable);
    ~PatternView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    int getSelectedStep() const noexcept { return selectedStep; }
    void setSelectedStep (int s) { selectedStep = s; repaint(); if (onSelect) onSelect (s); }
    std::function<void (int)> onSelect;

    static juce::Colour slotColour (int slot);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::Rectangle<float> gridArea() const;
    int stepAt (float x) const;
    int laneAt (float y) const;

    Session& session;
    SamplerEngine& engine;
    Style style;
    bool editable;
    int selectedStep = -1;
    int lastStep = -2;
    int dragVelStep = -1;
    float dragStartVel = 0.0f;
};

} // namespace ghostcut
