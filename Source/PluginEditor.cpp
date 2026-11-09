#include "PluginEditor.h"

using namespace ghostcut;

static constexpr int headerH = 52;
static constexpr int statusH = 26;

//==============================================================================
void PlannedPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto r = getLocalBounds().reduced (40);
    g.setColour (colours::text);
    GhostLookAndFeel::drawTracked (g, heading, r.removeFromTop (40).toFloat(), GhostLookAndFeel::label (26.0f, true), 0.3f,
                                   juce::Justification::centredLeft);
    g.setColour (colours::warn);
    GhostLookAndFeel::drawTracked (g, phaseText, r.removeFromTop (20).toFloat(), GhostLookAndFeel::label (11.0f, true), 0.2f,
                                   juce::Justification::centredLeft);
    r.removeFromTop (16);
    g.setColour (colours::textDim);
    g.setFont (GhostLookAndFeel::label (13.0f));
    g.drawText ("Not built yet. Nothing on this page is active in this version.", r.removeFromTop (22), juce::Justification::centredLeft);
    r.removeFromTop (12);
    g.setColour (colours::textFaint);
    for (auto& b : bullets)
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94  ") + b, r.removeFromTop (20), juce::Justification::centredLeft);
}

//==============================================================================
SettingsPanel::SettingsPanel (Session& s) : session (s)
{
    bitDepth.addItem ("16-bit integer", 16);
    bitDepth.addItem ("24-bit integer", 24);
    bitDepth.addItem ("32-bit float", 32);
    bitDepth.setSelectedId ((int) session.getExportDepth(), juce::dontSendNotification);
    bitDepth.onChange = [this] { session.setExportDepth ((AudioExporter::BitDepth) bitDepth.getSelectedId()); };
    addAndMakeVisible (bitDepth);
    openFolder.onClick = [] { auto f = AudioExporter::getDragExportFolder(); f.createDirectory(); f.startAsProcess(); };
    addAndMakeVisible (openFolder);
    session.addChangeListener (this);
}

SettingsPanel::~SettingsPanel() { session.removeChangeListener (this); }

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    bitDepth.setSelectedId ((int) session.getExportDepth(), juce::dontSendNotification);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto r = getLocalBounds().reduced (40);
    g.setColour (colours::text);
    GhostLookAndFeel::drawTracked (g, "SETTINGS", r.removeFromTop (40).toFloat(), GhostLookAndFeel::label (26.0f, true), 0.3f,
                                   juce::Justification::centredLeft);
    r.removeFromTop (10);
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (10.0f, true));
    g.drawText ("EXPORT BIT DEPTH", r.removeFromTop (14), juce::Justification::centredLeft);
    r.removeFromTop (40);

    g.drawText ("STORAGE", r.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (GhostLookAndFeel::label (12.5f));
    const juce::StringArray lines {
        "Projects store a reference to the source file (absolute path, size and a content fingerprint), not the audio.",
        "Keep source files with your project, or use Live's Collect All and Save, then LOCATE if a path breaks.",
        "Fragments, trim, selection and root notes are saved inside the DAW project.",
        "Exports are written at the source's own sample rate. Unprocessed fragments are not resampled.",
        "Drag-to-DAW renders go to: " + AudioExporter::getDragExportFolder().getFullPathName(),
    };
    r.removeFromTop (4);
    for (auto& l : lines) g.drawText (l, r.removeFromTop (20), juce::Justification::centredLeft);

    r.removeFromTop (56);
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (10.0f, true));
    g.drawText ("ABOUT", r.removeFromTop (14), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (GhostLookAndFeel::label (12.5f));
    g.drawText (juce::String ("GHOSTCUT ") + JucePlugin_VersionString + "  /  Phase 1 build  /  TRIPLE SEVEN AUDIO",
                r.removeFromTop (20), juce::Justification::centredLeft);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (40);
    r.removeFromTop (40 + 10 + 16);
    bitDepth.setBounds (r.removeFromTop (26).withWidth (200));
    r.removeFromTop (14 + 14 + 4 + 20 * 5 + 10);
    openFolder.setBounds (r.removeFromTop (28).withWidth (160));
}

//==============================================================================
GhostcutEditor::GhostcutEditor (GhostcutProcessor& p)
    : AudioProcessorEditor (p), processor (p), session (p.getSession()),
      discover (p.getSession(), p.getEngine(), p.getParameters()),
      generate ("GENERATE", "PHASE 3", { "Automatic chop generation from selected fragments",
                                         "ETHEREAL / HAUNTED / NOSTALGIA / HEAVEN / CHAOS generation modes",
                                         "Key, scale, BPM, density and seeded randomness" }),
      sequence ("SEQUENCE", "PHASE 3", { "16 / 32-step patterns with swing", "Host tempo sync", "Pattern save and MIDI export" }),
      effects ("EFFECTS", "PHASE 4", { "Reverb, delay, pitch / formant, texture, reverse, lo-fi",
                                       "Integrated with playback and export" }),
      settings (p.getSession())
{
    setLookAndFeel (&lnf);

    const juce::StringArray names { "Discover", "Generate", "Sequence", "Effects", "Settings" };
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setRadioGroupId (777);
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }

    for (juce::Component* c : { (juce::Component*) &discover, (juce::Component*) &generate, (juce::Component*) &sequence,
                                (juce::Component*) &effects, (juce::Component*) &settings })
        addChildComponent (c);

    addAndMakeVisible (progress);
    progress.setPercentageDisplay (false);
    session.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1000, 720, 2200, 1500);
    setSize (1180, 800);
    showTab (0);
    startTimerHz (15);
}

