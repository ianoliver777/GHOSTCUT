#include "PluginEditor.h"
#include "Analysis/OnnxRuntime.h"

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

    method.onChange = [this]
    {
        auto o = session.getSeparationOptions();
        const int id = method.getSelectedId();
        if (id == 1) o.method = VocalSeparator::Method::CenterExtract;
        else if (id >= 10 && id - 10 < models.size()) { o.method = VocalSeparator::Method::MdxModel; o.model = models[id - 10]; }
        session.setSeparationOptions (o);
    };
    addAndMakeVisible (method);

    installModel.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Install vocal model (.onnx)",
                                                       juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
                                                       "*.onnx");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (! fc.getResult().existsAsFile()) return;
                                  VocalModelInfo info; juce::String err;
                                  if (! VocalModels::install (fc.getResult(), info, err))
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Model not installed", err);
                                      return;
                                  }
                                  auto o = session.getSeparationOptions();
                                  o.method = VocalSeparator::Method::MdxModel;
                                  o.model = info;
                                  session.setSeparationOptions (o);
                                  refreshMethods();
                              });
    };
    openModels.onClick = [] { auto f = VocalModels::getFolder(); f.createDirectory(); f.startAsProcess(); };
    clearCache.onClick = [this] { VocalSeparator::clearCache(); repaint(); };
    for (auto* b : { &installModel, &openModels, &clearCache }) addAndMakeVisible (b);

    session.addChangeListener (this);
    refreshMethods();
}

SettingsPanel::~SettingsPanel() { session.removeChangeListener (this); }

void SettingsPanel::refreshMethods()
{
    models = VocalModels::scan();
    method.clear (juce::dontSendNotification);
    method.addItem ("Center extract (DSP, stereo only)", 1);
    for (int i = 0; i < models.size(); ++i)
        method.addItem ("ML: " + models[i].displayName, 10 + i);
    const auto o = session.getSeparationOptions();
    int sel = 1;
    if (o.method == VocalSeparator::Method::MdxModel)
        for (int i = 0; i < models.size(); ++i) if (models[i].file == o.model.file) sel = 10 + i;
    method.setSelectedId (sel, juce::dontSendNotification);
    method.setItemEnabled (1, true);
    repaint();
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    bitDepth.setSelectedId ((int) session.getExportDepth(), juce::dontSendNotification);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto r = getLocalBounds().reduced (40, 30);
    g.setColour (colours::text);
    GhostLookAndFeel::drawTracked (g, "SETTINGS", r.removeFromTop (40).toFloat(), GhostLookAndFeel::label (26.0f, true), 0.3f,
                                   juce::Justification::centredLeft);

    auto heading = [&] (juce::Rectangle<int> area, const juce::String& t)
    {
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (10.0f, true));
        g.drawText (t, area.removeFromTop (14), juce::Justification::centredLeft);
    };
    auto lines = [&] (juce::Rectangle<int> area, const juce::StringArray& ls, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (GhostLookAndFeel::label (12.0f));
        for (auto& l : ls) g.drawText (l, area.removeFromTop (19), juce::Justification::centredLeft, true);
    };

    heading (method.getBounds().translated (0, -18).withHeight (14), "VOCAL SEPARATION");
    auto& ort = OnnxRuntime::get();
    const auto cacheMb = (double) VocalSeparator::getCacheSizeBytes() / (1024.0 * 1024.0);
    lines (sepTextArea, {
        ort.isAvailable() ? "ONNX Runtime " + ort.getVersion() + " loaded (CPU, offline)."
                          : "ML unavailable: " + ort.getError(),
        models.isEmpty() ? "No ML model installed. Center extract works on stereo mixes only. See README for supported models."
                         : juce::String (models.size()) + " model(s) in " + VocalModels::getFolder().getFullPathName(),
        "Model weights are not included with GHOSTCUT. Check each model's license before commercial use.",
        "Separated stems are cached: " + juce::String (cacheMb, 1) + " MB in " + VocalSeparator::getCacheFolder().getFullPathName(),
    }, colours::textDim);

    heading (bitDepth.getBounds().translated (0, -18).withHeight (14), "EXPORT BIT DEPTH");

    heading (storageArea, "PROJECT STORAGE");
    lines (storageArea.withTrimmedTop (16), {
        "Projects store a reference to the source file (path, size, fingerprint), not the audio.",
        "Fragments, trim, view, search and root notes are saved in the DAW project. Isolated vocals reload from the stem cache.",
        "Use Live's Collect All and Save, then LOCATE if a path breaks.",
        "Drag-to-DAW renders: " + AudioExporter::getDragExportFolder().getFullPathName(),
    }, colours::textDim);

    heading (aboutArea, "ABOUT");
    lines (aboutArea.withTrimmedTop (16), { juce::String ("GHOSTCUT ") + JucePlugin_VersionString + "  /  Phase 2 build  /  TRIPLE SEVEN AUDIO" }, colours::textDim);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (40, 30);
    r.removeFromTop (40 + 26);
    auto row = r.removeFromTop (26);
    method.setBounds (row.removeFromLeft (320)); row.removeFromLeft (8);
    installModel.setBounds (row.removeFromLeft (130)); row.removeFromLeft (6);
    openModels.setBounds (row.removeFromLeft (150)); row.removeFromLeft (6);
    clearCache.setBounds (row.removeFromLeft (140));
    r.removeFromTop (8);
    sepTextArea = r.removeFromTop (19 * 4);
    r.removeFromTop (30);
    auto row2 = r.removeFromTop (26);
    bitDepth.setBounds (row2.removeFromLeft (200)); row2.removeFromLeft (8);
    openFolder.setBounds (row2.removeFromLeft (160));
    r.removeFromTop (24);
    storageArea = r.removeFromTop (16 + 19 * 4);
    r.removeFromTop (20);
    aboutArea = r.removeFromTop (40);
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
    setResizeLimits (1100, 780, 2200, 1500);
    setSize (1240, 860);
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
