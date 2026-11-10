#include "Pattern.h"

namespace ghostcut
{

juce::ValueTree GenSettings::toTree() const
{
    juce::ValueTree t ("GEN");
    t.setProperty ("mode", (int) mode, nullptr);
    t.setProperty ("key", key, nullptr);
    t.setProperty ("scale", (int) scale, nullptr);
    t.setProperty ("rootNote", rootOctaveNote, nullptr);
    t.setProperty ("octaves", octaveRange, nullptr);
    t.setProperty ("steps", steps, nullptr);
    t.setProperty ("div", stepDiv, nullptr);
    t.setProperty ("density", density, nullptr);
    t.setProperty ("complexity", complexity, nullptr);
    t.setProperty ("randomness", randomness, nullptr);
    t.setProperty ("swing", swing, nullptr);
    t.setProperty ("seed", (juce::int64) seed, nullptr);
    return t;
}

GenSettings GenSettings::fromTree (const juce::ValueTree& t)
{
    GenSettings s;
    if (! t.isValid()) return s;
    s.mode = (GenMode) juce::jlimit (0, 4, (int) t.getProperty ("mode", 0));
    s.key = juce::jlimit (0, 11, (int) t.getProperty ("key", 9));
    s.scale = (ScaleQuantizer::Scale) juce::jlimit (0, (int) ScaleQuantizer::Scale::numScales - 1, (int) t.getProperty ("scale", 0));
    s.rootOctaveNote = juce::jlimit (24, 96, (int) t.getProperty ("rootNote", 57));
    s.octaveRange = juce::jlimit (1, 3, (int) t.getProperty ("octaves", 2));
    s.steps = (int) t.getProperty ("steps", 16) == 32 ? 32 : 16;
    s.stepDiv = juce::jlimit (1, 8, (int) t.getProperty ("div", 4));
    s.density = (float) t.getProperty ("density", 0.5f);
    s.complexity = (float) t.getProperty ("complexity", 0.5f);
    s.randomness = (float) t.getProperty ("randomness", 0.3f);
    s.swing = (float) t.getProperty ("swing", 0.0f);
    s.seed = (uint32_t) (juce::int64) t.getProperty ("seed", 777);
    return s;
}

juce::ValueTree Pattern::toTree() const
{
    juce::ValueTree t ("PATTERN");
    t.setProperty ("length", length, nullptr);
    t.setProperty ("div", stepDiv, nullptr);
    t.setProperty ("swing", swing, nullptr);
    juce::StringArray uids, roots;
    for (auto u : slotUids) uids.add (juce::String (u));
    for (auto r : slotRoots) roots.add (juce::String (r));
    t.setProperty ("slots", uids.joinIntoString (","), nullptr);
    t.setProperty ("roots", roots.joinIntoString (","), nullptr);
    t.appendChild (madeWith.toTree(), nullptr);
    for (int i = 0; i < length; ++i)
    {
        if (! active[(size_t) i]) continue;
        const auto& e = events[(size_t) i];
        juce::ValueTree s ("STEP");
        s.setProperty ("i", i, nullptr);
        s.setProperty ("slot", e.slot, nullptr);
        s.setProperty ("note", e.note, nullptr);
        s.setProperty ("vel", e.velocity, nullptr);
        s.setProperty ("gate", e.gateSteps, nullptr);
        s.setProperty ("rev", e.reverse, nullptr);
        s.setProperty ("s0", e.startFrac, nullptr);
        s.setProperty ("s1", e.endFrac, nullptr);
        s.setProperty ("stretch", e.stretch, nullptr);
        s.setProperty ("fit", e.fitToGate, nullptr);
        s.setProperty ("ratchet", e.ratchet, nullptr);
        s.setProperty ("fin", e.fadeInMs, nullptr);
        s.setProperty ("fout", e.fadeOutMs, nullptr);
        s.setProperty ("formant", e.formantSemis, nullptr);
        s.setProperty ("choke", e.choke, nullptr);
        juce::StringArray ls;
        for (auto& l : e.layers) ls.add (juce::String (l.semitones) + ":" + juce::String (l.gain, 3) + ":" + juce::String (l.detuneCents, 1));
        s.setProperty ("layers", ls.joinIntoString (";"), nullptr);
        t.appendChild (s, nullptr);
    }
    return t;
}

Pattern Pattern::fromTree (const juce::ValueTree& t)
{
    Pattern p;
    if (! t.isValid()) return p;
    p.length = (int) t.getProperty ("length", 16) == 32 ? 32 : 16;
    p.stepDiv = juce::jlimit (1, 8, (int) t.getProperty ("div", 4));
    p.swing = (float) t.getProperty ("swing", 0.0f);
    juce::StringArray uids, roots;
    uids.addTokens (t.getProperty ("slots").toString(), ",", "");
    roots.addTokens (t.getProperty ("roots").toString(), ",", "");
    uids.removeEmptyStrings(); roots.removeEmptyStrings();
    for (auto& u : uids) p.slotUids.push_back (u.getLargeIntValue());
    for (auto& r : roots) p.slotRoots.push_back (r.getIntValue());
    p.slotRoots.resize (p.slotUids.size(), 60);
    p.madeWith = GenSettings::fromTree (t.getChildWithName ("GEN"));
    for (auto s : t)
    {
        if (! s.hasType ("STEP")) continue;
        const int i = (int) s.getProperty ("i", -1);
        if (i < 0 || i >= p.length) continue;
        auto& e = p.events[(size_t) i];
        e.slot = juce::jlimit (0, std::max (0, (int) p.slotUids.size() - 1), (int) s.getProperty ("slot", 0));
        e.note = (int) s.getProperty ("note", 60);
        e.velocity = (float) s.getProperty ("vel", 0.8f);
        e.gateSteps = (float) s.getProperty ("gate", 1.0f);
        e.reverse = (bool) s.getProperty ("rev", false);
        e.startFrac = (float) s.getProperty ("s0", 0.0f);
        e.endFrac = (float) s.getProperty ("s1", 1.0f);
        e.stretch = (float) s.getProperty ("stretch", 1.0f);
        e.fitToGate = (bool) s.getProperty ("fit", false);
        e.ratchet = juce::jlimit (1, 4, (int) s.getProperty ("ratchet", 1));
        e.fadeInMs = (float) s.getProperty ("fin", 3.0f);
        e.fadeOutMs = (float) s.getProperty ("fout", 10.0f);
        e.formantSemis = (float) s.getProperty ("formant", 0.0f);
        e.choke = (bool) s.getProperty ("choke", true);
        juce::StringArray ls;
        ls.addTokens (s.getProperty ("layers").toString(), ";", "");
        ls.removeEmptyStrings();
        for (auto& l : ls)
        {
            juce::StringArray parts;
            parts.addTokens (l, ":", "");
            if (parts.size() == 3) e.layers.push_back ({ parts[0].getIntValue(), parts[1].getFloatValue(), parts[2].getFloatValue() });
        }
        p.active[(size_t) i] = true;
    }
    return p;
}

} // namespace ghostcut
