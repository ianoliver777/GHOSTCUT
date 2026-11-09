#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** TRIPLE SEVEN AUDIO / GHOSTCUT palette: charcoal, muted greys, one pale accent. */
namespace colours
{
    inline const juce::Colour bg        { 0xff0f1012 };
    inline const juce::Colour panel     { 0xff17181b };
    inline const juce::Colour panelHi   { 0xff1e2024 };
    inline const juce::Colour line      { 0xff2a2d32 };
    inline const juce::Colour text      { 0xffd8d8d6 };
    inline const juce::Colour textDim   { 0xff80838a };
    inline const juce::Colour textFaint { 0xff4c4f55 };
    inline const juce::Colour wave      { 0xff9a9ea6 };
    inline const juce::Colour accent    { 0xffe8e4da }; // bone white
    inline const juce::Colour selection { 0x22e8e4da };
    inline const juce::Colour fragment  { 0xff5b6470 };
    inline const juce::Colour fragmentSel { 0xffc9c4b8 };
    inline const juce::Colour warn      { 0xffc8836a };
}

class GhostLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GhostLookAndFeel();

    static juce::Font mono (float h);
    static juce::Font label (float h, bool bold = false);
    /** Letter-spaced uppercase text, used for headings and the wordmark. */
    static void drawTracked (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                             juce::Font f, float tracking, juce::Justification j);

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return label (12.0f); }
    juce::Font getPopupMenuFont() override { return label (13.0f); }
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;
};

} // namespace ghostcut
