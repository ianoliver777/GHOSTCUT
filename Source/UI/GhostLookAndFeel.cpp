#include "GhostLookAndFeel.h"

namespace ghostcut
{

GhostLookAndFeel::GhostLookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, colours::bg);
    setColour (TextButton::buttonColourId, colours::panelHi);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::bg);
    setColour (Label::textColourId, colours::text);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, colours::panelHi);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::line);
    setColour (ComboBox::arrowColourId, colours::textDim);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::panelHi);
    setColour (PopupMenu::highlightedTextColourId, colours::accent);
    setColour (ListBox::backgroundColourId, colours::panel);
    setColour (ListBox::outlineColourId, colours::line);
    setColour (ScrollBar::thumbColourId, colours::textFaint);
    setColour (TooltipWindow::backgroundColourId, colours::panelHi);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::line);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
    setColour (ToggleButton::textColourId, colours::text);
    setColour (ToggleButton::tickColourId, colours::accent);
}

juce::Font GhostLookAndFeel::mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

juce::Font GhostLookAndFeel::label (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

void GhostLookAndFeel::drawTracked (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                                    juce::Font f, float tracking, juce::Justification j)
{
    f.setExtraKerningFactor (tracking);
    g.setFont (f);
    g.drawText (s, area, j, false);
}

void GhostLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? colours::accent : colours::panelHi;
    if (! b.isEnabled()) fill = colours::panel;
    else if (down) fill = fill.brighter (0.08f);
    else if (over) fill = fill.brighter (0.04f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (on ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
}

void GhostLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setColour (! b.isEnabled() ? colours::textFaint : on ? colours::bg : colours::text);
    drawTracked (g, b.getButtonText().toUpperCase(), b.getLocalBounds().toFloat(), label (11.0f, true), 0.12f,
                 juce::Justification::centred);
}

void GhostLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float d = std::min (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d);
    const float ang = a0 + pos * (a1 - a0);
    const auto c = r.getCentre();
    const float rad = d * 0.5f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, a0, a1, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, a0, ang, true);
    g.setColour (s.isEnabled() ? colours::accent : colours::textFaint);
    g.strokePath (val, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colours::panelHi);
    g.fillEllipse (r.reduced (6.0f));
    g.setColour (colours::text);
    const juce::Point<float> tip (c.x + (rad - 9) * std::sin (ang), c.y - (rad - 9) * std::cos (ang));
    g.drawLine ({ c, tip }, 1.5f);
}

void GhostLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (colours::line);
    g.fillRect ((float) x, cy - 1.0f, (float) w, 2.0f);
    g.setColour (s.isEnabled() ? colours::accent : colours::textFaint);
    g.fillRect ((float) x, cy - 1.0f, pos - (float) x, 2.0f);
    g.fillRect (pos - 2.0f, cy - 6.0f, 4.0f, 12.0f);
}

void GhostLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::panelHi);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (colours::textDim);
    g.fillPath (arrow);
}

void GhostLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool)
{
    g.setColour (colours::panel);
    g.fillRect (x, y, w, h);
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over ? colours::textDim : colours::textFaint);
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}

void GhostLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String&)
{
    g.setColour (colours::line);
    g.fillRect (0, h / 2 - 1, w, 2);
    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setColour (colours::accent);
        g.fillRect (0, h / 2 - 1, (int) (w * progress), 2);
    }
}

} // namespace ghostcut
