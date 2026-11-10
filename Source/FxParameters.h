#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Effects.h"
#include "Generation/ChopRenderer.h"
#include "Generation/Pattern.h"

namespace ghostcut::fxp
{
    // TEXTURE
    inline constexpr const char* texOn = "texOn", *texDrive = "texDrive", *texNoise = "texNoise", *texTilt = "texTilt",
                                 *texHp = "texHp", *texLp = "texLp", *texChorus = "texChorus", *texWow = "texWow", *texFlutter = "texFlutter";
    // LO-FI
    inline constexpr const char* lofiOn = "lofiOn", *lofiRate = "lofiRate", *lofiBits = "lofiBits", *lofiBand = "lofiBand",
                                 *lofiDist = "lofiDist", *lofiMix = "lofiMix";
    // DELAY
    inline constexpr const char* dlyOn = "dlyOn", *dlyDiv = "dlyDiv", *dlyFb = "dlyFb", *dlyPing = "dlyPing",
                                 *dlyLow = "dlyLow", *dlyHigh = "dlyHigh", *dlyMix = "dlyMix";
    // REVERB
    inline constexpr const char* revOn = "revOn", *revSize = "revSize", *revDecay = "revDecay", *revPre = "revPre",
                                 *revDamp = "revDamp", *revWidth = "revWidth", *revMix = "revMix";
    // PITCH / FORMANT + REVERSE (applied when chops are rendered)
    inline constexpr const char* pitchSemi = "pitchSemi", *pitchFine = "pitchFine", *formant = "formant", *harmony = "harmony",
                                 *revAll = "revAll", *revXfade = "revXfade";
    inline constexpr const char* fxFollow = "fxFollow";

    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout&);

    /** Cached raw parameter pointers for lock-free reads on the audio thread. */
    class Reader
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState&);
        FxSettings fx() const noexcept;
        RenderOptions render() const noexcept;
        float pitchRatioForKeys() const noexcept; // semitones + fine, for keyboard voices
        bool followMode() const noexcept { return follow->load() > 0.5f; }
    private:
        std::atomic<float>* get (juce::AudioProcessorValueTreeState&, const char*);
        std::atomic<float> *tOn, *tDrive, *tNoise, *tTilt, *tHp, *tLp, *tChorus, *tWow, *tFlutter;
        std::atomic<float> *lOn, *lRate, *lBits, *lBand, *lDist, *lMix;
        std::atomic<float> *dOn, *dDiv, *dFb, *dPing, *dLow, *dHigh, *dMix;
        std::atomic<float> *rOn, *rSize, *rDecay, *rPre, *rDamp, *rWidth, *rMix;
        std::atomic<float> *pSemi, *pFine, *pFormant, *pHarm, *pRevAll, *pRevX, *follow;
    };

    using ParamValues = std::vector<std::pair<juce::String, float>>;
    /** The effect settings that belong to a generation mode (real units, not 0..1). */
    ParamValues modePresetValues (GenMode);
    /** Loads the effects that belong to a generation mode (message thread). */
    void applyModePreset (juce::AudioProcessorValueTreeState&, GenMode);
}
