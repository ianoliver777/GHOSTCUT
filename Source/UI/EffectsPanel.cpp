#include "EffectsPanel.h"
#include "GhostLookAndFeel.h"
#include "../FxParameters.h"

namespace ghostcut
{

EffectsPanel::EffectsPanel (Session& s, juce::AudioProcessorValueTreeState& a) : session (s), apvts (a)
{
    using namespace fxp;
    {
        auto& sec = addSection ("TEXTURE", texOn);
        addKnob (sec, texDrive, "SATURATE"); addKnob (sec, texTilt, "TILT"); addKnob (sec, texHp, "LOW CUT"); addKnob (sec, texLp, "HIGH CUT");
        addKnob (sec, texChorus, "CHORUS"); addKnob (sec, texWow, "WOW"); addKnob (sec, texFlutter, "FLUTTER"); addKnob (sec, texNoise, "NOISE");
    }
    {
        auto& sec = addSection ("LO-FI", lofiOn);
        addKnob (sec, lofiRate, "RATE"); addKnob (sec, lofiBits, "BITS"); addKnob (sec, lofiBand, "BANDWIDTH");
        addKnob (sec, lofiDist, "DRIVE"); addKnob (sec, lofiMix, "MIX");
    }
    {
        auto& sec = addSection ("DELAY", dlyOn, "tempo-synced to the host");
        addCombo (sec, dlyDiv, "TIME"); addToggle (sec, dlyPing, "Ping-pong");
        addKnob (sec, dlyFb, "FEEDBACK"); addKnob (sec, dlyLow, "LOW CUT"); addKnob (sec, dlyHigh, "HIGH CUT"); addKnob (sec, dlyMix, "MIX");
    }
    {
        auto& sec = addSection ("REVERB", revOn);
        addKnob (sec, revSize, "SIZE"); addKnob (sec, revDecay, "DECAY"); addKnob (sec, revPre, "PRE-DELAY");
        addKnob (sec, revDamp, "DAMPING"); addKnob (sec, revWidth, "WIDTH"); addKnob (sec, revMix, "MIX");
    }
    {
        auto& sec = addSection ("PITCH / FORMANT", nullptr, "applied to pattern chops (offline quality); pitch also transposes keys");
        addKnob (sec, pitchSemi, "SEMITONES"); addKnob (sec, pitchFine, "FINE"); addKnob (sec, formant, "FORMANT");
        addCombo (sec, harmony, "HARMONY");
    }
    {
        auto& sec = addSection ("REVERSE", nullptr, "applied to pattern chops");
        addToggle (sec, revAll, "Reverse every chop");
        addKnob (sec, revXfade, "TRANSITION");
    }

    reversePattern.setTooltip ("Plays the current pattern backwards: step order mirrored and every chop flipped.");
    reversePattern.onClick = [this]
    {
        auto p = session.getPattern();
        Pattern r = p;
        for (int i = 0; i < p.length; ++i)
        {
            const int j = p.length - 1 - i;
            r.active[(size_t) j] = p.active[(size_t) i];
            r.events[(size_t) j] = p.events[(size_t) i];
            r.events[(size_t) j].reverse = ! p.events[(size_t) i].reverse;
        }
        session.setPattern (r);
    };
    addAndMakeVisible (reversePattern);

    for (int m = 0; m < (int) GenMode::numModes; ++m)
    {
        auto* b = modeFx.add (new juce::TextButton (juce::String (genModeName ((GenMode) m)) + " FX"));
        b->setTooltip ("Load the effect chain designed for " + juce::String (genModeName ((GenMode) m)));
        b->onClick = [this, m] { fxp::applyModePreset (apvts, (GenMode) m); };
        addAndMakeVisible (b);
    }
    followAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, fxFollow, follow);
    follow.setTooltip ("When on, pressing GENERATE also loads that mode's effects.");
    addAndMakeVisible (follow);
}

EffectsPanel::~EffectsPanel() = default;

EffectsPanel::Section& EffectsPanel::addSection (const juce::String& title, const char* onParam, const juce::String& note)
{
    auto sec = std::make_unique<Section>();
    sec->title = title;
    sec->note = note;
    if (onParam != nullptr)
    {
        sec->onButton = std::make_unique<juce::ToggleButton> ("ON");
        addAndMakeVisible (*sec->onButton);
        sec->onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, onParam, *sec->onButton);
    }
    sections.push_back (std::move (sec));
    return *sections.back();
}

void EffectsPanel::addKnob (Section& sec, const char* param, const juce::String& label)
{
    Section::Knob k;
    k.s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k.s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.s->setColour (juce::Slider::textBoxTextColourId, colours::textDim);
    k.s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 13);
    addAndMakeVisible (*k.s);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, param, *k.s);
    k.label = label;
    sec.knobs.push_back (std::move (k));
}

