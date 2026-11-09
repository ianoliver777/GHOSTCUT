#include "DiscoverPanel.h"
#include "GhostLookAndFeel.h"
#include "../PluginProcessor.h"

namespace ghostcut
{

DiscoverPanel::DiscoverPanel (Session& s, SamplerEngine& e, juce::AudioProcessorValueTreeState& a)
    : session (s), engine (e), apvts (a),
      waveform (s, e),
      list (s, e,
            [this] { return (int) apvts.getRawParameterValue (param::baseNote)->load(); },
            [this] { return apvts.getRawParameterValue (param::keyMode)->load() < 0.5f; }),
      inspector (s)
{
    setWantsKeyboardFocus (true);

    for (auto* b : { &importBtn, &locateBtn, &analyzeBtn, &clearBtn, &playBtn, &stopBtn, &loopBtn,
                     &zoomOut, &zoomIn, &zoomFit, &zoomSel, &trimBtn, &addFragBtn })
        addAndMakeVisible (b);
    addChildComponent (locateBtn);

    importBtn.onClick = [this] { openImportDialog(); };
    locateBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate " + session.getMissingSource().getFileName(),
                                                       session.getMissingSource().getParentDirectory(),
                                                       session.getImporter().getWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              { if (fc.getResult().existsAsFile()) session.relocateMissingSource (fc.getResult()); });
    };
    analyzeBtn.onClick = [this] { session.analyze(); };
    analyzeBtn.setTooltip ("Detect vocal phrases (energy + spectral flux + voicing). Manual fragments are kept.");
    clearBtn.onClick = [this] { session.clearFragments(); };

    sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensitivity.setRange (0.0, 1.0, 0.01);
    sensitivity.setValue (session.getSensitivity(), juce::dontSendNotification);
    sensitivity.setTooltip ("Detection sensitivity: higher finds more, shorter fragments. Press ANALYZE to apply.");
    sensitivity.onValueChange = [this] { session.setSensitivity ((float) sensitivity.getValue()); };
    sensitivity.onDragEnd = [this] { if (session.hasSource() && ! session.isBusy()) session.analyze(); };
    addAndMakeVisible (sensitivity);

    playBtn.onClick = [this] { session.playFrom (waveform.getCursor()); };
    stopBtn.onClick = [this] { session.stop(); };
    loopBtn.setClickingTogglesState (false);
    loopBtn.onClick = [this] { session.setLooping (! session.isLooping()); };
    zoomOut.onClick = [this] { waveform.zoomBy (0.5); };
    zoomIn.onClick  = [this] { waveform.zoomBy (2.0); };
    zoomFit.onClick = [this] { waveform.zoomToFit(); };
    zoomSel.onClick = [this] { waveform.zoomToSelection(); };
    trimBtn.setTooltip ("Crop the source to the selected region (non-destructive: the file on disk is untouched).");
    trimBtn.onClick = [this] { session.trimToSelection(); };
    addFragBtn.setTooltip ("Turn the selected region into a fragment.");
    addFragBtn.onClick = [this] { session.addFragmentFromSelection(); };

    waveform.onFragmentClicked = [this] (int i) { session.selectFragment (i); session.auditionFragment (i); };
    addAndMakeVisible (waveform);
    addAndMakeVisible (list);
    addAndMakeVisible (inspector);

    keyMode.addItemList ({ "Slices", "Chromatic" }, 1);
    trigger.addItemList ({ "One-shot", "Gate" }, 1);
    keyModeAtt = std::make_unique<ComboAttachment> (apvts, param::keyMode, keyMode);
    triggerAtt = std::make_unique<ComboAttachment> (apvts, param::trigger, trigger);
    keyMode.setTooltip ("SLICES: one fragment per key from BASE upward. CHROMATIC: selected fragment played across the keyboard from its root.");
    keyMode.onChange = [this] { list.refresh(); };
    addAndMakeVisible (keyMode);
    addAndMakeVisible (trigger);

    setupKnob (baseNote, "BASE", param::baseNote);
    baseNote.slider.onValueChange = [this] { list.refresh(); };
    setupKnob (attack, "ATTACK", param::attack);
    setupKnob (release, "RELEASE", param::release);
    setupKnob (velocity, "VELOCITY", param::velSens);
    setupKnob (gain, "OUTPUT", param::gain);

    session.addChangeListener (this);
    updateButtons();
    startTimerHz (20);
}

DiscoverPanel::~DiscoverPanel() { session.removeChangeListener (this); }

void DiscoverPanel::setupKnob (Knob& k, const juce::String& name, const char* paramId)
{
    k.name = name;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colours::textDim);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14); // recreate box with these colours
    addAndMakeVisible (k.slider);
    k.att = std::make_unique<SliderAttachment> (apvts, paramId, k.slider);
}

void DiscoverPanel::openImportDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   session.getImporter().getWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) session.importFile (fc.getResult()); });
}

void DiscoverPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateButtons();
    sensitivity.setValue (session.getSensitivity(), juce::dontSendNotification);
    resized(); // LOCATE button may have appeared / disappeared
    repaint();
}

void DiscoverPanel::timerCallback()
{
    playBtn.setToggleState (engine.isPreviewPlaying(), juce::dontSendNotification);
    if (engine.getActiveVoiceCount() > 0 || engine.isPreviewPlaying()) list.repaint();
}

void DiscoverPanel::updateButtons()
{
    const bool src = session.hasSource();
    const bool sel = session.hasSelection();
    locateBtn.setVisible (! src && session.getMissingSource() != juce::File());
    for (auto* b : { &analyzeBtn, &playBtn, &stopBtn, &loopBtn, &zoomOut, &zoomIn, &zoomFit })
        b->setEnabled (src);
    analyzeBtn.setEnabled (src && ! session.isBusy());
    clearBtn.setEnabled (! session.getFragments().empty());
    zoomSel.setEnabled (src && sel);
    trimBtn.setEnabled (src && sel && ! session.isBusy());
    addFragBtn.setEnabled (src && sel);
    loopBtn.setToggleState (session.isLooping(), juce::dontSendNotification);
}

