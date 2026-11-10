#include "GeneratePanel.h"
#include "GhostLookAndFeel.h"
#include "FragmentBrowser.h"
#include "../PluginProcessor.h"

namespace ghostcut
{

static const char* modeBlurb (int m)
{
    static const char* b[] = {
        "sparse, slowed, overlapping phrases with octave and fifth shimmer",
        "off-grid, reversed, slowed and darkened, wrong notes, ghosts an octave down",
        "swung pocket, short chops, repetition, slight detune",
        "long notes stretched to fill, stacked harmonies, slow movement",
        "dense stutters, ratchets, pitch jumps, random starts, wild formants",
    };
    return b[juce::jlimit (0, 4, m)];
}

void LabelledKnob::setup (juce::Component& parent, const juce::String& label, double lo, double hi, double step, double value)
{
    name = label;
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, colours::textDim);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
    slider.setRange (lo, hi, step);
    slider.setValue (value, juce::dontSendNotification);
    parent.addAndMakeVisible (slider);
}

//==============================================================================
FileDragButton::FileDragButton (juce::String t, std::function<juce::File (juce::String&)> m) : text (std::move (t)), make (std::move (m))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void FileDragButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isEnabled() ? colours::panelHi : colours::panel);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    g.setColour (isEnabled() ? colours::text : colours::textFaint);
    GhostLookAndFeel::drawTracked (g, text, r, GhostLookAndFeel::label (10.5f, true), 0.12f, juce::Justification::centred);
}

void FileDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (started || e.getDistanceFromDragStart() < 6 || ! isEnabled()) return;
    started = true;
    juce::String err;
    auto f = make (err);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