void EffectsPanel::addCombo (Section& sec, const char* param, const juce::String& label)
{
    Section::Combo c;
    c.c = std::make_unique<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (param)))
        c.c->addItemList (p->choices, 1);
    addAndMakeVisible (*c.c);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, param, *c.c);
    c.label = label;
    sec.combos.push_back (std::move (c));
}

void EffectsPanel::addToggle (Section& sec, const char* param, const juce::String& label)
{
    Section::Toggle t;
    t.b = std::make_unique<juce::ToggleButton> (label);
    addAndMakeVisible (*t.b);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, param, *t.b);
    sec.toggles.push_back (std::move (t));
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    for (auto& sp : sections)
    {
        auto& s = *sp;
        g.setColour (colours::panel);
        g.fillRect (s.area);
        auto r = s.area.reduced (12, 8);
        auto head = r.removeFromTop (18);
        g.setColour (colours::text);
        GhostLookAndFeel::drawTracked (g, s.title, head.toFloat(), GhostLookAndFeel::label (11.0f, true), 0.25f, juce::Justification::centredLeft);
        if (s.note.isNotEmpty())
        {
            g.setColour (colours::textFaint);
            g.setFont (GhostLookAndFeel::label (9.5f));
            g.drawText (s.note, r.removeFromTop (14), juce::Justification::centredLeft, true);
        }
        g.setColour (colours::textFaint);
        g.setFont (GhostLookAndFeel::label (9.0f, true));
        for (auto& k : s.knobs)
            g.drawText (k.label, k.s->getBounds().withHeight (11).translated (0, -11), juce::Justification::centred);
        for (auto& c : s.combos)
            g.drawText (c.label, c.c->getBounds().withHeight (11).translated (0, -12), juce::Justification::centredLeft);
    }
    g.setColour (colours::textFaint);
    g.setFont (GhostLookAndFeel::label (9.5f, true));
    g.drawText ("MODE FX PRESETS", modeFx[0]->getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (48).withTrimmedTop (16);
    for (auto* b : modeFx) { b->setBounds (top.removeFromLeft (128).withHeight (28)); top.removeFromLeft (4); }
    top.removeFromLeft (12);
    follow.setBounds (top.removeFromLeft (260).withHeight (28));
    reversePattern.setBounds (top.removeFromRight (170).withHeight (28));
    r.removeFromTop (8);

    // 3 x 2 grid; TEXTURE gets a double-width cell for its 8 knobs
    const int gap = 10;
    auto row1 = r.removeFromTop (std::min (180, (r.getHeight() - gap) / 2));
    r.removeFromTop (gap);
    auto row2 = r;
    const int colW = (row1.getWidth() - 2 * gap) / 3;
    std::array<juce::Rectangle<int>, 6> cells;
    cells[0] = row1.removeFromLeft (colW * 2 + gap); row1.removeFromLeft (gap);   // TEXTURE
    cells[1] = row1;                                                             // LO-FI
    cells[2] = row2.removeFromLeft (colW); row2.removeFromLeft (gap);           // DELAY
    cells[3] = row2.removeFromLeft (colW); row2.removeFromLeft (gap);           // REVERB
    auto last = row2;
    cells[4] = last.removeFromTop ((last.getHeight() - gap) * 11 / 20); last.removeFromTop (gap); // PITCH
    cells[5] = last;                                                             // REVERSE

    for (size_t i = 0; i < sections.size(); ++i)
    {
        auto& s = *sections[i];
        s.area = cells[i];
        auto in = s.area.reduced (12, 8);
        auto head = in.removeFromTop (18);
        if (s.onButton) s.onButton->setBounds (head.removeFromRight (56));
        if (s.note.isNotEmpty()) in.removeFromTop (14);
        in.removeFromTop (6);

        if (! s.combos.empty() || ! s.toggles.empty())
        {
            auto row = in.removeFromTop (s.combos.empty() ? 24 : 38);
            for (auto& c : s.combos) { c.c->setBounds (row.removeFromLeft (130).withTrimmedTop (s.combos.empty() ? 0 : 14)); row.removeFromLeft (8); }
            for (auto& t : s.toggles) { t.b->setBounds (row.removeFromLeft (170).withTrimmedTop (s.combos.empty() ? 0 : 14)); row.removeFromLeft (8); }
            in.removeFromTop (6);
        }
        if (s.knobs.empty()) continue;
        const int perRow = std::max (1, std::min ((int) s.knobs.size(), in.getWidth() / 72));
        const int rows = ((int) s.knobs.size() + perRow - 1) / perRow;
        const int kh = std::min (96, in.getHeight() / std::max (1, rows));
        const int kw = std::min (90, in.getWidth() / perRow);
        for (int k = 0; k < (int) s.knobs.size(); ++k)
        {
            const int rr = k / perRow, cc = k % perRow;
            s.knobs[(size_t) k].s->setBounds (in.getX() + cc * kw, in.getY() + rr * kh + 11, kw, kh - 11);
        }
    }
}

} // namespace ghostcut
