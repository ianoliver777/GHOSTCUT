#include "SequencePanel.h"
#include "GhostLookAndFeel.h"

namespace ghostcut
{

SequencePanel::SequencePanel (Session& s, SamplerEngine& e)
    : session (s), engine (e), lanes (s, e, PatternView::Style::Lanes, true)
{
    for (int i = 0; i < Session::numPatternSlots; ++i)
    {
        auto* b = bankButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + i))));
        b->setRadioGroupId (8080);
        b->setClickingTogglesState (true);
        b->setTooltip ("Pattern slot " + b->getButtonText() + ". Each slot is saved with the project.");
        b->onClick = [this, i] { if (! syncing) session.selectPatternSlot (i); };
        addAndMakeVisible (b);
    }

    length.addItem ("16 steps", 16);
    length.addItem ("32 steps", 32);
    length.onChange = [this]
    {
        if (syncing) return;
        auto p = session.getPattern();
        p.length = length.getSelectedId() == 32 ? 32 : 16;
        session.setPattern (p);
    };
    addAndMakeVisible (length);

    swing.setup (*this, "SWING", 0, 0.75, 0.01, 0.0);
    swing.slider.onDragEnd = [this] { auto p = session.getPattern(); p.swing = (float) swing.slider.getValue(); session.setPattern (p); };

    playBtn.onClick = [this] { session.setPatternPlaying (! engine.isSequencerRunning()); };
    clearBtn.onClick = [this]
    {
        auto p = session.getPattern();
        for (auto& a : p.active) a = false;
        session.setPattern (p);
    };
    randomBtn.setTooltip ("Generate a fresh pattern in this slot with the GENERATE settings and a new seed.");
    randomBtn.onClick = [this] { session.generate (true); };
    for (auto* b : { &playBtn, &clearBtn, &randomBtn }) addAndMakeVisible (b);

    lanes.onSelect = [this] (int) { syncStep(); repaint(); };
    addAndMakeVisible (lanes);

    // inspector
    noteDown.onClick = [this] { editStep ([] (StepEvent& ev) { ev.note -= 1; }); };
    noteUp.onClick   = [this] { editStep ([] (StepEvent& ev) { ev.note += 1; }); };
    octDown.onClick  = [this] { editStep ([] (StepEvent& ev) { ev.note -= 12; }); };
    octUp.onClick    = [this] { editStep ([] (StepEvent& ev) { ev.note += 12; }); };
    for (auto* b : { &noteDown, &noteUp, &octDown, &octUp }) addAndMakeVisible (b);

    auto setupSlider = [this] (juce::Slider& sl, double lo, double hi, double step)
    {
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        sl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        sl.setColour (juce::Slider::textBoxTextColourId, colours::textDim);
        sl.setRange (lo, hi, step);
        addAndMakeVisible (sl);
    };
    setupSlider (velocity, 0.05, 1.0, 0.01);
    setupSlider (gate, 0.25, 16.0, 0.25);
    setupSlider (startFrac, 0.0, 0.95, 0.01);
    setupSlider (endFrac, 0.05, 1.0, 0.01);
    velocity.onDragEnd  = [this] { editStep ([v = (float) velocity.getValue()] (StepEvent& ev) { ev.velocity = v; }); };
    gate.onDragEnd      = [this] { editStep ([v = (float) gate.getValue()] (StepEvent& ev) { ev.gateSteps = v; }); };
    startFrac.onDragEnd = [this] { editStep ([v = (float) startFrac.getValue()] (StepEvent& ev) { ev.startFrac = std::min (v, ev.endFrac - 0.05f); }); };
    endFrac.onDragEnd   = [this] { editStep ([v = (float) endFrac.getValue()] (StepEvent& ev) { ev.endFrac = std::max (v, ev.startFrac + 0.05f); }); };

    reverse.onClick = [this] { editStep ([v = reverse.getToggleState()] (StepEvent& ev) { ev.reverse = v; }); };
    choke.onClick   = [this] { editStep ([v = choke.getToggleState()] (StepEvent& ev) { ev.choke = v; }); };
    addAndMakeVisible (reverse);
    addAndMakeVisible (choke);

    ratchet.addItemList ({ "No ratchet", "Ratchet x2", "Ratchet x3", "Ratchet x4" }, 1);
    ratchet.onChange = [this] { editStep ([v = ratchet.getSelectedId()] (StepEvent& ev) { ev.ratchet = juce::jlimit (1, 4, v); }); };
    layer.addItem ("No layer", 1);
    layer.addItem ("+ octave up", 2);
    layer.addItem ("+ fifth up", 3);
    layer.addItem ("+ octave down", 4);
    layer.onChange = [this]
    {
        const int id = layer.getSelectedId();
        editStep ([id] (StepEvent& ev)
        {
            ev.layers.clear();
            if (id == 2) ev.layers.push_back ({ 12, 0.4f, 6.0f });
            if (id == 3) ev.layers.push_back ({ 7, 0.4f, 4.0f });
            if (id == 4) ev.layers.push_back ({ -12, 0.45f, 8.0f });
        });
    };
    addAndMakeVisible (ratchet);
    addAndMakeVisible (layer);
    deleteStep.onClick = [this]
    {
        const int st = lanes.getSelectedStep();
        if (st < 0) return;
        auto p = session.getPattern();
        p.active[(size_t) st] = false;
        session.setPattern (p);
        lanes.setSelectedStep (-1);
    };
    addAndMakeVisible (deleteStep);

    session.addChangeListener (this);
    syncBank();
    syncStep();
    startTimerHz (8);
}

