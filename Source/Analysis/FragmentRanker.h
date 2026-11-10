#pragma once

#include "../Core/Fragment.h"
#include <juce_core/juce_core.h>

namespace ghostcut
{

/**
    HEURISTIC character scores. Each score is a weighted mix of measured features
    that producers associate with the word, e.g. ETHEREAL leans on high register,
    breathiness, air, soft attack and held notes. They are tools for sorting, not
    claims about emotion. Every score can explain itself (see explain()).
*/
class FragmentRanker
{
public:
    struct Term { juce::String label; float weight; float value; }; // contribution = weight * value

    static void score (Fragment& f);
    static std::vector<Term> terms (const FragmentFeatures& ft, int kind);
    /** The top contributing reasons, e.g. "high register, breathy, held note". */
    static juce::String explain (const Fragment& f, int kind, int maxTerms = 3);
};

} // namespace ghostcut
