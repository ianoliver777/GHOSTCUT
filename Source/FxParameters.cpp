#include "FxParameters.h"

namespace ghostcut::fxp
{

using namespace juce;

static NormalisableRange<float> logRange (float lo, float hi)
{
    NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

void addParameters (AudioProcessorValueTreeState::ParameterLayout& l)
{
    // readable values everywhere: "80 Hz", "9.0 kHz", "1.60 s", "45 ms", "-46 dB", "0.35"
    auto fmt = [] (juce::String unit)
    {
        return [unit] (float v, int) -> juce::String
        {
            if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            if (unit == "s")  return juce::String (v, v < 10.0f ? 2 : 1) + " s";
            if (unit == "ms") return juce::String (juce::roundToInt (v)) + " ms";
            if (unit == "dB") return v <= -79.9f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " dB";
            if (unit == "st") return (v > 0 ? "+" : "") + juce::String (v, 1) + " st";
            if (unit == "ct") return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
            if (unit == "bit") return juce::String (v, 1) + " bit";
            return juce::String (v, 2);
        };
    };
    auto f = [&l, fmt] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit = "")
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (fmt (unit))));
    };
    auto b = [&l] (const char* id, const char* name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    b (texOn, "Texture On", false);
    f (texDrive, "Saturation", { 0, 1, 0.01f }, 0.0f);
    f (texNoise, "Noise", { -80, -20, 0.1f }, -80.0f, "dB");
    f (texTilt, "Tone Tilt", { -1, 1, 0.01f }, 0.0f);
    f (texHp, "Texture Low Cut", logRange (20, 1000), 20.0f, "Hz");
    f (texLp, "Texture High Cut", logRange (1000, 20000), 20000.0f, "Hz");
    f (texChorus, "Chorus", { 0, 1, 0.01f }, 0.0f);
    f (texWow, "Wow", { 0, 1, 0.01f }, 0.0f);
    f (texFlutter, "Flutter", { 0, 1, 0.01f }, 0.0f);

    b (lofiOn, "Lo-Fi On", false);
    f (lofiRate, "Sample Rate", logRange (1000, 44100), 44100.0f, "Hz");
    f (lofiBits, "Bit Depth", { 4, 16, 0.1f }, 16.0f, "bit");
    f (lofiBand, "Bandwidth", logRange (1000, 20000), 20000.0f, "Hz");
    f (lofiDist, "Lo-Fi Drive", { 0, 1, 0.01f }, 0.0f);
    f (lofiMix, "Lo-Fi Mix", { 0, 1, 0.01f }, 1.0f);

    b (dlyOn, "Delay On", false);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { dlyDiv, 1 }, "Delay Time", FxSettings::delayDivisionNames(), 5));
    f (dlyFb, "Delay Feedback", { 0, 0.95f, 0.01f }, 0.35f);
    b (dlyPing, "Ping-Pong", true);
    f (dlyLow, "Delay Low Cut", logRange (20, 2000), 150.0f, "Hz");
    f (dlyHigh, "Delay High Cut", logRange (1000, 20000), 6000.0f, "Hz");
    f (dlyMix, "Delay Mix", { 0, 1, 0.01f }, 0.25f);

    b (revOn, "Reverb On", false);
    f (revSize, "Reverb Size", { 0, 1, 0.01f }, 0.6f);
    f (revDecay, "Reverb Decay", logRange (0.2f, 20.0f), 3.0f, "s");
    f (revPre, "Pre-Delay", { 0, 250, 0.1f }, 20.0f, "ms");
    f (revDamp, "Damping", { 0, 1, 0.01f }, 0.4f);
    f (revWidth, "Reverb Width", { 0, 1, 0.01f }, 1.0f);
    f (revMix, "Reverb Mix", { 0, 1, 0.01f }, 0.3f);

    l.add (std::make_unique<AudioParameterInt> (ParameterID { pitchSemi, 1 }, "Pitch", -12, 12, 0, AudioParameterIntAttributes().withLabel ("st")));
    f (pitchFine, "Fine Tune", { -100, 100, 1 }, 0.0f, "ct");
    f (formant, "Formant", { -12, 12, 0.1f }, 0.0f, "st");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { harmony, 1 }, "Harmony", RenderOptions::harmonyNames(), 0));
    b (revAll, "Reverse All Chops", false);
    f (revXfade, "Reverse Transition", logRange (1, 500), 30.0f, "ms");
    b (fxFollow, "FX Follow Mode", true);
}