bool DiscoverPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { session.togglePlay (waveform.getCursor()); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { session.deleteFragment (session.getSelectedIndex()); return true; }
    const int sel = session.getSelectedIndex();
    const int n = (int) session.getFragments().size();
    if (k == juce::KeyPress::downKey && n > 0) { const int i = juce::jmin (n - 1, sel + 1); session.selectFragment (i); session.auditionFragment (i); return true; }
    if (k == juce::KeyPress::upKey && n > 0)   { const int i = juce::jmax (0, sel - 1);     session.selectFragment (i); session.auditionFragment (i); return true; }
    return false;
}

void DiscoverPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    g.setColour (colours::textFaint);
    auto sensLabel = sensitivity.getBounds().translated (0, -12).withHeight (12);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("SENSITIVITY", sensLabel, juce::Justification::centredLeft);

    // selection readout in the transport row
    if (auto snap = session.getSnapshot(); snap != nullptr && session.hasSelection())
    {
        const double a = (double) session.getSelectionStart() / snap->sampleRate;
        const double b = (double) session.getSelectionEnd() / snap->sampleRate;
        g.setColour (colours::textDim);
        g.setFont (GhostLookAndFeel::mono (11.0f));
        g.drawText ("SEL " + juce::String (a, 3) + " - " + juce::String (b, 3) + " s  (" + juce::String (b - a, 3) + " s)",
                    transportArea.withTrimmedLeft (transportArea.getWidth() - 300), juce::Justification::centredRight);
    }

    // instrument controls strip
    g.setColour (colours::panel);
    g.fillRect (controlsArea);
    g.setColour (colours::line);
    g.drawHorizontalLine (controlsArea.getY(), 0.0f, (float) getWidth());
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("KEY MODE", keyMode.getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);
    g.drawText ("TRIGGER", trigger.getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);
    for (auto* k : { &baseNote, &attack, &release, &velocity, &gain })
        g.drawText (k->name, k->slider.getBounds().withHeight (12).translated (0, -12), juce::Justification::centred);

    GhostLookAndFeel::drawTracked (g, "MIDI INSTRUMENT", controlsArea.reduced (14, 0).removeFromLeft (140).toFloat().withHeight (20).translated (0, 6),
                                   GhostLookAndFeel::label (9.5f, true), 0.2f, juce::Justification::centredLeft);
}

void DiscoverPanel::resized()
{
    auto r = getLocalBounds();
    const int pad = 12;

    // toolbar
    toolbarArea = r.removeFromTop (44).reduced (pad, 8);
    auto tb = toolbarArea;
    importBtn.setBounds (tb.removeFromLeft (84)); tb.removeFromLeft (6);
    if (locateBtn.isVisible()) { locateBtn.setBounds (tb.removeFromLeft (84)); tb.removeFromLeft (6); }
    else locateBtn.setBounds (tb.getX(), tb.getY(), 84, tb.getHeight());
    tb.removeFromLeft (14);
    analyzeBtn.setBounds (tb.removeFromLeft (84)); tb.removeFromLeft (10);
    sensitivity.setBounds (tb.removeFromLeft (150).withTrimmedTop (10));
    tb.removeFromLeft (10);
    clearBtn.setBounds (tb.removeFromLeft (64));

    // instrument controls at the bottom
    controlsArea = r.removeFromBottom (100);
    {
        auto c = controlsArea.reduced (pad, 10);
        c.removeFromLeft (150);
        auto combos = c.removeFromLeft (130).withTrimmedTop (14);
        keyMode.setBounds (combos.removeFromTop (24));
        combos.removeFromTop (16);
        trigger.setBounds (combos.removeFromTop (24));
        c.removeFromLeft (16);
        for (auto* k : { &baseNote, &attack, &release, &velocity, &gain })
        {
            k->slider.setBounds (c.removeFromLeft (78).withTrimmedTop (12));
            c.removeFromLeft (6);
        }
    }

    // fragment browser + inspector
    auto browser = r.removeFromBottom (juce::jlimit (262, 380, (int) (r.getHeight() * 0.48f)));
    browser = browser.reduced (pad, 0).withTrimmedBottom (pad);
    inspector.setBounds (browser.removeFromRight (juce::jlimit (260, 360, browser.getWidth() / 3)));
    browser.removeFromRight (8);
    list.setBounds (browser);

    // transport row
    transportArea = r.removeFromBottom (40).reduced (pad, 6);
    auto tr = transportArea;
    playBtn.setBounds (tr.removeFromLeft (64)); tr.removeFromLeft (4);
    stopBtn.setBounds (tr.removeFromLeft (64)); tr.removeFromLeft (4);
    loopBtn.setBounds (tr.removeFromLeft (64)); tr.removeFromLeft (16);
    zoomOut.setBounds (tr.removeFromLeft (30)); tr.removeFromLeft (4);
    zoomIn.setBounds (tr.removeFromLeft (30)); tr.removeFromLeft (4);
    zoomFit.setBounds (tr.removeFromLeft (44)); tr.removeFromLeft (4);
    zoomSel.setBounds (tr.removeFromLeft (44)); tr.removeFromLeft (16);
    trimBtn.setBounds (tr.removeFromLeft (64)); tr.removeFromLeft (4);
    addFragBtn.setBounds (tr.removeFromLeft (100));

    waveform.setBounds (r.reduced (pad, 0));
}

} // namespace ghostcut
