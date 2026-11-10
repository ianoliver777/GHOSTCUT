#include "FragmentBrowser.h"
#include "GhostLookAndFeel.h"
#include "../Analysis/FragmentRanker.h"

namespace ghostcut
{

static juce::String formatTime (double seconds)
{
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String (m) + ":" + (s < 10.0 ? "0" : "") + juce::String (s, 2);
}

void drawScoreBar (juce::Graphics& g, juce::Rectangle<float> r, float v, juce::Colour c)
{
    g.setColour (colours::line);
    g.fillRect (r);
    g.setColour (c);
    g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
}

static void drawMiniWave (juce::Graphics& g, juce::Rectangle<float> r, const SourceSnapshot& snap, int64_t s0, int64_t s1, juce::Colour c)
{
    const int w = (int) r.getWidth();
    if (w <= 0 || s1 <= s0) return;
    const double spp = (double) (s1 - s0) / w;
    const auto& peaks = snap.peaks;
    int level = -1;
    for (int l = 0; l < (int) peaks.levels.size(); ++l) if ((double) peaks.samplesPerBin (l) <= spp) level = l;
    g.setColour (c);
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
    for (int x = 0; x < w; ++x)
    {
        const int64_t a = s0 + (int64_t) (x * spp), b = s0 + (int64_t) ((x + 1) * spp);
        float hi = 0.0f;
        if (level >= 0)
        {
            const int spb = peaks.samplesPerBin (level);
            const auto& bins = peaks.levels[(size_t) level];
            for (int64_t i = a / spb; i <= b / spb && i < (int64_t) bins.size(); ++i)
                hi = std::max ({ hi, bins[(size_t) i].max, -bins[(size_t) i].min });
        }
        else
        {
            const float* d = snap.audio.getReadPointer (0);
            for (int64_t i = a; i < std::max (a + 1, b) && i < snap.numSamples(); ++i) hi = std::max (hi, std::abs (d[i]));
        }
        const float h = std::max (1.0f, hi * half);
        g.fillRect (r.getX() + (float) x, mid - h, 1.0f, h * 2.0f);
    }
}

static juce::String sortHeading (Session::Sort s)
{
    if (s == Session::Sort::Time)  return {};
    if (s == Session::Sort::Match) return "MATCH";
    return FragmentScores::name ((int) s - 1);
}

//==============================================================================
class FragmentList::Row : public juce::Component
{
public:
    explicit Row (FragmentList& o) : owner (o) {}
    int fragIndex = -1;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        const auto& frs = owner.session.getFragments();
        auto snap = owner.session.getSnapshot();
        if (fragIndex < 0 || fragIndex >= (int) frs.size()) return;
        const int i = fragIndex;
        const auto& f = frs[(size_t) i];
        const bool on = i < 64 && ((owner.engine.getSoundingMask() >> i) & 1u);
        const auto sort = owner.session.getSort();

        auto r = getLocalBounds().toFloat();
        g.setColour (selected ? colours::panelHi : colours::panel);
        g.fillRect (r);
        if (selected) { g.setColour (colours::accent); g.fillRect (r.withWidth (2.0f)); }
        g.setColour (colours::line);
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());

