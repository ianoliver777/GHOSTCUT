#include "SearchQuery.h"

namespace ghostcut
{

using F = SearchQuery::Feature;

float SearchQuery::featureValue (const Fragment& f, Feature feat)
{
    const auto& x = f.features;
    switch (feat)
    {
        case F::Note:        return f.detectedNote >= 0 ? (float) f.detectedNote : std::numeric_limits<float>::quiet_NaN();
        case F::Duration:    return x.durationSec;
        case F::Sustain:     return x.sustainSec;
        case F::Breathiness: return x.breathiness;
        case F::Airiness:    return x.airiness;
        case F::Attack:      return x.attackSec;
        case F::Decay:       return x.decaySec;
        case F::Slope:       return x.pitchSlope;
        case F::Bend:        return x.bendSemis;
        case F::Range:       return x.pitchRangeSemis;
        case F::Vibrato:     return x.vibratoRateHz > 0 ? x.vibratoDepthSemis : 0.0f;
        case F::Centroid:    return x.centroidHz;
        case F::Level:       return x.meanDb;
        case F::Stability:   return x.pitchStabilitySemis;
        case F::Harmonicity: return x.harmonicity;
        case F::Jumps:       return x.jumpsPerSec;
        case F::Voiced:      return x.voicedRatio;
        case F::ScoreEthereal:     return f.scores[FragmentScores::Ethereal];
        case F::ScoreHaunted:      return f.scores[FragmentScores::Haunted];
        case F::ScoreNostalgic:    return f.scores[FragmentScores::Nostalgic];
        case F::ScoreHeavenly:     return f.scores[FragmentScores::Heavenly];
        case F::ScoreExperimental: return f.scores[FragmentScores::Experimental];
    }
    return 0.0f;
}

float SearchQuery::match (const Fragment& f) const
{
    if (constraints.empty()) return 1.0f;
    float s = 0.0f, w = 0.0f;
    for (auto& c : constraints)
    {
        const float v = featureValue (f, c.feature);
        float sat = 0.0f; // unknown (e.g. no pitch) counts as not matching
        if (! std::isnan (v))
            sat = c.band ? juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (v - 0.5f * (c.lo + c.hi)) / (c.hi - c.lo))
                         : juce::jlimit (0.0f, 1.0f, (v - c.lo) / (c.hi - c.lo));
        s += sat * c.weight;
        w += c.weight;
    }
    return w > 0 ? s / w : 0.0f;
}

namespace
{
    /** Parses a note name in Ableton convention (C3 = MIDI 60), e.g. "a4", "f#3", "eb2". */
    int parseNote (const juce::String& t)
    {
        if (t.length() < 2 || t.length() > 4) return -1;
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        const juce::juce_wchar c = t[0];
        if (c < 'a' || c > 'g') return -1;
        int i = 1, semi = base[c - 'a'];
        if (t[i] == '#') { ++semi; ++i; }
        else if (t[i] == 'b' && t.length() > 2) { --semi; ++i; }
        const auto rest = t.substring (i);
        if (rest.isEmpty() || rest.retainCharacters ("-0123456789") != rest) return -1;
        const int oct = rest.getIntValue();
        const int midi = (oct + 2) * 12 + semi;
        return (midi >= 0 && midi <= 127) ? midi : -1;
    }

    /** "2s", "2", "500ms", "1.5sec" -> seconds; NaN if not a duration. */
    float parseSeconds (const juce::String& t, const juce::String& next, bool& usedNext)
    {
        usedNext = false;
        auto num = t.initialSectionContainingOnly ("0123456789.");
        if (num.isEmpty()) return std::numeric_limits<float>::quiet_NaN();
        auto unit = t.substring (num.length());
        if (unit.isEmpty() && next.isNotEmpty()) { unit = next; usedNext = true; }
        const float v = num.getFloatValue();
        if (unit == "ms" || unit.startsWith ("milli")) return v / 1000.0f;
        if (unit == "s" || unit.startsWith ("sec")) return v;
        usedNext = false;
        return std::numeric_limits<float>::quiet_NaN();
    }
}

