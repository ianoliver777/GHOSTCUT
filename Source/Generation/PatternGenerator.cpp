#include "PatternGenerator.h"
#include "Random.h"
#include <algorithm>

namespace ghostcut
{

const ModeProfile& getModeProfile (GenMode m)
{
    static const ModeProfile profiles[] = {
        // ETHEREAL: sparse, slowed, overlapping airy phrases with octave/fifth shimmer
        { { 1, 0, .1f, 0, .5f, 0, .15f, 0, .7f, 0, .1f, 0, .4f, 0, .2f, .05f }, 0.8f,
          3.0f, 8.0f, false, 1.3f, 2.0f, 0.12f, 0.35f, 0.0f, 0.10f,
          { 1, -1, 2, -2, 0 }, 0.08f, 0.0f,
          { 12, 7 }, 0.45f, 0.35f, 6.0f,
          40.0f, 220.0f, 0.0f, 1.5f, 0.60f, 0.15f, false, 8 },

        // HAUNTED: off-grid, reversed, slowed and darkened, wrong notes, octave-down ghosts
        { { 1, 0, 0, .3f, 0, 0, .6f, 0, 0, .2f, 0, .4f, 0, 0, .3f, 0 }, 0.7f,
          2.0f, 6.0f, false, 1.6f, 2.6f, 0.45f, 0.50f, 0.02f, 0.30f,
          { 1, -1, 0, 3, -3 }, 0.15f, 0.25f,
          { -12, 6 }, 0.30f, 0.40f, 18.0f,
          15.0f, 350.0f, -4.0f, -1.5f, 0.55f, 0.20f, false, 6 },

        // NOSTALGIA: swung boom-bap pocket, short chops, repetition, small detune
        { { 1, 0, .3f, .2f, .7f, 0, .4f, .3f, .9f, 0, .3f, .3f, .7f, .1f, .5f, .2f }, 1.0f,
          1.0f, 3.0f, false, 0.95f, 1.1f, 0.08f, 0.55f, 0.05f, 0.15f,
          { 0, 1, -1, 2, -2, 4, -4 }, 0.10f, 0.03f,
          { -12 }, 0.15f, 0.30f, 10.0f,
          8.0f, 60.0f, -1.0f, 0.5f, 0.70f, 0.20f, true, 4 },

        // HEAVEN: long held notes stretched to fill, stacked harmonies, slow movement
        { { 1, 0, 0, 0, .3f, 0, 0, 0, .8f, 0, 0, 0, .3f, 0, .1f, 0 }, 0.7f,
          4.0f, 12.0f, true, 1.0f, 1.0f, 0.05f, 0.30f, 0.0f, 0.05f,
          { 1, -1, 2, -2, 0 }, 0.05f, 0.0f,
          { 12, 7, 4 }, 0.80f, 0.45f, 8.0f,
          80.0f, 300.0f, 0.0f, 2.0f, 0.55f, 0.10f, false, 8 },

        // CHAOS: dense stutters, ratchets, jumps, random starts, wild formants
        { { 1, .5f, .6f, .7f, .8f, .4f, .7f, .6f, .9f, .5f, .6f, .8f, .7f, .5f, .8f, .6f }, 1.3f,
          0.25f, 1.5f, false, 0.5f, 1.5f, 0.30f, 0.25f, 0.35f, 0.60f,
          { 1, -1, 3, -3, 5, -5, 7, -7 }, 0.40f, 0.15f,
          { 12, -12, 7, 1 }, 0.20f, 0.40f, 30.0f,
          1.0f, 4.0f, -6.0f, 6.0f, 0.75f, 0.25f, true, 3 },
    };
    return profiles[juce::jlimit (0, 4, (int) m)];
}

float PatternGenerator::modeScore (const Fragment& f, GenMode m)
{
    switch (m)
    {
        case GenMode::Ethereal:  return f.scores[FragmentScores::Ethereal];
        case GenMode::Haunted:   return f.scores[FragmentScores::Haunted];
        case GenMode::Nostalgia: return f.scores[FragmentScores::Nostalgic];
        case GenMode::Heaven:    return f.scores[FragmentScores::Heavenly];
        case GenMode::Chaos:     return f.scores[FragmentScores::Experimental];
        case GenMode::numModes:  break;
    }
    return 0.5f;
}

std::vector<GenSlot> PatternGenerator::pickSlots (const std::vector<Fragment>& frags, GenMode mode, int maxSlots)
{
    std::vector<const Fragment*> sorted;
    for (auto& f : frags) if (f.length() > 0) sorted.push_back (&f);
    std::stable_sort (sorted.begin(), sorted.end(), [mode] (const Fragment* a, const Fragment* b)
                      { return modeScore (*a, mode) > modeScore (*b, mode); });
    std::vector<GenSlot> out;
    for (auto* f : sorted)
    {
        if ((int) out.size() >= maxSlots) break;
        out.push_back ({ f->uid, f->effectiveRoot (60), f->features.durationSec, modeScore (*f, mode) });
    }
    return out;
}

Pattern PatternGenerator::generate (const GenSettings& s, const std::vector<GenSlot>& slots, double bpm)
{
    Pattern p;
    p.length = s.steps == 32 ? 32 : 16;
    p.stepDiv = s.stepDiv;
    p.swing = s.swing;
    p.madeWith = s;
    if (slots.empty()) return p;
    for (auto& sl : slots) { p.slotUids.push_back (sl.uid); p.slotRoots.push_back (sl.root); }

    const auto& prof = getModeProfile (s.mode);
    Random32 rng (hashCombine (s.seed, (uint64_t) s.mode * (uint64_t) 1000003 + (uint64_t) 17));
    const float stepSec = (float) (60.0 / std::max (20.0, bpm) / s.stepDiv);
    const int stepsPerBar = s.stepDiv * 4;
    const auto& scaleIv = ScaleQuantizer::intervals (s.scale);
    const int maxDeg = (int) scaleIv.size() * s.octaveRange;
    // root of the melody range: the chosen key at the chosen octave
    const int rangeRoot = s.rootOctaveNote - (((s.rootOctaveNote - s.key) % 12) + 12) % 12;

    // ---- rhythm --------------------------------------------------------------
    std::array<bool, 32> hit {};
    float busyUntil = -1.0f;
    for (int i = 0; i < p.length; ++i)
    {
        const int pos16 = ((i % stepsPerBar) * 16) / stepsPerBar;
        const float w = prof.rhythm[(size_t) pos16];
        float prob = w * (0.15f + s.density * prof.densityScale) + s.randomness * (rng.uniformf() - 0.5f) * 0.6f;
        if (w <= 0.0f) prob = s.randomness * s.density * 0.15f; // ghost hits only with randomness
        bool h = i == 0 || rng.chance (juce::jlimit (0.0f, 1.0f, prob));
        // sustained modes leave space while a note is still ringing
        if (h && i > 0 && ! prof.choke && (float) i < busyUntil && rng.chance (0.7)) h = false;
        if (h) busyUntil = (float) i + prof.gateMin;
        hit[(size_t) i] = h;
    }

    // guarantee a usable minimum: 2 hits per bar at density 0, up to 6 at density 1
    // (fills the strongest remaining rhythm positions for the mode)
    const int bars = std::max (1, p.length / stepsPerBar);
    const int minHits = bars * (int) std::lround (2.0f + 4.0f * s.density * std::min (1.0f, prof.densityScale));
    int count = 0;
    for (int i = 0; i < p.length; ++i) count += hit[(size_t) i] ? 1 : 0;
    while (count < std::min (minHits, p.length))
    {
        float best = -1.0f; int bestI = -1;
        for (int i = 0; i < p.length; ++i)
        {
            if (hit[(size_t) i]) continue;
            const int pos16 = ((i % stepsPerBar) * 16) / stepsPerBar;
            const float score = prof.rhythm[(size_t) pos16] + 0.05f + 0.3f * rng.uniformf();
            if (score > best) { best = score; bestI = i; }
        }
        if (bestI < 0) break;
        hit[(size_t) bestI] = true;
        ++count;
    }

    // ---- melody: motif + varied repeats --------------------------------------
    std::vector<int> motif ((size_t) std::max (1, prof.motifLength));
    int degree = rng.range (0, std::max (0, maxDeg / 2));
    for (auto& d : motif)
    {
        if (rng.chance (prof.leapProb * (0.5f + s.complexity)))
            degree += (rng.chance (0.5) ? 1 : -1) * rng.range (3, 3 + (int) (4 * s.complexity));
        else
        {
            // complexity widens which of the profile's intervals get used
            const int n = (int) prof.stepIntervals.size();
            const int usable = std::max (1, (int) std::ceil (n * (0.4f + 0.6f * s.complexity)));
            degree += prof.stepIntervals[(size_t) rng.range (0, std::min (n, usable) - 1)];
        }
        if (degree < 0) degree = -degree;                 // reflect at the edges
        if (degree > maxDeg) degree = 2 * maxDeg - degree;
        d = juce::jlimit (0, maxDeg, degree);
    }
    const float variation = juce::jlimit (0.0f, 1.0f, s.complexity * 0.6f + s.randomness * 0.3f);

    // ---- events ----------------------------------------------------------------
    int prevSlot = -1, hitIndex = 0;
    for (int i = 0; i < p.length; ++i)
    {
        if (! hit[(size_t) i]) continue;
        StepEvent e;

        // slot: favour fragments that suit the mode, sometimes repeat the last one
        if (prevSlot >= 0 && rng.chance (prof.repeatProb)) e.slot = prevSlot;
        else
        {
            float total = 0.0f;
            for (auto& sl : slots) total += sl.modeScore * sl.modeScore + 0.05f;
            float r = rng.uniformf() * total;
            e.slot = (int) slots.size() - 1;
            for (int k = 0; k < (int) slots.size(); ++k)
            {
                r -= slots[(size_t) k].modeScore * slots[(size_t) k].modeScore + 0.05f;
                if (r <= 0.0f) { e.slot = k; break; }
            }
        }
        prevSlot = e.slot;

        int deg = motif[(size_t) (hitIndex % (int) motif.size())];
        if (hitIndex >= (int) motif.size() && rng.chance (variation))
            deg = juce::jlimit (0, maxDeg, deg + (rng.chance (0.5) ? 1 : -1) * rng.range (1, 2));
        int note = ScaleQuantizer::degreeToMidi (rangeRoot, s.scale, deg);
        if (prof.chromaticProb > 0 && rng.chance (prof.chromaticProb)) note += rng.chance (0.5) ? 1 : -1;

        // keep transposition musical: move by octaves toward the chop's own pitch
        const int root = slots[(size_t) e.slot].root;
        while (note - root > 14) note -= 12;
        while (root - note > 14) note += 12;
        e.note = note;

        e.gateSteps = rng.rangef (prof.gateMin, prof.gateMax) * (prof.choke ? 1.0f : (1.0f - 0.4f * s.density));
        e.gateSteps = std::max (0.25f, std::min (e.gateSteps, (float) p.length));
        const bool onBeat = (i % s.stepDiv) == 0;
        e.velocity = juce::jlimit (0.15f, 1.0f, prof.velBase + (onBeat ? prof.velAccent : 0.0f)
                                                    + s.randomness * (rng.uniformf() - 0.5f) * 0.4f);
        e.reverse = rng.chance (prof.reverseProb);
        if (rng.chance (prof.randomStartProb))
        {
            e.startFrac = rng.rangef (0.0f, 0.5f);
            e.endFrac = juce::jmin (1.0f, e.startFrac + rng.rangef (0.25f, 0.6f));
        }
        e.fitToGate = prof.fitToGate;
        e.stretch = prof.fitToGate ? 1.0f : rng.rangef (prof.stretchMin, prof.stretchMax);
        if (! prof.fitToGate)
        {
            // never stretch a chop far past its gate when it will be choked anyway
            const float chopSec = slots[(size_t) e.slot].durationSec * (e.endFrac - e.startFrac);
            if (prof.choke && chopSec > 0.01f)
                e.stretch = std::min (e.stretch, std::max (0.5f, e.gateSteps * stepSec / chopSec * 1.5f));
        }
        if (rng.chance (prof.ratchetProb * (0.5f + s.complexity))) e.ratchet = rng.range (2, 4);
        e.fadeInMs = prof.fadeInMs;
        e.fadeOutMs = prof.fadeOutMs;
        e.formantSemis = rng.rangef (prof.formantMin, prof.formantMax);
        e.choke = prof.choke;
        if (! prof.layerSemis.empty() && rng.chance (prof.layerProb))
        {
            const int raw = prof.layerSemis[(size_t) rng.range (0, (int) prof.layerSemis.size() - 1)];
            // fifths/thirds snap into the scale; octaves stay octaves
            const int semis = std::abs (raw) % 12 == 0 ? raw : ScaleQuantizer::quantize (note + raw, s.key, s.scale) - note;
            if (semis != 0)
                e.layers.push_back ({ semis, prof.layerGain, rng.rangef (-prof.layerDetune, prof.layerDetune) });
        }

        p.events[(size_t) i] = e;
        p.active[(size_t) i] = true;
        ++hitIndex;
    }
    return p;
}

} // namespace ghostcut
