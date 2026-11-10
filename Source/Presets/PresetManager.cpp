#include "PresetManager.h"
#include "../FxParameters.h"

namespace ghostcut
{

namespace
{
    struct Factory
    {
        const char* name;
        const char* category;
        GenMode mode;
        fxp::ParamValues overrides;                  // applied after the mode's FX preset
        std::function<void (GenSettings&)> gen;      // generation tweaks
    };

    const std::vector<Factory>& factoryTable()
    {
        using S = ScaleQuantizer::Scale;
        static const std::vector<Factory> t = {
            // ETHEREAL
            { "Glass Choir", "Ethereal", GenMode::Ethereal, {},
              [] (GenSettings& g) { g.key = 9; g.scale = S::Minor; g.density = 0.4f; g.complexity = 0.35f; } },
            { "Halo Drift", "Ethereal", GenMode::Ethereal, { { "dlyDiv", 9 }, { "dlyFb", 0.55f }, { "revDecay", 9.0f } },
              [] (GenSettings& g) { g.key = 4; g.scale = S::Dorian; g.density = 0.3f; g.stepDiv = 2; } },
            { "Static Angel", "Ethereal", GenMode::Ethereal, { { "lofiOn", 1 }, { "lofiBits", 13 }, { "lofiRate", 30000 }, { "lofiMix", 0.6f }, { "texNoise", -54 } },
              [] (GenSettings& g) { g.key = 2; g.scale = S::Minor; g.density = 0.5f; g.randomness = 0.45f; } },
            // HAUNTED
            { "Basement Hymn", "Haunted", GenMode::Haunted, {},
              [] (GenSettings& g) { g.key = 1; g.scale = S::Phrygian; g.density = 0.45f; } },
            { "Reverse Seance", "Haunted", GenMode::Haunted, { { "revAll", 1 }, { "revXfade", 220 } },
              [] (GenSettings& g) { g.key = 7; g.scale = S::HarmonicMinor; g.density = 0.35f; } },
            { "Wrong Room", "Haunted", GenMode::Haunted, { { "formant", -3 }, { "texWow", 0.6f } },
              [] (GenSettings& g) { g.key = 6; g.scale = S::Phrygian; g.complexity = 0.85f; g.randomness = 0.6f; } },
            // NOSTALGIA
            { "Tape Memory", "Nostalgia", GenMode::Nostalgia, {},
              [] (GenSettings& g) { g.key = 9; g.scale = S::Minor; g.swing = 0.2f; } },
            { "Bedroom 2011", "Nostalgia", GenMode::Nostalgia, { { "pitchSemi", -2 }, { "lofiRate", 22000 }, { "revMix", 0.25f } },
              [] (GenSettings& g) { g.key = 5; g.scale = S::Minor; g.swing = 0.28f; g.density = 0.6f; } },
            { "Cassette Choir", "Nostalgia", GenMode::Nostalgia, { { "harmony", 6 }, { "texFlutter", 0.5f }, { "lofiBits", 10 } },
              [] (GenSettings& g) { g.key = 0; g.scale = S::Dorian; g.swing = 0.15f; g.complexity = 0.3f; } },
            // HEAVEN
            { "Cathedral", "Heaven", GenMode::Heaven, {},
              [] (GenSettings& g) { g.key = 4; g.scale = S::Major; g.density = 0.4f; } },
            { "Stacked Light", "Heaven", GenMode::Heaven, { { "harmony", 5 } },
              [] (GenSettings& g) { g.key = 9; g.scale = S::PentatonicMajor; g.density = 0.35f; g.octaveRange = 1; } },
            { "White Room", "Heaven", GenMode::Heaven, { { "formant", 2 }, { "revDecay", 18 }, { "revMix", 0.65f } },
              [] (GenSettings& g) { g.key = 2; g.scale = S::Major; g.density = 0.25f; } },
            // CHAOS
            { "Glitch Saint", "Chaos", GenMode::Chaos, {},
              [] (GenSettings& g) { g.key = 9; g.scale = S::Minor; g.density = 0.6f; } },
            { "Shatter", "Chaos", GenMode::Chaos, { { "lofiBits", 5 }, { "dlyFb", 0.8f } },
              [] (GenSettings& g) { g.key = 1; g.scale = S::Chromatic; g.density = 0.85f; g.complexity = 0.9f; g.steps = 32; g.stepDiv = 8; } },
            { "Data Ghost", "Chaos", GenMode::Chaos, { { "revOn", 1 }, { "revDecay", 6 }, { "revMix", 0.35f }, { "formant", 4 } },
              [] (GenSettings& g) { g.key = 11; g.scale = S::HarmonicMinor; g.density = 0.5f; g.randomness = 0.8f; } },
            // INSTRUMENTS
            { "Clean Sampler", "Instruments", GenMode::Nostalgia,
              { { "texOn", 0 }, { "lofiOn", 0 }, { "dlyOn", 0 }, { "revOn", 0 } }, {} },
            { "Chromatic Pad", "Instruments", GenMode::Heaven,
              { { "keyMode", 1 }, { "trigger", 1 }, { "attack", 120 }, { "release", 1500 }, { "dlyOn", 0 } }, {} },
        };
        return t;
    }