        auto area = getLocalBounds().reduced (8, 0);
        g.setFont (GhostLookAndFeel::mono (11.0f));
        g.setColour (on ? colours::accent : colours::text);
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2) + (f.manual ? "*" : "") + (owner.session.isPinned (i) ? "^" : ""),
                    area.removeFromLeft (30), juce::Justification::centredLeft);

        g.setColour (colours::textDim);
        g.drawText (owner.isSlices() ? noteName (owner.getBaseNote() + i) : "--", area.removeFromLeft (38), juce::Justification::centredLeft);
        if (snap != nullptr)
        {
            g.drawText (formatTime ((double) f.start / snap->sampleRate), area.removeFromLeft (58), juce::Justification::centredLeft);
            g.drawText (juce::String ((double) f.length() / snap->sampleRate, 2) + "s", area.removeFromLeft (48), juce::Justification::centredLeft);
        }
        const int root = f.effectiveRoot (-1);
        g.setColour (f.rootOverride >= 0 ? colours::accent : colours::textDim);
        g.drawText (root >= 0 ? noteName (root) : "--", area.removeFromLeft (38), juce::Justification::centredLeft);

        // sort value (score or match)
        auto valueCol = area.removeFromLeft (62);
        if (sort != Session::Sort::Time)
        {
            const float v = owner.session.sortValue (i);
            drawScoreBar (g, valueCol.toFloat().withSizeKeepingCentre (30, 3).withX ((float) valueCol.getX()), v, colours::accent);
            g.setColour (colours::text);
            g.drawText (juce::String (juce::roundToInt (v * 100)), valueCol.withTrimmedLeft (34), juce::Justification::centredLeft);
        }

        // character: five stacked mini bars E H N Hv X
        auto ch = area.removeFromLeft (64).toFloat().reduced (0.0f, 6.0f);
        const float bw = (ch.getWidth() - 8.0f) / 5.0f;
        for (int k = 0; k < FragmentScores::numKinds; ++k)
        {
            auto col = juce::Rectangle<float> (ch.getX() + k * (bw + 2.0f), ch.getY(), bw, ch.getHeight());
            g.setColour (colours::line);
            g.fillRect (col);
            const float v = f.measured ? f.scores[k] : 0.0f;
            const bool hl = sort != Session::Sort::Time && sort != Session::Sort::Match && (int) sort - 1 == k;
            g.setColour (hl ? colours::accent : colours::fragment.brighter (0.3f));
            g.fillRect (col.withTop (col.getBottom() - col.getHeight() * v));
        }
        area.removeFromLeft (8);

        if (snap != nullptr && area.getWidth() > 30)
            drawMiniWave (g, area.toFloat().reduced (2.0f, 5.0f), *snap, f.start, f.end,
                          selected ? colours::fragmentSel : colours::fragment);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragged = false;
        owner.session.selectFragment (fragIndex);
        owner.session.auditionFragment (fragIndex);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged || e.getDistanceFromDragStart() < 8) return;
        dragged = true;
        juce::String err;
        auto file = owner.session.renderFragmentForDrag (fragIndex, err);
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

private:
    FragmentList& owner;
    bool dragged = false;
};

FragmentList::FragmentList (Session& s, SamplerEngine& e, std::function<int()> base, std::function<bool()> slices)
    : session (s), engine (e), getBaseNote (std::move (base)), isSlices (std::move (slices))
{
    list.setModel (this);
    list.setRowHeight (28);
    list.setColour (juce::ListBox::backgroundColourId, colours::panel);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);
    session.addChangeListener (this);
    order = session.getDisplayOrder();
}

FragmentList::~FragmentList() { session.removeChangeListener (this); }

void FragmentList::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto header = getLocalBounds().removeFromTop (22).reduced (8, 0);
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    auto col = [&] (const juce::String& t, int w) { g.drawText (t, header.removeFromLeft (w), juce::Justification::centredLeft); };
    col ("#", 30); col ("KEY", 38); col ("TIME", 58); col ("LEN", 48); col ("ROOT", 38);
    col (sortHeading (session.getSort()), 62); col ("E H N Hv X", 72);

    if (session.getFragments().empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (12.0f));
        g.drawText (session.hasSource() ? "No fragments yet. Press ANALYZE or select a region and press + FRAGMENT."
                                        : "Import audio to discover fragments.",
                    getLocalBounds().withTrimmedTop (22), juce::Justification::centred);
    }
}

void FragmentList::resized() { list.setBounds (getLocalBounds().withTrimmedTop (22)); }

void FragmentList::refresh()
{
    order = session.getDisplayOrder();
    list.updateContent();
    list.repaint();
    repaint();
}

int FragmentList::getNumRows() { return (int) order.size(); }

juce::Component* FragmentList::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr) { delete existing; r = new Row (*this); }
    r->fragIndex = row >= 0 && row < (int) order.size() ? order[(size_t) row] : -1;
    r->selected = r->fragIndex >= 0 && r->fragIndex == session.getSelectedIndex();
    r->setVisible (r->fragIndex >= 0);
    r->repaint();
    return r;
}

void FragmentList::changeListenerCallback (juce::ChangeBroadcaster*)
{
    order = session.getDisplayOrder();
    list.updateContent();
    // a new ranking (sort or search) starts at the top; otherwise keep the selection visible
    const auto rankKey = juce::String ((int) session.getSort()) + "|" + session.getSearchText();
    if (rankKey != lastRankKey) { lastRankKey = rankKey; list.scrollToEnsureRowIsOnscreen (0); }
    else
    {
        const int sel = session.getSelectedIndex();
        for (int row = 0; row < (int) order.size(); ++row)
            if (order[(size_t) row] == sel) { list.scrollToEnsureRowIsOnscreen (row); break; }
    }
    list.repaint();
    repaint();
}

//==============================================================================
class FragmentInspector::DragHandle : public juce::Component
{
public:
    explicit DragHandle (Session& s) : session (s) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isEnabled() ? colours::panelHi : colours::panel);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        g.setColour (isEnabled() ? colours::text : colours::textFaint);
        GhostLookAndFeel::drawTracked (g, "DRAG TO DAW", r, GhostLookAndFeel::label (11.0f, true), 0.12f, juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 6) return;
        started = true;
        juce::String err;
        auto f = session.renderFragmentForDrag (session.getSelectedIndex(), err);
        if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    Session& session;
    bool started = false;
};

