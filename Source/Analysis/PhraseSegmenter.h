#pragma once

#include "../Core/Fragment.h"
#include "../Core/SourceSnapshot.h"
#include <functional>

namespace ghostcut
{

/** Per-frame measurements (10 ms hop). Kept so Phase 2 can reuse them. */
struct FrameFeatures
{
    std::vector<float> db;        // RMS level, dBFS
    std::vector<float> flux;      // positive spectral flux, normalised 0..1
    std::vector<float> centroid;  // spectral centroid, Hz
    std::vector<float> f0;        // Hz, 0 = unvoiced
    std::vector<float> clarity;   // 1 - YIN aperiodicity, 0..1
    double hopSeconds = 0.01;
    int numFrames() const noexcept { return (int) db.size(); }
};

/**
    Phase 1 vocal phrase detector. Pure DSP, no ML:
      - RMS energy with an adaptive noise floor and hysteresis gate
      - spectral flux and centroid from a Hann-windowed FFT
      - YIN pitch on a decimated (~11 kHz) signal for voicing and f0
    Active regions are merged across short gaps (breaths), very short blips are
    dropped and overly long regions are split at their quietest point.

    Works best on isolated vocals / acapellas. On full mixes everything is
    "active", so cuts fall on energy dips; Phase 2 adds source separation first.
*/
class PhraseSegmenter
{
public:
    struct Settings
    {
        float sensitivity   = 0.5f;   // 0..1, higher = more, smaller fragments
        float minGapMs      = 180.0f; // gaps shorter than this are merged
        float minLengthMs   = 200.0f;
        float maxLengthMs   = 8000.0f;
        float preRollMs     = 15.0f;
        float tailMs        = 80.0f;
        float minVoicedRatio = 0.0f;  // 0 = keep everything active
        int   maxFragments  = 256;
    };

    using Progress = std::function<void (float)>;
    using ShouldContinue = std::function<bool()>;

    static FrameFeatures computeFrames (const SourceSnapshot& src,
                                        const Progress& progress = {},
                                        const ShouldContinue& keepGoing = {});

    /** Returns fragments sorted by start time. Empty vector if cancelled. */
    static std::vector<Fragment> segment (const SourceSnapshot& src,
                                          const FrameFeatures& frames,
                                          const Settings& settings);

    /** Measures features for an arbitrary region (used for manual fragments too). */
    static void measure (Fragment& f, const SourceSnapshot& src, const FrameFeatures& frames);
};

} // namespace ghostcut