SearchQuery SearchQuery::parse (const juce::String& raw)
{
    SearchQuery q;
    auto text = raw.toLowerCase().replaceCharacters (",;:!?\"()/", "         ");
    text = text.replace ("high-pitched", "high").replace ("low-pitched", "low").replace ("drawn-out", "sustained")
               .replace ("pitch bend", "bend").replace ("pitch-bend", "bend").replace ("head voice", "falsetto")
               .replace ("long note", "sustained").replace ("held note", "sustained").replace ("long tail", "longtail")
               .replace ("long decay", "longtail").replace ("-", " ");
    juce::StringArray tok;
    tok.addTokens (text, " ", "");
    tok.removeEmptyStrings();
    for (auto& t : tok) if (t.endsWithChar ('.')) t = t.dropLastCharacters (1);

    static const juce::StringArray stop { "a", "an", "the", "with", "and", "of", "for", "that", "is", "vocal", "vocals",
        "voice", "sample", "samples", "fragment", "fragments", "chop", "chops", "sound", "sounds", "suitable", "something",
        "like", "very", "really", "some", "kind", "sort", "me", "find", "i", "want", "quite", "bit", "little", "in",
        "to", "on", "it", "has", "have", "which", "good", "nice", "one", "ones", "singing", "sung", "note", "notes",
        "pitch", "pitched", "movement", "melody", "line", "texture", "piece", "part", "than", "around", "about" };

    bool negate = false;
    auto add = [&] (juce::String label, Feature f, float lo, float hi, float w = 1.0f)
    {
        if (negate) { std::swap (lo, hi); label = "not " + label; }
        for (auto& c : q.constraints) if (c.feature == f && c.label == label) return; // de-dup
        q.constraints.push_back ({ label, f, lo, hi, w });
    };

    for (int i = 0; i < tok.size(); ++i)
    {
        const auto t = tok[i];
        const auto next = i + 1 < tok.size() ? tok[i + 1] : juce::String();
        const auto prev = i > 0 ? tok[i - 1] : juce::String();

        if (t == "not" || t == "no" || t == "without" || t == "non") { negate = true; continue; }

        // comparisons: "under 2 seconds", "longer than 1s", "above a4", "below c3", "around e4"
        const bool less = t == "under" || t == "below" || t == "shorter" || t == "less" || t == "max";
        const bool more = t == "over" || t == "above" || t == "longer" || t == "more" || t == "min" || t == "at";
        const bool near = t == "around" || t == "near" || t == "about";
        if (less || more || near)
        {
            int j = i + 1;
            while (j < tok.size() && (tok[j] == "than" || tok[j] == "least")) ++j;
            if (j < tok.size())
            {
                if (const int n = parseNote (tok[j]); n >= 0)
                {
                    const auto name = noteName (n);
                    if (near) { add ("register near " + name, F::Note, (float) n - 3, (float) n + 3); q.constraints.back().band = true; }
                    else if (less) add ("register below " + name, F::Note, (float) n + 2, (float) n);
                    else add ("register above " + name, F::Note, (float) n - 2, (float) n);
                    i = j; negate = false; continue;
                }
                bool usedNext = false;
                const float sec = parseSeconds (tok[j], j + 1 < tok.size() ? tok[j + 1] : juce::String(), usedNext);
                if (! std::isnan (sec))
                {
                    const auto lbl = juce::String (sec, sec < 1 ? 2 : 1) + " s";
                    if (less) add ("length <= " + lbl, F::Duration, sec * 1.25f, sec);
                    else if (more) add ("length >= " + lbl, F::Duration, sec * 0.8f, sec);
                    else add ("length ~ " + lbl, F::Duration, sec * 0.5f, sec);
                    i = j + (usedNext ? 1 : 0); negate = false; continue;
                }
            }
        }

        bool matched = true;
        if (t == "high" || t == "higher" || t == "soprano" || t == "squeaky")       add ("high register", F::Note, 62, 69);
        else if (t == "falsetto")                                                  { add ("high register", F::Note, 64, 71); add ("breathy", F::Breathiness, 0.2f, 0.4f, 0.5f); }
        else if (t == "low" || t == "deep" || t == "bass" || t == "baritone" || t == "lower") add ("low register", F::Note, 60, 50);
        else if (t == "airy" || t == "air")                                        { add ("airy top end", F::Airiness, 0.01f, 0.08f); add ("breathy", F::Breathiness, 0.2f, 0.45f, 0.5f); }
        else if (t == "breathy" || t == "breath" || t == "breathing" || t == "whisper" || t == "whispered" || t == "whispery")
                                                                                     add ("breathy", F::Breathiness, 0.25f, 0.55f);
        else if (t == "soft" || t == "gentle" || t == "delicate" || t == "smooth") { add ("soft attack", F::Attack, 0.02f, 0.15f); add ("quiet", F::Level, -18.0f, -30.0f, 0.5f); }
        else if (t == "quiet" || t == "faint")                                     add ("quiet", F::Level, -18.0f, -30.0f);
        else if (t == "loud" || t == "powerful" || t == "belted" || t == "strong")  add ("loud", F::Level, -26.0f, -14.0f);
        else if (t == "sustained" || t == "held" || t == "hold" || t == "sustain" || t == "holding")
                                                                                     add ("held note", F::Sustain, 0.3f, 0.9f);
        else if (t == "vowel" || t == "vowels" || t == "ooh" || t == "aah" || t == "ahh")
                                                                                   { add ("held note", F::Sustain, 0.2f, 0.6f); add ("voiced", F::Voiced, 0.6f, 0.9f, 0.5f); }
        else if (t == "longtail" || ((t == "tail" || t == "decay" || t == "decays") && prev == "long"))
                                                                                   add ("long decay", F::Decay, 0.15f, 0.6f);
        else if (t == "long" || t == "longer")
        {
            if (next == "decay" || next == "tail") continue; // handled on next token
            // "long falsetto note", "long high tone": a held note, not just a long fragment
            const auto next2 = i + 2 < tok.size() ? tok[i + 2] : juce::String();
            static const juce::StringArray held { "note", "notes", "tone", "vowel", "vowels" };
            if (held.contains (next) || held.contains (next2)) add ("held note", F::Sustain, 0.3f, 0.9f);
            else add ("long", F::Duration, 1.2f, 2.5f);
        }
        else if (t == "short" || t == "brief" || t == "quick" || t == "tiny" || t == "stab" || t == "stabs")
                                                                                     add ("short", F::Duration, 1.2f, 0.5f);
        else if (t == "descending" || t == "falling" || t == "downward" || t == "down" || t == "dropping")
                                                                                     add ("descending", F::Slope, -0.5f, -3.0f);
        else if (t == "ascending" || t == "rising" || t == "upward" || t == "up" || t == "climbing")
                                                                                     add ("ascending", F::Slope, 0.5f, 3.0f);
        else if (t == "bend" || t == "bends" || t == "bending" || t == "bent" || t == "glide" || t == "slide" || t == "sliding" || t == "scoop")
                                                                                     add ("pitch bend", F::Bend, 1.0f, 3.0f);
        else if (t == "vibrato" || t == "wobbly" || t == "wobble" || t == "shaky" || t == "trembling")
                                                                                     add ("vibrato", F::Vibrato, 0.08f, 0.35f);
        else if (t == "stable" || t == "steady" || t == "straight")              add ("stable pitch", F::Stability, 0.8f, 0.2f);
        else if (t == "clean" || t == "pure" || t == "clear")                     { add ("pure tone", F::Harmonicity, 0.6f, 0.9f); add ("not breathy", F::Breathiness, 0.5f, 0.25f, 0.5f); }
        else if (t == "dark" || t == "warm" || t == "muffled")                    add ("dark tone", F::Centroid, 2500.0f, 1000.0f);
        else if (t == "bright" || t == "crisp" || t == "sharp" || t == "shiny")   add ("bright tone", F::Centroid, 1500.0f, 3500.0f);
        else if (t == "melodic" || t == "wide" || t == "range")                   add ("wide pitch range", F::Range, 2.0f, 7.0f);
        else if (t == "unusual" || t == "weird" || t == "strange" || t == "odd" || t == "unconventional")
                                                                                   { add ("unusual movement", F::Jumps, 0.3f, 2.0f, 0.5f); add ("EXPERIMENTAL score", F::ScoreExperimental, 0.3f, 0.6f); }
        else if (t == "reverse" || t == "reversing" || t == "reversed" || t == "backwards" || t == "backward")
                                                                                   { add ("long tail (reverses into a swell)", F::Decay, 0.1f, 0.5f); add ("sharp start", F::Attack, 0.1f, 0.02f, 0.5f); }
        else if (t == "ghostly" || t == "ghost" || t == "haunted" || t == "haunting" || t == "eerie" || t == "spooky" || t == "creepy" || t == "dark-sounding")
                                                                                     add ("HAUNTED score", F::ScoreHaunted, 0.3f, 0.65f);
        else if (t == "ethereal" || t == "dreamy" || t == "floaty" || t == "floating" || t == "atmospheric")
                                                                                     add ("ETHEREAL score", F::ScoreEthereal, 0.3f, 0.65f);
        else if (t == "heavenly" || t == "angelic" || t == "celestial" || t == "choir" || t == "choral" || t == "heaven")
                                                                                     add ("HEAVENLY score", F::ScoreHeavenly, 0.3f, 0.65f);
        else if (t == "nostalgic" || t == "nostalgia" || t == "vintage" || t == "memory" || t == "old")
                                                                                     add ("NOSTALGIC score", F::ScoreNostalgic, 0.3f, 0.65f);
        else if (t == "experimental" || t == "glitchy" || t == "glitch" || t == "chaotic" || t == "chaos")
                                                                                     add ("EXPERIMENTAL score", F::ScoreExperimental, 0.3f, 0.65f);
        else matched = false;

        if (! matched && ! stop.contains (t) && t.containsAnyOf ("abcdefghijklmnopqrstuvwxyz"))
            q.ignoredWords.addIfNotAlreadyThere (t);
        if (matched || ! stop.contains (t)) negate = false;
    }
    return q;
}

} // namespace ghostcut
