#pragma once

#include "../Core/Session.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** Large central waveform: zoom, scroll, cursor, region selection, playhead,
    and a fragment lane whose blocks can be selected and resized. */
class WaveformView : public juce::Component,
                     private juce::ChangeListener,
                     private juce::ScrollBar::Listener,
                     private juce::Timer
{
public:
    WaveformView (Session&, SamplerEngine&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

    void zoomBy (double factor, double anchorX = -1.0);
    void zoomToFit();
    void zoomToSelection();
    int64_t getCursor() const noexcept { return cursor; }

    std::function<void (int)> onFragmentClicked; // index

private:
    enum class Drag { None, Select, FragStart, FragEnd };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;

    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> laneArea() const;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    void clampView();
    void updateScrollBar();
    int fragmentAtX (float x) const;
    void drawWave (juce::Graphics&, juce::Rectangle<int> area, const SourceSnapshot&);
    void drawRuler (juce::Graphics&, juce::Rectangle<int> area, const SourceSnapshot&);

    Session& session;
    SamplerEngine& engine;
    juce::ScrollBar hbar { false };

    const SourceSnapshot* shownSnapshot = nullptr;
    double viewStart = 0.0;        // samples
    double samplesPerPixel = 1.0;
    int64_t cursor = 0;

    Drag drag = Drag::None;
    int64_t dragAnchor = 0;
    int dragFragment = -1;
    bool dragMoved = false;
};

} // namespace ghostcut
