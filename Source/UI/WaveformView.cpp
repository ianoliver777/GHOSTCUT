#include "WaveformView.h"
#include "GhostLookAndFeel.h"

namespace ghostcut
{

static constexpr int laneHeight = 22;
static constexpr int rulerHeight = 16;
static constexpr int barHeight = 10;
static constexpr float edgeGrab = 6.0f;

WaveformView::WaveformView (Session& s, SamplerEngine& e) : session (s), engine (e)
{
    addAndMakeVisible (hbar);
    hbar.setAutoHide (false);
    hbar.addListener (this);
    session.addChangeListener (this);
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    session.removeChangeListener (this);
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    return getLocalBounds().withTrimmedTop (rulerHeight).withTrimmedBottom (laneHeight + barHeight + 2);
}

juce::Rectangle<int> WaveformView::laneArea() const
{
    auto r = getLocalBounds().withTrimmedBottom (barHeight + 2);
    return r.removeFromBottom (laneHeight);
}

void WaveformView::resized()
{
    hbar.setBounds (getLocalBounds().removeFromBottom (barHeight));
    clampView();
    updateScrollBar();
}

double WaveformView::xToSample (float x) const { return viewStart + (double) x * samplesPerPixel; }
float WaveformView::sampleToX (double s) const  { return (float) ((s - viewStart) / samplesPerPixel); }

void WaveformView::clampView()
{
    auto snap = session.getSnapshot();
    const int w = std::max (1, getWidth());
    if (snap == nullptr) { viewStart = 0; samplesPerPixel = 1; return; }
    const double total = (double) snap->numSamples();
    samplesPerPixel = juce::jlimit (0.05, std::max (0.05, total / w), samplesPerPixel);
    viewStart = juce::jlimit (0.0, std::max (0.0, total - samplesPerPixel * w), viewStart);
}

void WaveformView::updateScrollBar()
{
    auto snap = session.getSnapshot();
    if (snap == nullptr) { hbar.setRangeLimits (0, 1); hbar.setCurrentRange (0, 1); return; }
    hbar.setRangeLimits (0.0, (double) snap->numSamples(), juce::dontSendNotification);
    hbar.setCurrentRange (viewStart, samplesPerPixel * getWidth(), juce::dontSendNotification);
}

void WaveformView::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    viewStart = newStart;
    clampView();
    repaint();
}

void WaveformView::zoomBy (double factor, double anchorX)
{
    if (session.getSnapshot() == nullptr) return;
    if (anchorX < 0) anchorX = getWidth() * 0.5;
    const double anchorSample = xToSample ((float) anchorX);
    samplesPerPixel /= factor;
    clampView();
    viewStart = anchorSample - anchorX * samplesPerPixel;
    clampView();
    updateScrollBar();
    repaint();
}

void WaveformView::zoomToFit()
{
    auto snap = session.getSnapshot();
    if (snap == nullptr) return;
    samplesPerPixel = (double) snap->numSamples() / std::max (1, getWidth());
    viewStart = 0;
    clampView();
    updateScrollBar();
    repaint();
}

void WaveformView::zoomToSelection()
{
    if (! session.hasSelection()) return;
    const double len = (double) (session.getSelectionEnd() - session.getSelectionStart());
    samplesPerPixel = len * 1.1 / std::max (1, getWidth());
    viewStart = session.getSelectionStart() - len * 0.05;
    clampView();
    updateScrollBar();
    repaint();
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto snap = session.getSnapshot();
    if (snap.get() != shownSnapshot)
    {
        shownSnapshot = snap.get();
        cursor = 0;
        zoomToFit();
    }
    // keep the selected fragment in view
    const int sel = session.getSelectedIndex();
    if (snap != nullptr && sel >= 0 && sel < (int) session.getFragments().size() && drag == Drag::None)
    {
        const auto& f = session.getFragments()[(size_t) sel];
        const double visEnd = viewStart + samplesPerPixel * getWidth();
        if ((double) f.start < viewStart || (double) f.end > visEnd)
        {
            const double len = (double) f.length();
            if (len > samplesPerPixel * getWidth()) samplesPerPixel = len * 1.2 / std::max (1, getWidth());
            viewStart = (double) f.start - (samplesPerPixel * getWidth() - len) * 0.5;
            clampView();
            updateScrollBar();
        }
    }
    repaint();
}

void WaveformView::timerCallback()
{
    if (engine.isPreviewPlaying() || session.isBusy() || engine.getActiveVoiceCount() > 0) repaint();
}

