#pragma once

#include "PhraseSegmenter.h"

namespace ghostcut
{

/** Turns per-frame measurements into per-fragment FragmentFeatures:
    pitch (median, register, spread, range, slope, bends, sustain, vibrato),
    voice quality (voicing, harmonicity, breathiness, airiness, brightness),
    envelope (level, attack, decay) and movement (pitch jumps, spectral flux). */
class FeatureExtractor
{
public:
    static void measure (Fragment& f, const SourceSnapshot& src, const FrameFeatures& frames);
};

} // namespace ghostcut
