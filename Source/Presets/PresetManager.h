#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Session.h"

namespace ghostcut
{

/**
    Presets = every automatable parameter (instrument, pattern sync, effects,
    pitch/formant) + the GENERATE settings. They never contain audio, fragments or
    patterns: those belong to the project, not to a sound preset.

    Factory presets are defined in code (so they can't go missing) and are also
    exposed to the host as programs. User presets are XML files:
        ~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Presets/<Category>/<Name>.ghostpreset
*/
class PresetManager : public juce::ChangeBroadcaster
{
public:
    struct Info
    {
        juce::String name, category;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
        juce::String id() const { return (factory ? "factory:" : "user:") + category + "/" + name; }
    };

    PresetManager (juce::AudioProcessorValueTreeState&, Session&);

    static juce::StringArray categories();
    static juce::File getUserFolder();

    juce::Array<Info> list() const;                  // factory first, then user, grouped by category
    int numFactory() const;
    juce::String factoryName (int index) const;

    bool load (const Info&, juce::String& error);
    bool loadFactory (int index);
    bool saveUser (const juce::String& name, const juce::String& category, juce::String& error);
    bool deleteUser (const Info&, juce::String& error);
    void step (int delta);                           // next / previous in list order

    juce::String getCurrentName() const { return currentName; }
    juce::String getCurrentId() const { return currentId; }
    void setCurrent (const juce::String& name, const juce::String& id) { currentName = name; currentId = id; sendChangeMessage(); }

    juce::ValueTree capture() const;                 // current state as a preset tree
    void apply (const juce::ValueTree& preset);

private:
    juce::AudioProcessorValueTreeState& apvts;
    Session& session;
    juce::String currentName { "Init" }, currentId;
};

} // namespace ghostcut
