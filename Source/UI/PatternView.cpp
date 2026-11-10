#include "PatternView.h"
#include "GhostLookAndFeel.h"

namespace ghostcut
{

PatternView::PatternView (Session& s, SamplerEngine& e, Style st, bool ed)
    : session (s), engine (e), style (st), editable (ed)
{
    session.addChangeListener (this);
    startTimerHz (30);
}

PatternView::~PatternView() { session.removeChangeListener (this); }

juce::Colour PatternView::slotColour (int slot)
{
    // muted greys and bone tones: distinguishable without going neon
    static const juce::uint32 c[] = { 0xffe8e4da, 0xff9aa4b1, 0xffc9a98e, 0xff8fa898, 0xffb39bb8, 0xff7f8ea3, 0xffd2c38f, 0xffa38686 };
    return juce::Colour (c[((slot % 8) + 8) % 8]);
}

void PatternView::timerCallback()
{
    const int st = engine.isSequencerRunning() ? engine.getCurrentStep() : -1;
    if (st != lastStep) { lastStep = st; repaint(); }
}

juce::Rectangle<float> PatternView::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (style == Style::Lanes ? 140.0f : 36.0f).withTrimmedTop (16.0f).reduced (0, 2);
}

int PatternView::stepAt (float x) const
{
    const auto g = gridArea();
    const int len = session.getPattern().length;
    if (x < g.getX() || x >= g.getRight()) return -1;
    return juce::jlimit (0, len - 1, (int) ((x - g.getX()) / g.getWidth() * len));
}

int PatternView::laneAt (float y) const
{
    const auto g = gridArea();
    const int lanes = std::max (1, (int) session.getPattern().slotUids.size());
    if (y < g.getY() || y >= g.getBottom()) return -1;
    return juce::jlimit (0, lanes - 1, (int) ((y - g.getY()) / g.getHeight() * lanes));
}

void PatternView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto& p = session.getPattern();
    const int len = p.length;
    const auto grid = gridArea();
    const float cw = grid.getWidth() / len;
    const int playing = engine.isSequencerRunning() ? engine.getCurrentStep() : -1;

    // step columns + beat lines + numbers
    for (int i = 0; i < len; ++i)
    {
        const float x = grid.getX() + i * cw;
        if (i == playing) { g.setColour (colours::accent.withAlpha (0.10f)); g.fillRect (x, grid.getY(), cw, grid.getHeight()); }
        if (i == selectedStep && editable) { g.setColour (colours::accent.withAlpha (0.06f)); g.fillRect (x, grid.getY(), cw, grid.getHeight()); }
        const bool beat = i % std::max (1, p.stepDiv) == 0;
        g.setColour (beat ? colours::line.brighter (0.15f) : colours::line);
        g.drawVerticalLine ((int) x, grid.getY(), grid.getBottom());
        if (beat)
        {
            g.setColour (colours::textFaint);
            g.setFont (GhostLookAndFeel::mono (9.0f));
            g.drawText (juce::String (i / std::max (1, p.stepDiv) + 1), juce::Rectangle<float> (x + 3, 0, 30, 14), juce::Justification::centredLeft);
        }
    }

    if (! session.hasPattern())
    {
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (12.0f));
        g.drawText (style == Style::Lanes ? "Empty pattern slot. Generate one on the GENERATE page, or pin fragments and click cells to build one."
                                          : "No pattern yet. Pick a mode and press GENERATE.",
                    grid, juce::Justification::centred);
        return;
    }

    if (style == Style::Roll)
    {
        int lo = 127, hi = 0;
        for (int i = 0; i < len; ++i)
            if (p.active[(size_t) i])
            {
                lo = std::min (lo, p.events[(size_t) i].note);
                hi = std::max (hi, p.events[(size_t) i].note);
                for (auto& l : p.events[(size_t) i].layers) { lo = std::min (lo, p.events[(size_t) i].note + l.semitones); hi = std::max (hi, p.events[(size_t) i].note + l.semitones); }
            }
        lo -= 2; hi += 2;
        const float rh = grid.getHeight() / (float) std::max (1, hi - lo + 1);
        auto yFor = [&] (int note) { return grid.getBottom() - (note - lo + 1) * rh; };

        // note labels at C's
        g.setFont (GhostLookAndFeel::mono (9.0f));
        for (int n = lo; n <= hi; ++n)
            if (n % 12 == 0 || hi - lo < 14)
            {
                g.setColour (colours::textFaint);
                g.drawText (noteName (n), juce::Rectangle<float> (2, yFor (n), 32, rh), juce::Justification::centredLeft);
            }

        for (int i = 0; i < len; ++i)
        {
            if (! p.active[(size_t) i]) continue;
            const auto& e = p.events[(size_t) i];
            const float x = grid.getX() + i * cw + ((i & 1) ? p.swing * cw : 0.0f);
            const float w = std::max (3.0f, std::min (e.gateSteps, (float) (len - i)) * cw - 2.0f);
            auto draw = [&] (int note, float alpha)
            {
                auto r = juce::Rectangle<float> (x + 1, yFor (note) + 1, w, std::max (3.0f, rh - 2));
                g.setColour (slotColour (e.slot).withAlpha (alpha * (0.35f + 0.65f * e.velocity)));
                g.fillRect (r);
                if (e.reverse) { g.setColour (colours::bg); g.drawLine (r.getX(), r.getBottom(), r.getX() + std::min (10.0f, r.getWidth()), r.getY(), 1.2f); }
                for (int k = 1; k < e.ratchet; ++k)
                {
                    g.setColour (colours::bg);
                    g.drawVerticalLine ((int) (x + 1 + k * cw / e.ratchet), r.getY(), r.getBottom());
                }
            };
            for (auto& l : e.layers) draw (e.note + l.semitones, 0.45f);
            draw (e.note, 1.0f);
        }
        return;
    }

    // ---- lanes ----
    const int lanes = (int) p.slotUids.size();
    const float lh = grid.getHeight() / std::max (1, lanes);
    auto snap = session.getSnapshot();
    for (int l = 0; l < lanes; ++l)
    {
        const float y = grid.getY() + l * lh;
        g.setColour (colours::line);
        g.drawHorizontalLine ((int) y, grid.getX(), grid.getRight());
        const int fi = session.fragmentIndexForSlot (l);
        auto label = juce::Rectangle<float> (4, y, 132, lh);
        g.setColour (slotColour (l));
        g.fillRect (label.removeFromLeft (4).reduced (0, 4));
        label.removeFromLeft (6);
        g.setFont (GhostLookAndFeel::mono (10.5f));
        g.setColour (fi >= 0 ? colours::text : colours::warn);
        g.drawText (fi >= 0 ? "FRAG " + juce::String (fi + 1).paddedLeft ('0', 2) + "  " + noteName (p.slotRoots[(size_t) l]) : "MISSING", label, juce::Justification::centredLeft);
    }

    for (int i = 0; i < len; ++i)
    {
        if (! p.active[(size_t) i]) continue;
        const auto& e = p.events[(size_t) i];
        const float x = grid.getX() + i * cw;
        const float y = grid.getY() + e.slot * lh;
        auto r = juce::Rectangle<float> (x + 2, y + 3, cw - 4, lh - 6);
        auto vel = r.withTop (r.getBottom() - r.getHeight() * (0.25f + 0.75f * e.velocity));
        g.setColour (slotColour (e.slot).withAlpha (0.25f));
        g.fillRect (r);
        g.setColour (slotColour (e.slot));
        g.fillRect (vel);
        if (i == selectedStep) { g.setColour (colours::accent); g.drawRect (r.expanded (1.5f), 1.5f); }
        g.setColour (colours::bg);
        g.setFont (GhostLookAndFeel::mono (9.0f));
        juce::String tag = noteName (e.note);
        if (e.reverse) tag += " R";
        if (e.ratchet > 1) tag += " x" + juce::String (e.ratchet);
        if (cw > 34) g.drawFittedText (tag, r.reduced (2).toNearestInt(), juce::Justification::bottomLeft, 2);
    }
    juce::ignoreUnused (snap);
}