    void setParam (juce::ValueTree& t, const juce::String& id, float v)
    {
        juce::ValueTree p ("P");
        p.setProperty ("id", id, nullptr);
        p.setProperty ("v", v, nullptr);
        for (auto c : t) if (c.hasType ("P") && c["id"].toString() == id) { c.setProperty ("v", v, nullptr); return; }
        t.appendChild (p, nullptr);
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& a, Session& s) : apvts (a), session (s) {}

juce::StringArray PresetManager::categories() { return { "Ethereal", "Haunted", "Nostalgia", "Heaven", "Chaos", "Instruments", "User" }; }

juce::File PresetManager::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("TRIPLE SEVEN AUDIO").getChildFile ("GHOSTCUT").getChildFile ("Presets");
}

int PresetManager::numFactory() const { return (int) factoryTable().size(); }
juce::String PresetManager::factoryName (int i) const
{
    return (i >= 0 && i < numFactory()) ? juce::String (factoryTable()[(size_t) i].name) : juce::String();
}

juce::Array<PresetManager::Info> PresetManager::list() const
{
    juce::Array<Info> out;
    for (int i = 0; i < numFactory(); ++i)
        out.add ({ factoryTable()[(size_t) i].name, factoryTable()[(size_t) i].category, true, i, {} });
    for (auto& cat : categories())
        for (auto& f : getUserFolder().getChildFile (cat).findChildFiles (juce::File::findFiles, false, "*.ghostpreset"))
            out.add ({ f.getFileNameWithoutExtension(), cat, false, -1, f });
    return out;
}

juce::ValueTree PresetManager::capture() const
{
    juce::ValueTree t ("GHOSTCUT_PRESET");
    t.setProperty ("version", 1, nullptr);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            setParam (t, rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    t.appendChild (session.getGenSettings().toTree(), nullptr);
    return t;
}

void PresetManager::apply (const juce::ValueTree& t)
{
    // every parameter: the preset's value, or its default if the preset doesn't mention it
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float norm = rp->getDefaultValue();
            for (auto c : t)
                if (c.hasType ("P") && c["id"].toString() == rp->getParameterID())
                    norm = rp->convertTo0to1 ((float) c["v"]);
            rp->setValueNotifyingHost (norm);
        }
    const auto g = t.getChildWithName ("GEN");
    if (g.isValid())
    {
        auto gs = GenSettings::fromTree (g);
        gs.seed = session.getGenSettings().seed;  // a sound preset never changes your seed
        session.setGenSettings (gs);
    }
}

bool PresetManager::loadFactory (int index)
{
    if (index < 0 || index >= numFactory()) return false;
    const auto& f = factoryTable()[(size_t) index];
    juce::ValueTree t ("GHOSTCUT_PRESET");
    for (auto& [id, v] : fxp::modePresetValues (f.mode)) setParam (t, id, v);
    for (auto& [id, v] : f.overrides) setParam (t, id, v);
    GenSettings g;
    g.mode = f.mode;
    if (f.gen) f.gen (g);
    t.appendChild (g.toTree(), nullptr);
    apply (t);
    setCurrent (f.name, "factory:" + juce::String (f.category) + "/" + f.name);
    return true;
}

bool PresetManager::load (const Info& info, juce::String& error)
{
    if (info.factory) return loadFactory (info.factoryIndex);
    auto xml = juce::XmlDocument::parse (info.file);
    if (xml == nullptr) { error = "Could not read " + info.file.getFileName(); return false; }
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("GHOSTCUT_PRESET")) { error = info.file.getFileName() + " is not a GHOSTCUT preset"; return false; }
    apply (t);
    setCurrent (info.name, info.id());
    return true;
}

bool PresetManager::saveUser (const juce::String& rawName, const juce::String& category, juce::String& error)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) { error = "Give the preset a name."; return false; }
    const auto cat = categories().contains (category) ? category : juce::String ("User");
    auto dir = getUserFolder().getChildFile (cat);
    if (! dir.createDirectory()) { error = "Can't create " + dir.getFullPathName(); return false; }
    auto file = dir.getChildFile (name + ".ghostpreset");
    auto t = capture();
    t.setProperty ("name", name, nullptr);
    t.setProperty ("category", cat, nullptr);
    if (auto xml = t.createXml(); xml == nullptr || ! xml->writeTo (file)) { error = "Write failed: " + file.getFullPathName(); return false; }
    setCurrent (name, "user:" + cat + "/" + name);
    return true;
}

bool PresetManager::deleteUser (const Info& info, juce::String& error)
{
    if (info.factory) { error = "Factory presets can't be deleted."; return false; }
    if (! info.file.deleteFile()) { error = "Could not delete " + info.file.getFileName(); return false; }
    if (currentId == info.id()) setCurrent (currentName + " (deleted)", {});
    sendChangeMessage();
    return true;
}

void PresetManager::step (int delta)
{
    auto all = list();
    if (all.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < all.size(); ++i) if (all[i].id() == currentId) idx = i;
    idx = ((idx < 0 ? (delta > 0 ? -1 : 0) : idx) + delta + all.size()) % all.size();
    juce::String err;
    load (all[idx], err);
}

} // namespace ghostcut