int WaveformView::fragmentAtX (float x) const
{
    const double s = xToSample (x);
    const auto& frs = session.getFragments();
    for (int i = 0; i < (int) frs.size(); ++i)
        if (s >= (double) frs[(size_t) i].start && s < (double) frs[(size_t) i].end) return i;
    return -1;
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto snapPtr = session.getSnapshot();
    auto wave = waveArea();

    if (snapPtr == nullptr)
    {
        g.setColour (colours::line);
        g.drawRect (wave.reduced (12), 1);
        const auto missing = session.getMissingSource();
        g.setColour (missing != juce::File() ? colours::warn : colours::textDim);
        GhostLookAndFeel::drawTracked (g, missing != juce::File() ? "SOURCE MISSING: " + missing.getFileName().toUpperCase()
                                                                  : "DROP A WAV OR MP3 HERE",
                                       wave.toFloat(), GhostLookAndFeel::label (13.0f, true), 0.25f,
                                       juce::Justification::centred);
        return;
    }

    const auto& snap = *snapPtr;
    drawRuler (g, getLocalBounds().removeFromTop (rulerHeight), snap);

    // selected fragment tint
    const auto& frs = session.getFragments();
    const int sel = session.getSelectedIndex();
    if (sel >= 0 && sel < (int) frs.size())
    {
        const auto& f = frs[(size_t) sel];
        const float x0 = sampleToX ((double) f.start), x1 = sampleToX ((double) f.end);
        g.setColour (colours::fragmentSel.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight()));
    }

    drawWave (g, wave, snap);

    // selection
    if (session.hasSelection())
    {
        const float x0 = sampleToX ((double) session.getSelectionStart());
        const float x1 = sampleToX ((double) session.getSelectionEnd());
        g.setColour (colours::selection);
        g.fillRect (juce::Rectangle<float> (x0, (float) wave.getY(), x1 - x0, (float) wave.getHeight()));
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawVerticalLine ((int) x0, (float) wave.getY(), (float) wave.getBottom());
        g.drawVerticalLine ((int) x1, (float) wave.getY(), (float) wave.getBottom());
    }

    // fragment lane
    auto lane = laneArea();
    g.setColour (colours::bg);
    g.fillRect (lane);
    const uint64_t sounding = engine.getSoundingMask();
    for (int i = 0; i < (int) frs.size(); ++i)
    {
        const auto& f = frs[(size_t) i];
        float x0 = sampleToX ((double) f.start), x1 = sampleToX ((double) f.end);
        if (x1 < 0 || x0 > (float) getWidth()) continue;
        auto block = juce::Rectangle<float> (x0, (float) lane.getY() + 3, std::max (2.0f, x1 - x0 - 1), (float) lane.getHeight() - 6);
        const bool isSel = i == sel;
        const bool isOn = i < 64 && ((sounding >> i) & 1u);
        g.setColour (isSel ? colours::fragmentSel : isOn ? colours::text : colours::fragment);
        g.fillRect (block);
        if (block.getWidth() > 18)
        {
            g.setColour (isSel ? colours::bg : colours::text.withAlpha (0.8f));
            g.setFont (GhostLookAndFeel::mono (10.0f));
            g.drawText (juce::String (i + 1), block.reduced (3, 0), juce::Justification::centredLeft, false);
        }
        if (isSel)
        {
            g.setColour (colours::accent);
            g.fillRect (block.withWidth (2.0f));
            g.fillRect (block.withLeft (block.getRight() - 2.0f));
        }
    }

    // cursor + playhead
    g.setColour (colours::textDim);
    g.drawVerticalLine ((int) sampleToX ((double) cursor), (float) wave.getY(), (float) wave.getBottom());
    if (engine.isPreviewPlaying())
    {
        g.setColour (colours::accent);
        const float px = sampleToX ((double) engine.getPreviewPosition());
        g.fillRect (px - 0.5f, (float) wave.getY(), 1.5f, (float) wave.getHeight());
    }
}