void PatternView::mouseDown (const juce::MouseEvent& e)
{
    if (! editable || style != Style::Lanes) return;
    const int step = stepAt (e.position.x);
    const int lane = laneAt (e.position.y);
    if (step < 0 || lane < 0) return;

    auto p = session.getPattern();
    dragVelStep = -1;
    if (p.active[(size_t) step] && p.events[(size_t) step].slot == lane)
    {
        if (selectedStep == step && ! e.mods.isPopupMenu())
        {
            p.active[(size_t) step] = false;          // second click on a selected hit removes it
            session.setPattern (p);
            setSelectedStep (-1);
            return;
        }
        setSelectedStep (step);
        dragVelStep = step;
        dragStartVel = p.events[(size_t) step].velocity;
        return;
    }

    // place this lane's fragment on the step (replacing whatever was there)
    StepEvent ev = p.active[(size_t) step] ? p.events[(size_t) step] : StepEvent {};
    ev.slot = lane;
    if (! p.active[(size_t) step])
    {
        ev.note = lane < (int) p.slotRoots.size() ? p.slotRoots[(size_t) lane] : 60;
        ev.gateSteps = 1.0f;
        ev.velocity = 0.8f;
    }
    p.events[(size_t) step] = ev;
    p.active[(size_t) step] = true;
    session.setPattern (p);
    setSelectedStep (step);
}

void PatternView::mouseDrag (const juce::MouseEvent& e)
{
    // vertical drag on a hit = velocity
    if (dragVelStep < 0) return;
    auto p = session.getPattern();
    if (! p.active[(size_t) dragVelStep]) return;
    const float v = juce::jlimit (0.05f, 1.0f, dragStartVel - e.getDistanceFromDragStartY() / 120.0f);
    if (std::abs (v - p.events[(size_t) dragVelStep].velocity) < 0.02f) return;
    p.events[(size_t) dragVelStep].velocity = v;
    session.setPattern (p);
}

} // namespace ghostcut
