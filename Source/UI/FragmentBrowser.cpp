#include "FragmentBrowser.h"
#include "GhostLookAndFeel.h"

namespace ghostcut
{

static juce::String formatTime (double seconds)
{
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String (m) + ":" + (s < 10.0 ? "0" : "") + juce::String (s, 2);
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

//==============================================================================
class FragmentList::Row : public juce::Component
{
public:
    explicit Row (FragmentList& o) : owner (o) {}
    int row = -1;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        const auto& frs = owner.session.getFragments();
        auto snap = owner.session.getSnapshot();
        if (row < 0 || row >= (int) frs.size()) return;
        const auto& f = frs[(size_t) row];
        const bool on = row < 64 && ((owner.engine.getSoundingMask() >> row) & 1u);

        auto r = getLocalBounds().toFloat();
        g.setColour (selected ? colours::panelHi : colours::panel);
        g.fillRect (r);
        if (selected) { g.setColour (colours::accent); g.fillRect (r.withWidth (2.0f)); }
        g.setColour (colours::line);
        g.drawHorizontalLine (getHeight() - 1, 0, (float) getWidth());

        auto area = getLocalBounds().reduced (8, 0);
        g.setFont (GhostLookAndFeel::mono (11.0f));
        g.setColour (on ? colours::accent : colours::text);
        g.drawText (juce::String (row + 1).paddedLeft ('0', 2), area.removeFromLeft (26), juce::Justification::centredLeft);

        g.setColour (colours::textDim);
        const juce::String key = owner.isSlices() ? noteName (owner.getBaseNote() + row) : "--";
        g.drawText (key, area.removeFromLeft (40), juce::Justification::centredLeft);

        if (snap != nullptr)
        {
            g.drawText (formatTime ((double) f.start / snap->sampleRate), area.removeFromLeft (62), juce::Justification::centredLeft);
            g.drawText (juce::String ((double) f.length() / snap->sampleRate, 2) + "s", area.removeFromLeft (52), juce::Justification::centredLeft);
        }
        const int root = f.effectiveRoot (-1);
        g.setColour (f.rootOverride >= 0 ? colours::accent : colours::textDim);
        g.drawText (root >= 0 ? noteName (root) : "--", area.removeFromLeft (40), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.drawText (juce::String (juce::roundToInt (f.features.voicedRatio * 100)) + "%v", area.removeFromLeft (46), juce::Justification::centredLeft);
        if (f.manual) { g.setColour (colours::textFaint); g.drawText ("MAN", area.removeFromLeft (32), juce::Justification::centredLeft); }
        else area.removeFromLeft (32);

        if (snap != nullptr && area.getWidth() > 30)
            drawMiniWave (g, area.toFloat().reduced (2.0f, 5.0f), *snap, f.start, f.end,
                          selected ? colours::fragmentSel : colours::fragment);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragged = false;
        owner.session.selectFragment (row);
        owner.session.auditionFragment (row);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged || e.getDistanceFromDragStart() < 8) return;
        dragged = true;
        juce::String err;
        auto file = owner.session.renderFragmentForDrag (row, err);
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
}

FragmentList::~FragmentList() { session.removeChangeListener (this); }

void FragmentList::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    auto header = getLocalBounds().removeFromTop (22).reduced (8, 0);
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    auto col = [&] (const char* t, int w) { g.drawText (t, header.removeFromLeft (w), juce::Justification::centredLeft); };
    col ("#", 26); col ("KEY", 40); col ("TIME", 62); col ("LEN", 52); col ("ROOT", 40); col ("VOICED", 46); col ("", 32);
    col ("FRAGMENTS", 120);

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
void FragmentList::refresh() { list.updateContent(); list.repaint(); }

int FragmentList::getNumRows() { return (int) session.getFragments().size(); }

juce::Component* FragmentList::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr) { delete existing; r = new Row (*this); }
    r->row = row;
    r->selected = row == session.getSelectedIndex();
    r->setVisible (row < getNumRows());
    r->repaint();
    return r;
}

void FragmentList::changeListenerCallback (juce::ChangeBroadcaster*)
{
    list.updateContent();
    const int sel = session.getSelectedIndex();
    if (sel >= 0) list.scrollToEnsureRowIsOnscreen (sel);
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
        float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ r.getX() + 4, r.getY() + 1, r.getRight() - 4, r.getY() + 1 }, dashes, 2);
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
    for (auto* b : { &audition, &exportBtn, &del }) addAndMakeVisible (b);
    addAndMakeVisible (rootBox);

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
                                (juce::Component*) &rootBox, (juce::Component*) dragHandle.get() })
        c->setEnabled (has);
    if (has)
    {
        const int o = session.getFragments()[(size_t) i].rootOverride;
        rootBox.setSelectedId (o < 0 ? 1 : o + 2, juce::dontSendNotification);
    }
    repaint();
}

void FragmentInspector::exportSelected()
{
    auto snap = session.getSnapshot();
    const int i = session.getSelectedIndex();
    if (snap == nullptr || i < 0) return;
    const auto& f = session.getFragments()[(size_t) i];
    auto def = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile (AudioExporter::makeFragmentFileName (*snap, i + 1, f.start));
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

    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = r.removeFromTop (15);
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (10.0f, true));
        g.drawText (k, row.removeFromLeft (96), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.setFont (GhostLookAndFeel::mono (11.0f));
        g.drawText (v, row, juce::Justification::centredLeft);
    };
    const auto& ft = f.features;
    line ("START", formatTime ((double) f.start / snap->sampleRate));
    line ("LENGTH", juce::String ((double) f.length() / snap->sampleRate, 3) + " s");
    line ("PITCH", ft.medianF0Hz > 0 ? juce::String (ft.medianF0Hz, 1) + " Hz  " + noteName (f.detectedNote) : "no stable pitch");
    line ("PITCH SPREAD", ft.medianF0Hz > 0 ? juce::String (ft.pitchStabilitySemis, 2) + " st" : "--");
    line ("VOICED", juce::String (juce::roundToInt (ft.voicedRatio * 100)) + " %");
    line ("LEVEL", juce::String (ft.meanDb, 1) + " dB avg / " + juce::String (ft.peakDb, 1) + " pk");
    line ("BRIGHTNESS", juce::String (juce::roundToInt (ft.centroidHz)) + " Hz centroid");

    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f));
    g.drawText ("Measurements only. Character scores arrive in Phase 2.", r.removeFromTop (14), juce::Justification::centredLeft);
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
    dragHandle->setBounds (r.removeFromBottom (28));
    // root note picker sits top-right, level with the fragment heading
    rootBox.setBounds (r.withTrimmedTop (14).removeFromTop (24).removeFromRight (std::min (120, r.getWidth() / 2)));
}

} // namespace ghostcut