//==============================================================================
GeneratePanel::GeneratePanel (Session& s, SamplerEngine& e, juce::AudioProcessorValueTreeState& a)
    : session (s), engine (e), apvts (a),
      dragWav ("DRAG LOOP TO DAW", [this] (juce::String& err) { return session.renderLoopForDrag (err); }),
      dragMid ("DRAG MIDI TO DAW", [this] (juce::String& err) { return session.renderMidiForDrag (err); }),
      roll (s, e, PatternView::Style::Roll, false)
{
    for (int m = 0; m < (int) GenMode::numModes; ++m)
    {
        auto* b = modeButtons.add (new juce::TextButton (genModeName ((GenMode) m)));
        b->setRadioGroupId (4242);
        b->setClickingTogglesState (true);
        b->setTooltip (modeBlurb (m));
        b->onClick = [this, m] { if (! syncing) { auto g = session.getGenSettings(); g.mode = (GenMode) m; session.setGenSettings (g); } };
        addAndMakeVisible (b);
    }

    key.addItemList (ScaleQuantizer::keyNames(), 1);
    scale.addItemList (ScaleQuantizer::scaleNames(), 1);
    for (int n = 36; n <= 72; ++n) lowNote.addItem ("from " + noteName (n), n);
    octaves.addItemList ({ "1 octave", "2 octaves", "3 octaves" }, 1);
    steps.addItem ("16 steps", 16); steps.addItem ("32 steps", 32);
    rate.addItem ("1/8", 2); rate.addItem ("1/16", 4); rate.addItem ("1/32", 8);
    for (auto* c : { &key, &scale, &lowNote, &octaves, &steps, &rate })
    {
        c->onChange = [this] { pushSettings(); };
        addAndMakeVisible (c);
    }

    density.setup (*this, "DENSITY", 0, 1, 0.01, 0.5);
    complexity.setup (*this, "COMPLEXITY", 0, 1, 0.01, 0.5);
    randomness.setup (*this, "RANDOMNESS", 0, 1, 0.01, 0.3);
    swing.setup (*this, "SWING", 0, 0.75, 0.01, 0.0);
    for (auto* k : { &density, &complexity, &randomness, &swing })
        k->slider.onDragEnd = [this] { pushSettings(); };

    seed.setInputRestrictions (9, "0123456789");
    seed.setJustification (juce::Justification::centred);
    seed.setFont (GhostLookAndFeel::mono (14.0f));
    seed.setColour (juce::TextEditor::backgroundColourId, colours::panelHi);
    seed.setColour (juce::TextEditor::outlineColourId, colours::line);
    seed.setColour (juce::TextEditor::textColourId, colours::accent);
    seed.setTooltip ("Same seed + same settings + same fragments = the same pattern, every time.");
    seed.onReturnKey = [this] { pushSettings(); session.generate (false); };
    seed.onFocusLost = [this] { pushSettings(); };
    addAndMakeVisible (seed);

    dice.onClick = [this] { pushSettings(); session.generate (true); };
    generateBtn.onClick = [this] { pushSettings(); session.generate (true); };
    generateBtn.setTooltip ("New pattern with a fresh seed.");
    rebuildBtn.onClick = [this] { pushSettings(); session.generate (false); };
    rebuildBtn.setTooltip ("Rebuild with the current seed (e.g. after changing key or density).");
    playBtn.onClick = [this] { session.setPatternPlaying (! engine.isSequencerRunning()); };
    playBtn.setTooltip ("Preview without the DAW playing. In a DAW the pattern also follows the host transport.");
    exportWav.onClick = [this] { exportWith (false); };
    exportMid.onClick = [this] { exportWith (true); };
    for (juce::Component* c : { (juce::Component*) &dice, (juce::Component*) &generateBtn, (juce::Component*) &rebuildBtn,
                                (juce::Component*) &playBtn, (juce::Component*) &exportWav, (juce::Component*) &exportMid,
                                (juce::Component*) &dragWav, (juce::Component*) &dragMid, (juce::Component*) &roll })
        addAndMakeVisible (c);

    syncMode.addItemList ({ "Sync: off", "Sync: host transport" }, 1);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, param::seqMode, syncMode);
    addAndMakeVisible (syncMode);
    for (auto* sl : { &tempo, &patLevel })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        sl->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        sl->setColour (juce::Slider::textBoxTextColourId, colours::textDim);
        addAndMakeVisible (sl);
    }
    tempoAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, param::tempo, tempo);
    levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, param::patLevel, patLevel);
    tempo.setTooltip ("Used when the host has no tempo (or in the standalone app).");

    session.addChangeListener (this);
    syncFromSession();
    startTimerHz (8);
}

GeneratePanel::~GeneratePanel() { session.removeChangeListener (this); }

void GeneratePanel::syncFromSession()
{
    const juce::ScopedValueSetter<bool> svs (syncing, true);
    const auto& g = session.getGenSettings();
    for (int m = 0; m < modeButtons.size(); ++m) modeButtons[m]->setToggleState ((int) g.mode == m, juce::dontSendNotification);
    key.setSelectedId (g.key + 1, juce::dontSendNotification);
    scale.setSelectedId ((int) g.scale + 1, juce::dontSendNotification);
    lowNote.setSelectedId (g.rootOctaveNote, juce::dontSendNotification);
    octaves.setSelectedId (g.octaveRange, juce::dontSendNotification);
    steps.setSelectedId (g.steps, juce::dontSendNotification);
    rate.setSelectedId (g.stepDiv, juce::dontSendNotification);
    density.slider.setValue (g.density, juce::dontSendNotification);
    complexity.slider.setValue (g.complexity, juce::dontSendNotification);
    randomness.slider.setValue (g.randomness, juce::dontSendNotification);
    swing.slider.setValue (g.swing, juce::dontSendNotification);
    if (! seed.hasKeyboardFocus (true)) seed.setText (juce::String ((juce::int64) g.seed), false);
    const bool can = session.hasSource() && ! session.getFragments().empty();
    generateBtn.setEnabled (can); rebuildBtn.setEnabled (can); dice.setEnabled (can);
    for (juce::Component* c : { (juce::Component*) &exportWav, (juce::Component*) &exportMid, (juce::Component*) &dragWav,
                                (juce::Component*) &dragMid, (juce::Component*) &playBtn })
        c->setEnabled (session.hasPattern());
}