GhostcutEditor::~GhostcutEditor()
{
    session.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void GhostcutEditor::showTab (int index)
{
    currentTab = index;
    juce::Component* pages[] = { &discover, &generate, &sequence, &effects, &settings };
    for (int i = 0; i < 5; ++i)
    {
        pages[i]->setVisible (i == index);
        tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    }
    if (index == 0) discover.grabKeyboardFocus();
}

void GhostcutEditor::timerCallback()
{
    const bool busy = session.isBusy();
    progress.setVisible (busy);
    progressValue = (double) session.getProgress();
    if (busy) repaint (statusArea);
}

void GhostcutEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    // header
    g.setColour (colours::panel);
    g.fillRect (headerArea);
    g.setColour (colours::line);
    g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

    auto brand = brandArea.toFloat();
    g.setColour (colours::accent);
    GhostLookAndFeel::drawTracked (g, "GHOSTCUT", brand.removeFromTop (brand.getHeight() * 0.62f),
                                   GhostLookAndFeel::label (21.0f, true), 0.42f, juce::Justification::bottomLeft);
    // subtle distressed accent: thin scan gaps across the wordmark
    g.setColour (colours::panel.withAlpha (0.55f));
    for (float y = (float) brandArea.getY() + 12.0f; y < brandArea.getY() + brandArea.getHeight() * 0.62f; y += 4.0f)
        g.fillRect ((float) brandArea.getX(), y, 150.0f, 1.0f);
    g.setColour (colours::textDim);
    GhostLookAndFeel::drawTracked (g, "TRIPLE SEVEN AUDIO", brand, GhostLookAndFeel::label (9.0f, true), 0.35f,
                                   juce::Justification::topLeft);

    // status bar: always shows the current sample and analysis state
    g.setColour (colours::panel);
    g.fillRect (statusArea);
    g.setColour (colours::line);
    g.drawHorizontalLine (statusArea.getY(), 0.0f, (float) getWidth());

    auto s = statusArea.reduced (12, 0);
    g.setFont (GhostLookAndFeel::mono (11.0f));
    if (auto snap = session.getSnapshot())
    {
        const auto info = snap->displayName + "   " + juce::String (snap->sampleRate / 1000.0, 1) + " kHz   "
                          + (snap->audio.getNumChannels() > 1 ? "STEREO" : "MONO") + "   "
                          + juce::String (snap->lengthSeconds(), 2) + " s   "
                          + juce::String (session.getFragments().size()) + " FRAGMENTS";
        g.setColour (colours::text);
        g.drawText (info, s.removeFromLeft (s.getWidth() / 2), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (colours::textFaint);
        g.drawText ("NO SOURCE", s.removeFromLeft (s.getWidth() / 2), juce::Justification::centredLeft);
    }

    g.setColour (session.hasError() ? colours::warn : colours::textDim);
    auto statusText = session.getStatusText();
    if (session.isBusy()) statusText += "  " + juce::String (juce::roundToInt (session.getProgress() * 100)) + "%";
    g.drawText (statusText, s.withTrimmedRight (session.isBusy() ? 170 : 0), juce::Justification::centredRight, true);
}

void GhostcutEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    g.setColour (colours::bg.withAlpha (0.75f));
    g.fillRect (getLocalBounds());
    g.setColour (colours::accent);
    g.drawRect (getLocalBounds().reduced (10), 1);
    GhostLookAndFeel::drawTracked (g, "DROP TO IMPORT", getLocalBounds().toFloat(), GhostLookAndFeel::label (18.0f, true), 0.4f,
                                   juce::Justification::centred);
}

void GhostcutEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (headerH);
    statusArea = r.removeFromBottom (statusH);

    auto h = headerArea.reduced (16, 0);
    brandArea = h.removeFromLeft (220).reduced (0, 9);
    auto tabRow = h.withSizeKeepingCentre (std::min (h.getWidth(), 5 * 112), 28);
    for (auto* t : tabs) { t->setBounds (tabRow.removeFromLeft (108)); tabRow.removeFromLeft (4); }

    progress.setBounds (statusArea.reduced (12, 0).removeFromRight (150).withSizeKeepingCentre (150, 8));

    for (juce::Component* c : { (juce::Component*) &discover, (juce::Component*) &generate, (juce::Component*) &sequence,
                                (juce::Component*) &effects, (juce::Component*) &settings })
        c->setBounds (r);
}

void GhostcutEditor::mouseUp (const juce::MouseEvent& e)
{
    if (brandArea.contains (e.getPosition()))
        juce::URL ("https://triplesevenaudio.com").launchInDefaultBrowser();
}

bool GhostcutEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (session.getImporter().canImport (juce::File (f))) return true;
    return false;
}

void GhostcutEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (auto& f : files)
    {
        const juce::File file (f);
        if (session.getImporter().canImport (file))
        {
            if (session.getMissingSource() != juce::File() && file.getFileName() == session.getMissingSource().getFileName())
                session.relocateMissingSource (file);
            else
                session.importFile (file);
            showTab (0);
            return;
        }
    }
}
