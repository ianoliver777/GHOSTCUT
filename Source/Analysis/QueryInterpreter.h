#pragma once

#include "SearchQuery.h"

namespace ghostcut
{

/**
    Turns a search description into measurable constraints.

    GHOSTCUT ships ONE implementation: KeywordInterpreter (offline, deterministic,
    always available). It is what search uses.

    This interface exists so an optional language-model front end can be added later
    as a clearly separate component, e.g. a local model or a cloud API the user opts
    into. Contract for any such implementation:
      - It returns constraints in the JSON schema of SearchQuery::toJson():
          { "constraints": [ { "feature": "breathiness", "from": 0.25, "to": 0.55,
                               "weight": 1, "label": "breathy", "band": false }, ... ],
            "like_fragment": -1 }
      - GHOSTCUT validates that JSON with SearchQuery::fromJson() (unknown features or
        bad ranges are rejected) and shows the resulting constraints to the user exactly
        like keyword searches, so nothing is hidden or "magic".
      - If it fails or is unavailable, search falls back to KeywordInterpreter.
      - Core search must never require it (offline-first).
*/
class QueryInterpreter
{
public:
    virtual ~QueryInterpreter() = default;
    virtual juce::String name() const = 0;
    virtual SearchQuery interpret (const juce::String& text, juce::String& error) = 0;
};

class KeywordInterpreter final : public QueryInterpreter
{
public:
    juce::String name() const override { return "Keywords (offline)"; }
    SearchQuery interpret (const juce::String& text, juce::String&) override { return SearchQuery::parse (text); }
};

} // namespace ghostcut