void GeneratePanel::pushSettings()
{
    if (syncing) return;
    auto g = session.getGenSettings();
    g.key = std::max (0, key.getSelectedId() - 1);
    g.scale = (ScaleQuantizer::Scale) std::max (0, scale.getSelectedId() - 1);
    g.rootOctaveNote = lowNote.getSelectedId() > 0 ? lowNote.getSelectedId() : 57;
    g.octaveRange = juce::jlimit (1, 3, octaves.getSelectedId());
    g.steps = steps.getSelectedId() == 32 ? 32 : 16;
    g.stepDiv = rate.getSelectedId() > 0 ? rate.getSelectedId() : 4;
    g.density = (float) density.slider.getValue();
    g.complexity = (float) complexity.slider.getValue();
    g.randomness = (float) randomness.slider.getValue();
    g.swing = (float) swing.slider.getValue();
    g.seed = (uint32_t) seed.getText().getLargeIntValue();
    session.setGenSettings (g);
}

void GeneratePanel::timerCallback()
{
    playBtn.setToggleState (engine.isSequencerRunning(), juce::dontSendNotification);
    playBtn.setButtonText (engine.isSequencerRunning() ? "Stop" : "Play");
    repaint (outArea);
}

void GeneratePanel::exportWith (bool midi)
{
    const auto base = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> (midi ? "Export MIDI pattern" : "Export loop",
                                                   base.getChildFile (midi ? "GHOSTCUT_pattern.mid" : "GHOSTCUT_loop.wav"),
                                                   midi ? "*.mid" : "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, midi] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              const auto err = midi ? session.exportMidi (f.withFileExtension ("mid"))
                                                    : session.exportLoop (f.withFileExtension ("wav"), 1);
                              if (err.isNotEmpty())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", err);
                          });
}

void GeneratePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto heading = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (colours::textFaint);
        GhostLookAndFeel::drawTracked (g, t, r.removeFromTop (14).toFloat(), GhostLookAndFeel::label (9.5f, true), 0.2f, juce::Justification::centredLeft);
    };

    heading (modeArea, "MODE");
    // mode blurb under the buttons
    g.setColour (colours::textDim);
    g.setFont (GhostLookAndFeel::label (11.0f));
    g.drawFittedText (modeBlurb ((int) session.getGenSettings().mode),
                      modeArea.withTop (modeButtons.getLast()->getBottom() + 6).withHeight (32), juce::Justification::topLeft, 2);

    heading (slotsArea, session.numPinned() > 0 ? "CHOPS: PINNED IN DISCOVER" : "CHOPS: TOP 6 FOR THIS MODE");
    {
        auto r = slotsArea.withTrimmedTop (18);
        const auto slots = session.currentGenSlots();
        g.setFont (GhostLookAndFeel::mono (10.5f));
        int i = 0;
        for (auto& sl : slots)
        {
            auto row = r.removeFromTop (17);
            int fi = -1;
            for (int k = 0; k < (int) session.getFragments().size(); ++k) if (session.getFragments()[(size_t) k].uid == sl.uid) fi = k;
            g.setColour (PatternView::slotColour (i++));
            g.fillRect (row.removeFromLeft (3).reduced (0, 3));
            row.removeFromLeft (6);
            g.setColour (colours::text);
            g.drawText ("FRAG " + juce::String (fi + 1).paddedLeft ('0', 2) + "  " + noteName (sl.root) + "  " + juce::String (sl.durationSec, 2) + "s",
                        row.removeFromLeft (150), juce::Justification::centredLeft);
            drawScoreBar (g, row.toFloat().withSizeKeepingCentre ((float) row.getWidth(), 3.0f), sl.modeScore, colours::fragment.brighter (0.4f));
        }
        if (slots.empty())
        {
            g.setColour (colours::textFaint);
            g.drawText ("Discover fragments first.", r.removeFromTop (17), juce::Justification::centredLeft);
        }
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (10.0f));
        g.drawFittedText ("Pin specific fragments with PIN in the DISCOVER inspector to choose them yourself.",
                          r.removeFromTop (34).withTrimmedTop (6), juce::Justification::topLeft, 2);
    }

    heading (musicArea, "MUSIC");
    for (auto* k : { &density, &complexity, &randomness, &swing })
    {
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (9.5f, true));
        g.drawText (k->name, k->slider.getBounds().withHeight (12).translated (0, -12), juce::Justification::centred);
    }
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("SEED", seed.getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);

    heading (outArea, "PLAYBACK + EXPORT");
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.setColour (colours::textFaint);
    g.drawText ("FALLBACK TEMPO", tempo.getBounds().translated (0, -13).withHeight (12), juce::Justification::centredLeft);
    g.drawText ("PATTERN LEVEL", patLevel.getBounds().translated (0, -13).withHeight (12), juce::Justification::centredLeft);
    g.setFont (GhostLookAndFeel::mono (11.0f));
    g.setColour (colours::text);
    juce::String info = "TEMPO " + juce::String (session.getBpm(), 1) + " BPM";
    if (session.hasPattern())
        info += "   " + juce::String (session.getPattern().numEvents()) + " HITS   SEED " + juce::String ((juce::int64) session.getPattern().madeWith.seed);
    if (session.isRenderingPattern()) info += "   rendering...";
    g.drawText (info, outArea.withTrimmedTop (16).removeFromTop (16), juce::Justification::centredLeft);
}

void GeneratePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto rollArea = r.removeFromBottom (std::max (200, r.getHeight() - 360)); // controls need ~360 px
    roll.setBounds (rollArea);
    r.removeFromBottom (10);

    const int colW = (r.getWidth() - 32) / 3;
    modeArea = r.removeFromLeft (colW);
    r.removeFromLeft (16);
    musicArea = r.removeFromLeft (colW);
    r.removeFromLeft (16);
    outArea = r;

    {
        auto m = modeArea.withTrimmedTop (18);
        auto row = m.removeFromTop (30);
        const int bw = (row.getWidth() - 4 * 4) / 5;
        for (auto* b : modeButtons) { b->setBounds (row.removeFromLeft (bw)); row.removeFromLeft (4); }
        slotsArea = m.withTrimmedTop (48);
    }
    {
        auto m = musicArea.withTrimmedTop (18);
        auto row = m.removeFromTop (24);
        const int w = (row.getWidth() - 8) / 3;
        key.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        scale.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        lowNote.setBounds (row);
        m.removeFromTop (6);
        row = m.removeFromTop (24);
        octaves.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        steps.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
        rate.setBounds (row);
        m.removeFromTop (18);
        auto knobs = m.removeFromTop (78);
        const int kw = knobs.getWidth() / 4;
        for (auto* k : { &density, &complexity, &randomness, &swing }) k->slider.setBounds (knobs.removeFromLeft (kw).reduced (4, 0));
        m.removeFromTop (20);
        row = m.removeFromTop (28);
        seed.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6);
        dice.setBounds (row.removeFromLeft (96));
    }
    {
        auto o = outArea.withTrimmedTop (40);
        auto row = o.removeFromTop (34);
        generateBtn.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6);
        rebuildBtn.setBounds (row);
        o.removeFromTop (6);
        row = o.removeFromTop (28);
        playBtn.setBounds (row.removeFromLeft (90)); row.removeFromLeft (6);
        syncMode.setBounds (row);
        o.removeFromTop (18);
        tempo.setBounds (o.removeFromTop (22));
        o.removeFromTop (16);
        patLevel.setBounds (o.removeFromTop (22));
        o.removeFromTop (8);
        row = o.removeFromTop (26);
        exportWav.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6);
        exportMid.setBounds (row);
        o.removeFromTop (6);
        row = o.removeFromTop (26);
        dragWav.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6);
        dragMid.setBounds (row);
    }
}

} // namespace ghostcut