FragmentInspector::FragmentInspector (Session& s) : session (s)
{
    dragHandle = std::make_unique<DragHandle> (session);
    addAndMakeVisible (*dragHandle);
    for (auto* b : { &audition, &exportBtn, &del, &pin }) addAndMakeVisible (b);
    addAndMakeVisible (rootBox);
    pin.setTooltip ("Pin this fragment for GENERATE (up to 8). With no pins, GENERATE picks the best ones for the mode.");
    pin.onClick = [this] { session.togglePin (session.getSelectedIndex()); };

    rootBox.addItem ("ROOT: AUTO", 1);
    for (int n = 24; n <= 96; ++n) rootBox.addItem ("ROOT: " + noteName (n), n + 2);
    rootBox.setSelectedId (1, juce::dontSendNotification);
    rootBox.setTooltip ("Root note used in CHROMATIC mode. AUTO uses the detected pitch.");
    rootBox.onChange = [this]
    {
        const int id = rootBox.getSelectedId();
        session.setFragmentRootOverride (session.getSelectedIndex(), id <= 1 ? -1 : id - 2);
    };

    audition.onClick = [this] { session.auditionFragment (session.getSelectedIndex()); };
    exportBtn.onClick = [this] { exportSelected(); };
    exportBtn.setTooltip ("Exports what you hear: the original, or the isolated vocals when VOCALS is on.");
    del.onClick = [this] { session.deleteFragment (session.getSelectedIndex()); };

    session.addChangeListener (this);
    update();
}

FragmentInspector::~FragmentInspector() { session.removeChangeListener (this); }

void FragmentInspector::changeListenerCallback (juce::ChangeBroadcaster*) { update(); }

void FragmentInspector::update()
{
    const int i = session.getSelectedIndex();
    const bool has = i >= 0 && i < (int) session.getFragments().size() && session.hasSource();
    for (juce::Component* c : { (juce::Component*) &audition, (juce::Component*) &exportBtn, (juce::Component*) &del,
                                (juce::Component*) &rootBox, (juce::Component*) dragHandle.get(), (juce::Component*) &pin })
        c->setEnabled (has);
    pin.setToggleState (has && session.isPinned (i), juce::dontSendNotification);
    pin.setButtonText (has && session.isPinned (i) ? "Pinned" : "Pin");
    if (has)
    {
        const int o = session.getFragments()[(size_t) i].rootOverride;
        rootBox.setSelectedId (o < 0 ? 1 : o + 2, juce::dontSendNotification);
    }
    repaint();
}

void FragmentInspector::exportSelected()
{
    auto snap = session.getOriginalSnapshot();
    const int i = session.getSelectedIndex();
    if (snap == nullptr || i < 0) return;
    const auto& f = session.getFragments()[(size_t) i];
    auto name = AudioExporter::makeFragmentFileName (*snap, i + 1, f.start);
    if (session.getView() == Session::View::Vocals) name = name.replace (".wav", "_vocals.wav");
    auto def = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name);
    chooser = std::make_unique<juce::FileChooser> ("Export fragment", def, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, i] (const juce::FileChooser& fc)
                          {
                              auto dest = fc.getResult();
                              if (dest == juce::File()) return;
                              const auto err = session.exportFragment (i, dest.withFileExtension ("wav"));
                              if (err.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", err);
                          });
}