void WaveformView::drawRuler (juce::Graphics& g, juce::Rectangle<int> area, const SourceSnapshot& snap)
{
    g.setColour (colours::bg);
    g.fillRect (area);
    const double secPerPx = samplesPerPixel / snap.sampleRate;
    const double targetPx = 90.0;
    const double steps[] = { 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    double step = steps[std::size (steps) - 1];
    for (double s : steps) if (s / secPerPx >= targetPx) { step = s; break; }

    const double t0 = viewStart / snap.sampleRate;
    const double t1 = t0 + getWidth() * secPerPx;
    g.setFont (GhostLookAndFeel::mono (9.5f));
    for (double t = std::floor (t0 / step) * step; t <= t1; t += step)
    {
        const float x = sampleToX (t * snap.sampleRate);
        g.setColour (colours::line);
        g.drawVerticalLine ((int) x, (float) area.getBottom() - 4, (float) area.getBottom());
        g.setColour (colours::textFaint);
        const int mins = (int) (t / 60.0);
        const double secs = t - mins * 60.0;
        const auto label = juce::String (mins) + ":" + (secs < 10 ? "0" : "") + juce::String (secs, step < 1 ? 2 : 0);
        g.drawText (label, (int) x + 3, area.getY(), 70, area.getHeight(), juce::Justification::centredLeft, false);
    }
}

void WaveformView::drawWave (juce::Graphics& g, juce::Rectangle<int> area, const SourceSnapshot& snap)
{
    const float mid = (float) area.getCentreY();
    const float half = (float) area.getHeight() * 0.46f;
    g.setColour (colours::line);
    g.drawHorizontalLine ((int) mid, 0.0f, (float) getWidth());

    const int w = getWidth();
    const auto& peaks = snap.peaks;
    const int64_t total = snap.numSamples();
    g.setColour (colours::wave);

    // choose the coarsest pyramid level that still has >= 1 bin per pixel
    int level = -1;
    for (int l = 0; l < (int) peaks.levels.size(); ++l)
        if ((double) peaks.samplesPerBin (l) <= samplesPerPixel) level = l;

    for (int x = 0; x < w; ++x)
    {
        const double s0 = xToSample ((float) x), s1 = xToSample ((float) (x + 1));
        if (s0 >= (double) total) break;
        float lo = 0.0f, hi = 0.0f;

        if (level >= 0)
        {
            const int spb = peaks.samplesPerBin (level);
            const auto& bins = peaks.levels[(size_t) level];
            const int b0 = (int) (s0 / spb), b1 = std::max (b0 + 1, (int) std::ceil (s1 / spb));
            lo = 1.0f; hi = -1.0f;
            for (int b = b0; b < b1 && b < (int) bins.size(); ++b) { lo = std::min (lo, bins[(size_t) b].min); hi = std::max (hi, bins[(size_t) b].max); }
            if (lo > hi) lo = hi = 0.0f;
        }
        else
        {
            // zoomed in past the pyramid: read samples directly
            const int64_t a = (int64_t) s0, b = std::min<int64_t> (total, std::max<int64_t> (a + 1, (int64_t) std::ceil (s1)));
            lo = 1.0f; hi = -1.0f;
            for (int ch = 0; ch < snap.audio.getNumChannels(); ++ch)
            {
                const float* d = snap.audio.getReadPointer (ch);
                for (int64_t i = a; i < b; ++i) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
            }
            if (lo > hi) lo = hi = 0.0f;
        }
        const float y0 = mid - hi * half, y1 = mid - lo * half;
        g.fillRect ((float) x, y0, 1.0f, std::max (1.0f, y1 - y0));
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (! laneArea().contains (e.getPosition())) return;
    const int sel = session.getSelectedIndex();
    if (sel < 0 || sel >= (int) session.getFragments().size()) return;
    const auto& f = session.getFragments()[(size_t) sel];
    if (std::abs (e.position.x - sampleToX ((double) f.start)) < edgeGrab
        || std::abs (e.position.x - sampleToX ((double) f.end)) < edgeGrab)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (session.getSnapshot() == nullptr) return;
    dragMoved = false;
    drag = Drag::None;

    if (laneArea().contains (e.getPosition()))
    {
        const int sel = session.getSelectedIndex();
        if (sel >= 0 && sel < (int) session.getFragments().size())
        {
            const auto& f = session.getFragments()[(size_t) sel];
            if (std::abs (e.position.x - sampleToX ((double) f.start)) < edgeGrab) { drag = Drag::FragStart; dragFragment = sel; return; }
            if (std::abs (e.position.x - sampleToX ((double) f.end)) < edgeGrab)   { drag = Drag::FragEnd;   dragFragment = sel; return; }
        }
        const int hit = fragmentAtX (e.position.x);
        if (hit >= 0 && onFragmentClicked) onFragmentClicked (hit);
        return;
    }

    drag = Drag::Select;
    dragAnchor = (int64_t) xToSample (e.position.x);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    auto snap = session.getSnapshot();
    if (snap == nullptr) return;
    dragMoved = dragMoved || e.getDistanceFromDragStart() > 2;
    const int64_t s = juce::jlimit<int64_t> (0, snap->numSamples(), (int64_t) xToSample (e.position.x));

    if (drag == Drag::Select && dragMoved)
        session.setSelection (std::min (dragAnchor, s), std::max (dragAnchor, s));
    else if ((drag == Drag::FragStart || drag == Drag::FragEnd) && dragFragment < (int) session.getFragments().size())
    {
        const auto f = session.getFragments()[(size_t) dragFragment];
        if (drag == Drag::FragStart) session.setFragmentBounds (dragFragment, s, f.end);
        else                         session.setFragmentBounds (dragFragment, f.start, s);
        dragFragment = session.getSelectedIndex(); // index can change after re-sorting
    }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::Select && ! dragMoved)
    {
        cursor = juce::jmax<int64_t> (0, (int64_t) xToSample (e.position.x));
        session.setSelection (0, 0);
    }
    drag = Drag::None;
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (session.getSnapshot() == nullptr || laneArea().contains (e.getPosition())) return;
    cursor = (int64_t) xToSample (e.position.x);
    session.playFrom (cursor);
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (session.getSnapshot() == nullptr) return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (std::pow (2.0, (double) w.deltaY * 2.0), e.position.x);
        return;
    }
    const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    viewStart -= (double) delta * getWidth() * 0.5 * samplesPerPixel;
    clampView();
    updateScrollBar();
    repaint();
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoomBy ((double) scale, e.position.x);
}

} // namespace ghostcut