std::atomic<float>* Reader::get (AudioProcessorValueTreeState& s, const char* id)
{
    auto* p = s.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

Reader::Reader (AudioProcessorValueTreeState& s)
{
    tOn = get (s, texOn); tDrive = get (s, texDrive); tNoise = get (s, texNoise); tTilt = get (s, texTilt);
    tHp = get (s, texHp); tLp = get (s, texLp); tChorus = get (s, texChorus); tWow = get (s, texWow); tFlutter = get (s, texFlutter);
    lOn = get (s, lofiOn); lRate = get (s, lofiRate); lBits = get (s, lofiBits); lBand = get (s, lofiBand); lDist = get (s, lofiDist); lMix = get (s, lofiMix);
    dOn = get (s, dlyOn); dDiv = get (s, dlyDiv); dFb = get (s, dlyFb); dPing = get (s, dlyPing); dLow = get (s, dlyLow); dHigh = get (s, dlyHigh); dMix = get (s, dlyMix);
    rOn = get (s, revOn); rSize = get (s, revSize); rDecay = get (s, revDecay); rPre = get (s, revPre); rDamp = get (s, revDamp); rWidth = get (s, revWidth); rMix = get (s, revMix);
    pSemi = get (s, pitchSemi); pFine = get (s, pitchFine); pFormant = get (s, formant); pHarm = get (s, harmony);
    pRevAll = get (s, revAll); pRevX = get (s, revXfade); follow = get (s, fxFollow);
}

FxSettings Reader::fx() const noexcept
{
    FxSettings f;
    f.texOn = tOn->load() > 0.5f; f.drive = tDrive->load(); f.noiseDb = tNoise->load(); f.tilt = tTilt->load();
    f.hpHz = tHp->load(); f.lpHz = tLp->load(); f.chorus = tChorus->load(); f.wow = tWow->load(); f.flutter = tFlutter->load();
    f.lofiOn = lOn->load() > 0.5f; f.lofiRateHz = lRate->load(); f.lofiBits = lBits->load(); f.lofiBandHz = lBand->load();
    f.lofiDist = lDist->load(); f.lofiMix = lMix->load();
    f.dlyOn = dOn->load() > 0.5f; f.dlyDivision = (int) dDiv->load(); f.dlyFeedback = dFb->load(); f.dlyPingPong = dPing->load() > 0.5f;
    f.dlyLowCut = dLow->load(); f.dlyHighCut = dHigh->load(); f.dlyMix = dMix->load();
    f.revOn = rOn->load() > 0.5f; f.revSize = rSize->load(); f.revDecay = rDecay->load(); f.revPreDelayMs = rPre->load();
    f.revDamping = rDamp->load(); f.revWidth = rWidth->load(); f.revMix = rMix->load();
    return f;
}

RenderOptions Reader::render() const noexcept
{
    RenderOptions o;
    o.semitones = std::round (pSemi->load());
    o.fineCents = std::round (pFine->load());
    o.formantSemis = std::round (pFormant->load() * 10.0f) / 10.0f;
    o.harmony = (int) pHarm->load();
    o.reverseAll = pRevAll->load() > 0.5f;
    o.reverseFadeMs = std::round (pRevX->load());
    return o;
}

float Reader::pitchRatioForKeys() const noexcept
{
    return std::pow (2.0f, (std::round (pSemi->load()) + pFine->load() / 100.0f) / 12.0f);
}

void applyModePreset (AudioProcessorValueTreeState& s, GenMode mode)
{
    auto set = [&s] (const char* id, float v)
    {
        if (auto* p = s.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    // start from neutral
    for (auto* id : { texOn, lofiOn, dlyOn, revOn }) set (id, 0.0f);
    set (texDrive, 0); set (texNoise, -80); set (texTilt, 0); set (texHp, 20); set (texLp, 20000);
    set (texChorus, 0); set (texWow, 0); set (texFlutter, 0);

    switch (mode)
    {
        case GenMode::Ethereal:
            set (revOn, 1); set (revSize, 0.85f); set (revDecay, 6.5f); set (revPre, 45); set (revDamp, 0.35f); set (revWidth, 1); set (revMix, 0.42f);
            set (dlyOn, 1); set (dlyDiv, 6); set (dlyFb, 0.45f); set (dlyPing, 1); set (dlyLow, 300); set (dlyHigh, 5500); set (dlyMix, 0.22f);
            set (texOn, 1); set (texChorus, 0.35f); set (texTilt, 0.2f); set (texHp, 120);
            break;
        case GenMode::Haunted:
            set (revOn, 1); set (revSize, 0.95f); set (revDecay, 9.0f); set (revPre, 80); set (revDamp, 0.75f); set (revWidth, 0.9f); set (revMix, 0.5f);
            set (dlyOn, 1); set (dlyDiv, 9); set (dlyFb, 0.5f); set (dlyPing, 0); set (dlyLow, 200); set (dlyHigh, 2500); set (dlyMix, 0.2f);
            set (texOn, 1); set (texTilt, -0.6f); set (texLp, 5000); set (texWow, 0.35f); set (texNoise, -58);
            break;
        case GenMode::Nostalgia:
            set (texOn, 1); set (texDrive, 0.35f); set (texTilt, -0.25f); set (texLp, 9000); set (texHp, 80);
            set (texWow, 0.45f); set (texFlutter, 0.3f); set (texNoise, -46);
            set (lofiOn, 1); set (lofiRate, 26000); set (lofiBits, 12); set (lofiBand, 11000); set (lofiDist, 0.15f); set (lofiMix, 0.8f);
            set (revOn, 1); set (revSize, 0.4f); set (revDecay, 1.6f); set (revPre, 10); set (revDamp, 0.6f); set (revWidth, 0.7f); set (revMix, 0.18f);
            break;
        case GenMode::Heaven:
            set (revOn, 1); set (revSize, 1.0f); set (revDecay, 14.0f); set (revPre, 30); set (revDamp, 0.15f); set (revWidth, 1); set (revMix, 0.55f);
            set (dlyOn, 1); set (dlyDiv, 8); set (dlyFb, 0.4f); set (dlyPing, 1); set (dlyLow, 400); set (dlyHigh, 9000); set (dlyMix, 0.18f);
            set (texOn, 1); set (texTilt, 0.35f); set (texChorus, 0.25f); set (texHp, 150);
            break;
        case GenMode::Chaos:
            set (dlyOn, 1); set (dlyDiv, 1); set (dlyFb, 0.7f); set (dlyPing, 1); set (dlyLow, 300); set (dlyHigh, 8000); set (dlyMix, 0.3f);
            set (lofiOn, 1); set (lofiRate, 9000); set (lofiBits, 7); set (lofiBand, 14000); set (lofiDist, 0.4f); set (lofiMix, 0.55f);
            set (revOn, 1); set (revSize, 0.3f); set (revDecay, 1.2f); set (revPre, 0); set (revDamp, 0.3f); set (revWidth, 1); set (revMix, 0.2f);
            set (texOn, 1); set (texDrive, 0.5f); set (texFlutter, 0.4f);
            break;
        case GenMode::numModes: break;
    }
}

} // namespace ghostcut::fxp