SequencePanel::~SequencePanel() { session.removeChangeListener (this); }

template <typename Fn>
void SequencePanel::editStep (Fn&& fn)
{
    if (syncing) return;
    const int st = lanes.getSelectedStep();
    auto p = session.getPattern();
    if (st < 0 || st >= p.length || ! p.active[(size_t) st]) return;
    fn (p.events[(size_t) st]);
    p.events[(size_t) st].note = juce::jlimit (12, 115, p.events[(size_t) st].note);
    session.setPattern (p);
}

void SequencePanel::syncBank()
{
    const juce::ScopedValueSetter<bool> svs (syncing, true);
    for (int i = 0; i < bankButtons.size(); ++i)
    {
        bankButtons[i]->setToggleState (i == session.getPatternSlot(), juce::dontSendNotification);
        bankButtons[i]->setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)) + (session.isPatternSlotUsed (i) || (i == session.getPatternSlot() && session.hasPattern()) ? "*" : ""));
    }
    length.setSelectedId (session.getPattern().length, juce::dontSendNotification);
    swing.slider.setValue (session.getPattern().swing, juce::dontSendNotification);
}

void SequencePanel::syncStep()
{
    const juce::ScopedValueSetter<bool> svs (syncing, true);
    const int st = lanes.getSelectedStep();
    const auto& p = session.getPattern();
    const bool has = st >= 0 && st < p.length && p.active[(size_t) st];
    for (juce::Component* c : { (juce::Component*) &noteDown, (juce::Component*) &noteUp, (juce::Component*) &octDown,
                                (juce::Component*) &octUp, (juce::Component*) &velocity, (juce::Component*) &gate,
                                (juce::Component*) &startFrac, (juce::Component*) &endFrac, (juce::Component*) &reverse,
                                (juce::Component*) &choke, (juce::Component*) &ratchet, (juce::Component*) &layer,
                                (juce::Component*) &deleteStep })
        c->setEnabled (has);
    if (! has) return;
    const auto& e = p.events[(size_t) st];
    velocity.setValue (e.velocity, juce::dontSendNotification);
    gate.setValue (e.gateSteps, juce::dontSendNotification);
    startFrac.setValue (e.startFrac, juce::dontSendNotification);
    endFrac.setValue (e.endFrac, juce::dontSendNotification);
    reverse.setToggleState (e.reverse, juce::dontSendNotification);
    choke.setToggleState (e.choke, juce::dontSendNotification);
    ratchet.setSelectedId (e.ratchet, juce::dontSendNotification);
    int lid = 1;
    if (! e.layers.empty()) lid = e.layers[0].semitones == 12 ? 2 : e.layers[0].semitones == 7 ? 3 : e.layers[0].semitones == -12 ? 4 : 1;
    layer.setSelectedId (lid, juce::dontSendNotification);
}