void FragmentInspector::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto r = getLocalBounds().reduced (14, 10);
    const int i = session.getSelectedIndex();
    auto snap = session.getSnapshot();

    g.setColour (colours::textFaint);
    GhostLookAndFeel::drawTracked (g, "FRAGMENT", r.removeFromTop (14).toFloat(), GhostLookAndFeel::label (9.5f, true), 0.2f, juce::Justification::centredLeft);

    if (i < 0 || i >= (int) session.getFragments().size() || snap == nullptr)
    {
        g.setColour (colours::textDim);
        g.setFont (GhostLookAndFeel::label (12.0f));
        g.drawText ("Nothing selected", r.removeFromTop (30), juce::Justification::centredLeft);
        return;
    }

    const auto& f = session.getFragments()[(size_t) i];
    g.setColour (colours::text);
    GhostLookAndFeel::drawTracked (g, juce::String (i + 1).paddedLeft ('0', 2) + (f.manual ? "  MANUAL" : "  DETECTED"),
                                   r.removeFromTop (26).withTrimmedRight (rootBox.getWidth() + 8).toFloat(),
                                   GhostLookAndFeel::label (18.0f, true), 0.08f, juce::Justification::centredLeft);
    r.removeFromTop (4);
    r.removeFromBottom (28 + 6 + 28 + 6);

    auto left = r.removeFromLeft (r.getWidth() * 55 / 100);
    r.removeFromLeft (10);
    auto right = r;

    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = left.removeFromTop (15);
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (9.5f, true));
        g.drawText (k, row.removeFromLeft (64), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.setFont (GhostLookAndFeel::mono (10.5f));
        g.drawText (v, row, juce::Justification::centredLeft, true);
    };
    const auto& ft = f.features;
    const bool pitched = ft.medianF0Hz > 0;
    line ("START", formatTime ((double) f.start / snap->sampleRate) + "   " + juce::String (ft.durationSec > 0 ? ft.durationSec : (double) f.length() / snap->sampleRate, 2) + " s");
    line ("PITCH", pitched ? juce::String (ft.medianF0Hz, 1) + " Hz  " + noteName (f.detectedNote) : "no stable pitch");
    line ("CONTOUR", pitched ? "range " + juce::String (ft.pitchRangeSemis, 1) + "st  slope " + juce::String (ft.pitchSlope, 1) + "st/s" : "--");
    line ("BEND", pitched ? juce::String (ft.bendSemis, 1) + " st glide, " + juce::String (ft.jumpsPerSec, 1) + " jumps/s" : "--");
    line ("HELD", juce::String (ft.sustainSec, 2) + " s longest note");
    line ("VIBRATO", ft.vibratoRateHz > 0 ? juce::String (ft.vibratoRateHz, 1) + " Hz, " + juce::String (ft.vibratoDepthSemis, 2) + " st" : "none");
    line ("VOICE", juce::String (juce::roundToInt (ft.voicedRatio * 100)) + "% voiced, harm " + juce::String (ft.harmonicity, 2));
    line ("BREATH", juce::String (ft.breathiness, 2) + " (heur.)  air " + juce::String (juce::roundToInt (ft.airiness * 100)) + "%");
    line ("TONE", juce::String (juce::roundToInt (ft.centroidHz)) + " Hz centroid, " + juce::String (ft.meanDb, 1) + " dB");
    line ("ENVELOPE", "attack " + juce::String (juce::roundToInt (ft.attackSec * 1000)) + " ms, tail " + juce::String (juce::roundToInt (ft.decaySec * 1000)) + " ms");

    // scores
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("CHARACTER  (heuristic)", right.removeFromTop (15), juce::Justification::centredLeft);
    int best = 0;
    for (int k = 1; k < FragmentScores::numKinds; ++k) if (f.scores[k] > f.scores[best]) best = k;
    for (int k = 0; k < FragmentScores::numKinds; ++k)
    {
        auto row = right.removeFromTop (17);
        g.setColour (k == best ? colours::text : colours::textDim);
        g.setFont (GhostLookAndFeel::label (9.5f, true));
        g.drawText (FragmentScores::name (k), row.removeFromLeft (84), juce::Justification::centredLeft);
        auto val = row.removeFromRight (24);
        drawScoreBar (g, row.toFloat().withSizeKeepingCentre ((float) row.getWidth(), 3.0f), f.measured ? f.scores[k] : 0.0f,
                      k == best ? colours::accent : colours::fragment.brighter (0.4f));
        g.setFont (GhostLookAndFeel::mono (10.0f));
        g.drawText (f.measured ? juce::String (juce::roundToInt (f.scores[k] * 100)) : "--", val, juce::Justification::centredRight);
    }
    right.removeFromTop (4);
    if (f.measured)
    {
        g.setColour (colours::textDim);
        g.setFont (GhostLookAndFeel::label (10.0f));
        g.drawFittedText ("Most " + juce::String (FragmentScores::name (best)).toLowerCase() + ": "
                              + FragmentRanker::explain (f, best), right.removeFromTop (30), juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (10.0f));
        g.drawText ("Measuring...", right.removeFromTop (15), juce::Justification::centredLeft);
    }
}

void FragmentInspector::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto bottom = r.removeFromBottom (28);
    const int w = (bottom.getWidth() - 12) / 3;
    audition.setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (6);
    exportBtn.setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (6);
    del.setBounds (bottom);
    r.removeFromBottom (6);
    auto dragRow = r.removeFromBottom (28);
    pin.setBounds (dragRow.removeFromRight (90));
    dragRow.removeFromRight (6);
    dragHandle->setBounds (dragRow);
    rootBox.setBounds (r.withTrimmedTop (14).removeFromTop (24).removeFromRight (std::min (120, r.getWidth() / 2)));
}

} // namespace ghostcut