void SequencePanel::timerCallback()
{
    playBtn.setToggleState (engine.isSequencerRunning(), juce::dontSendNotification);
    playBtn.setButtonText (engine.isSequencerRunning() ? "Stop" : "Play");
}

void SequencePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("PATTERN", bankButtons[0]->getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);
    g.drawText ("SWING", swing.slider.getBounds().withHeight (12).translated (0, -12), juce::Justification::centred);

    auto r = inspectorArea;
    g.setColour (colours::panel);
    g.fillRect (r);
    r = r.reduced (14, 10);
    g.setColour (colours::textFaint);
    GhostLookAndFeel::drawTracked (g, "STEP", r.removeFromTop (14).toFloat(), GhostLookAndFeel::label (9.5f, true), 0.2f, juce::Justification::centredLeft);
    const int st = lanes.getSelectedStep();
    const auto& p = session.getPattern();
    g.setColour (colours::text);
    juce::String title = "Click a cell to place a chop";
    if (st >= 0 && st < p.length && p.active[(size_t) st])
        title = juce::String (st + 1).paddedLeft ('0', 2) + "   " + noteName (p.events[(size_t) st].note);
    GhostLookAndFeel::drawTracked (g, title, r.removeFromTop (26).toFloat(), GhostLookAndFeel::label (st >= 0 ? 18.0f : 12.0f, st >= 0), 0.06f, juce::Justification::centredLeft);

    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.setColour (colours::textFaint);
    auto lab = [&] (const char* t, juce::Component& c) { g.drawText (t, c.getBounds().translated (0, -13).withHeight (12), juce::Justification::centredLeft); };
    lab ("NOTE", noteDown); lab ("VELOCITY", velocity); lab ("GATE (STEPS)", gate); lab ("CHOP START", startFrac); lab ("CHOP END", endFrac);

    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (10.0f));
    g.drawFittedText ("Click a lane cell to place that fragment. Click a selected hit again to remove it. Drag a hit up/down for velocity.",
                      lanes.getBounds().withTop (lanes.getBottom() + 2).withHeight (16), juce::Justification::centredLeft, 1);
}

void SequencePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (60).withTrimmedTop (16);
    for (auto* b : bankButtons) { b->setBounds (top.removeFromLeft (40).withHeight (28)); top.removeFromLeft (4); }
    top.removeFromLeft (16);
    length.setBounds (top.removeFromLeft (110).withHeight (26));
    top.removeFromLeft (10);
    swing.slider.setBounds (top.removeFromLeft (70).withY (top.getY() - 10).withHeight (56));
    top.removeFromLeft (10);
    playBtn.setBounds (top.removeFromLeft (80).withHeight (28)); top.removeFromLeft (4);
    randomBtn.setBounds (top.removeFromLeft (110).withHeight (28)); top.removeFromLeft (4);
    clearBtn.setBounds (top.removeFromLeft (70).withHeight (28));

    r.removeFromTop (8);
    inspectorArea = r.removeFromRight (300);
    r.removeFromRight (10);
    lanes.setBounds (r.withTrimmedBottom (20));

    auto in = inspectorArea.reduced (14, 10);
    in.removeFromTop (14 + 26 + 18);
    auto row = in.removeFromTop (26);
    const int bw = (row.getWidth() - 12) / 4;
    for (auto* b : { &octDown, &noteDown, &noteUp, &octUp }) { b->setBounds (row.removeFromLeft (bw)); row.removeFromLeft (4); }
    for (auto* sl : { &velocity, &gate, &startFrac, &endFrac })
    {
        in.removeFromTop (18);
        sl->setBounds (in.removeFromTop (22));
    }
    in.removeFromTop (10);
    reverse.setBounds (in.removeFromTop (24));
    choke.setBounds (in.removeFromTop (24));
    in.removeFromTop (6);
    ratchet.setBounds (in.removeFromTop (24));
    in.removeFromTop (6);
    layer.setBounds (in.removeFromTop (24));
    in.removeFromTop (10);
    deleteStep.setBounds (in.removeFromTop (28));
}

} // namespace ghostcut
