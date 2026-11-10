#include "Session.h"
#include <numeric>

namespace ghostcut
{

namespace ids
{
    static const juce::Identifier source { "SOURCE" }, fragmentsNode { "FRAGMENTS" }, frag { "F" };
    static const juce::Identifier path { "path" }, name { "name" }, size { "size" }, hash { "hash" };
    static const juce::Identifier trimStart { "trimStart" }, trimEnd { "trimEnd" }, sampleRate { "sampleRate" }, numSamples { "numSamples" };
    static const juce::Identifier uid { "uid" }, start { "start" }, end { "end" }, manual { "manual" };
    static const juce::Identifier detected { "detected" }, rootOverride { "root" };
    static const juce::Identifier f0 { "f0" }, voiced { "voiced" }, stab { "stab" }, meanDb { "meanDb" }, peakDb { "peakDb" }, cent { "cent" };
    static const juce::Identifier selected { "selected" }, selStart { "selStart" }, selEnd { "selEnd" }, loop { "loop" };
    static const juce::Identifier sensitivity { "sensitivity" }, exportBits { "exportBits" }, version { "version" };
    static const juce::Identifier view { "view" }, granularity { "granularity" }, sort { "sort" }, search { "search" };
    static const juce::Identifier vocalsMethod { "vocalsMethod" }, sepMethod { "sepMethod" }, sepModel { "sepModel" };
    static const juce::Identifier patterns { "PATTERNS" }, patternSlot { "slot" }, pinned { "pinned" };
}

static VocalSeparator::Options defaultSeparationOptions()
{
    VocalSeparator::Options o;
    o.threads = std::max (1, juce::SystemStats::getNumCpus() / 2); // leave headroom for the DAW
    const auto models = VocalModels::scan();
    if (! models.isEmpty()) { o.method = VocalSeparator::Method::MdxModel; o.model = models.getReference (0); }
    else o.method = VocalSeparator::Method::CenterExtract;
    return o;
}

Session::Session (SamplerEngine& e) : engine (e), sepOptions (defaultSeparationOptions())
{
    updateStateCache();
    startTimerHz (2); // tempo watch for fit-to-gate patterns
}

Session::~Session()
{
    stopTimer();
    alive->store (false);
    generation.fetch_add (1);                 // makes running jobs bail out
    worker.removeAllJobs (true, 10000);
}

//==============================================================================
template <typename Fn>
void Session::runJob (const juce::String& name, Fn&& fn)
{
    worker.addJob ([fn = std::forward<Fn> (fn)]() mutable { fn(); });
    juce::ignoreUnused (name);
}

void Session::setStatus (const juce::String& s, bool error)
{
    status = s;
    statusIsError = error;
    sendChangeMessage();
}

//==============================================================================
void Session::importFile (const juce::File& f)
{
    missingSource = juce::File();
    startLoad (f, 0, -1, true, {}, {});
}

void Session::relocateMissingSource (const juce::File& f)
{
    auto keep = fragments;
    const auto s = missingTrimStart, e = missingTrimEnd;
    const auto h = missingHash;
    missingSource = juce::File();
    startLoad (f, s, e, false, std::move (keep), h);
}

void Session::startLoad (const juce::File& f, int64_t trimStart, int64_t trimEnd, bool autoAnalyze,
                         std::vector<Fragment> keepFragments, const juce::String& expectedHash)
{
    if (! importer.canImport (f))
    {
        setStatus ("Can't import " + f.getFileName() + " (WAV, MP3, AIFF or FLAC only)", true);
        return;
    }

    const int jobGen = generation.fetch_add (1) + 1;
    busy = true;
    progress = 0.0f;
    engine.sendCommand ({ EngineCommand::Type::StopAll });
    setStatus ("Loading " + f.getFileName() + "...");

    auto aliveFlag = alive;
    runJob ("load", [this, aliveFlag, jobGen, f, trimStart, trimEnd, autoAnalyze,
                     keep = std::move (keepFragments), expectedHash]() mutable
    {
        auto keepGoing = [this, aliveFlag, jobGen] { return aliveFlag->load() && generation.load() == jobGen; };
        auto result = importer.load (f, trimStart, trimEnd, [this] (float p) { progress = p; }, keepGoing);

        juce::MessageManager::callAsync ([this, aliveFlag, jobGen, result, autoAnalyze, keep = std::move (keep), expectedHash]() mutable
        {
            if (! aliveFlag->load() || generation.load() != jobGen) return;
            busy = false;

            if (result.snapshot == nullptr)
            {
                setStatus (result.error, true);
                return;
            }

            snapshot = result.snapshot;
            vocals.reset();
            vocalsLabel.clear();
            vocalsMethodId.clear();
            if (autoAnalyze) { view = View::Original; pendingVocalsMethod.clear(); }
            frames.reset();
            publishPlayback();

            const auto n = snapshot->numSamples();
            if (autoAnalyze)
            {
                fragments.clear();
                selectedIndex = -1;
                selStart = selEnd = 0;
            }
            else
            {
                // restored project: keep fragments, clamp them to the audio we actually have
                fragments = std::move (keep);
                for (auto& fr : fragments) { fr.start = juce::jlimit<int64_t> (0, n, fr.start); fr.end = juce::jlimit<int64_t> (fr.start, n, fr.end); }
                fragments.erase (std::remove_if (fragments.begin(), fragments.end(), [] (const Fragment& fr) { return fr.length() < 16; }), fragments.end());
                for (auto& fr : fragments) uidCounter = std::max (uidCounter, fr.uid);
                selectedIndex = juce::jlimit (-1, (int) fragments.size() - 1, selectedIndex);
            }
            publishTable();
            updateStateCache();

            juce::String msg = snapshot->displayName + " loaded";
            const bool changed = expectedHash.isNotEmpty() && expectedHash != snapshot->contentHash;
            if (changed) msg = snapshot->displayName + " loaded, but the file differs from the one saved in this project. Fragment positions may be off.";
            setStatus (msg, changed);

            if (! autoAnalyze && pendingVocalsMethod.isNotEmpty()) startSeparation (true);
            else startFrameAnalysis (autoAnalyze);
        });
    });
}

void Session::analyze()
{
    if (snapshot == nullptr) { setStatus ("Import audio first", true); return; }
    if (frames != nullptr)
    {
        // frames are cached: re-segmenting is instant and needs no worker
        PhraseSegmenter::Settings s;
        s.sensitivity = sensitivity;
        s.granularity = granularity;
        auto found = PhraseSegmenter::segment (*analysisSnapshot(), *frames, s);
        std::vector<Fragment> manual;
        for (auto& f : fragments) if (f.manual) manual.push_back (f);
        fragments = std::move (found);
        for (auto& f : fragments) f.uid = nextUid();
        for (auto& f : manual) fragments.push_back (f);
        selectedIndex = fragments.empty() ? -1 : 0;
        sortAndPublish (fragments.empty() ? -1 : fragments.front().uid);
        setStatus (juce::String (fragments.size()) + " fragments found" + (vocals != nullptr ? " in isolated vocals" : ""));
        return;
    }
    startFrameAnalysis (true);
}

void Session::startFrameAnalysis (bool segmentAfter)
{
    if (snapshot == nullptr) return;
    const int jobGen = generation.load();
    busy = true;
    progress = 0.0f;
    setStatus (segmentAfter ? "Analyzing phrases..." : "Measuring audio...");

    auto snap = analysisSnapshot();
    auto aliveFlag = alive;
    runJob ("frames", [this, aliveFlag, jobGen, snap, segmentAfter]
    {
        auto keepGoing = [this, aliveFlag, jobGen] { return aliveFlag->load() && generation.load() == jobGen; };
        auto ff = std::make_shared<FrameFeatures> (PhraseSegmenter::computeFrames (*snap, [this] (float p) { progress = p; }, keepGoing));

        juce::MessageManager::callAsync ([this, aliveFlag, jobGen, ff, segmentAfter]
        {
            if (! aliveFlag->load() || generation.load() != jobGen) return;
            busy = false;
            if (ff->numFrames() == 0) { setStatus ("Analysis cancelled", true); return; }
            frames = ff;

            // (re)measure everything against the current analysis audio
            for (auto& f : fragments) PhraseSegmenter::measure (f, *analysisSnapshot(), *frames);

            if (segmentAfter) analyze();
            else { publishTable(); updateStateCache(); setStatus (snapshot->displayName + " ready"); }
            if (hasPattern()) renderPatternAsync();
            sendChangeMessage();
        });
    });
}

void Session::trimToSelection()
{
    if (snapshot == nullptr || ! hasSelection()) return;
    auto cropped = AudioImporter::crop (*snapshot, selStart, selEnd);
    if (cropped == nullptr) { setStatus ("Selection too short to trim", true); return; }
    std::shared_ptr<SourceSnapshot> croppedVocals;
    if (vocals != nullptr)
    {
        croppedVocals = AudioImporter::crop (*vocals, selStart, selEnd);
        if (croppedVocals != nullptr) croppedVocals->displayName = vocals->displayName;
    }

    // shift fragments into the new coordinates; drop those that fall outside
    const int64_t off = selStart, len = selEnd - selStart;
    std::vector<Fragment> kept;
    for (auto f : fragments)
    {
        f.start -= off; f.end -= off;
        f.start = std::max<int64_t> (0, f.start);
        f.end = std::min<int64_t> (len, f.end);
        if (f.length() >= 16) kept.push_back (f);
    }
    engine.sendCommand ({ EngineCommand::Type::StopAll });
    generation.fetch_add (1);
    snapshot = cropped;
    vocals = croppedVocals;
    if (vocals == nullptr) { view = View::Original; vocalsMethodId.clear(); }
    frames.reset();
    fragments = std::move (kept);
    selStart = selEnd = 0;
    publishPlayback();
    sortAndPublish (-1);
    setStatus ("Trimmed to " + juce::String (snapshot->lengthSeconds(), 2) + " s");
    startFrameAnalysis (false);
}

//==============================================================================
void Session::selectFragment (int index)
{
    selectedIndex = juce::jlimit (-1, (int) fragments.size() - 1, index);
    engine.setSelectedFragment (selectedIndex);
    updateStateCache();
    sendChangeMessage();
}

void Session::setFragmentBounds (int index, int64_t start, int64_t end)
{
    if (snapshot == nullptr || index < 0 || index >= (int) fragments.size()) return;
    const int64_t n = snapshot->numSamples();
    const int64_t minLen = (int64_t) (0.02 * snapshot->sampleRate);
    auto& f = fragments[(size_t) index];
    f.start = juce::jlimit<int64_t> (0, n - minLen, start);
    f.end = juce::jlimit<int64_t> (f.start + minLen, n, end);
    if (frames != nullptr) PhraseSegmenter::measure (f, *analysisSnapshot(), *frames);
    sortAndPublish (f.uid);
}

void Session::setFragmentRootOverride (int index, int note)
{
    if (index < 0 || index >= (int) fragments.size()) return;
    fragments[(size_t) index].rootOverride = note;
    publishTable();
    updateStateCache();
    sendChangeMessage();
}

void Session::deleteFragment (int index)
{
    if (index < 0 || index >= (int) fragments.size()) return;
    fragments.erase (fragments.begin() + index);
    const int64_t keep = fragments.empty() ? -1 : fragments[(size_t) juce::jmin (index, (int) fragments.size() - 1)].uid;
    sortAndPublish (keep);
}

void Session::clearFragments()
{
    fragments.clear();
    sortAndPublish (-1);
}

int Session::addFragmentFromSelection()
{
    if (snapshot == nullptr || ! hasSelection()) return -1;
    Fragment f;
    f.uid = nextUid();
    f.manual = true;
    f.start = selStart;
    f.end = selEnd;
    if (frames != nullptr) PhraseSegmenter::measure (f, *analysisSnapshot(), *frames);
    fragments.push_back (f);
    sortAndPublish (f.uid);
    return selectedIndex;
}

void Session::auditionFragment (int index)
{
    if (index < 0 || index >= (int) fragments.size()) return;
    EngineCommand c { EngineCommand::Type::AuditionFragment, index };
    c.loop = loop;
    engine.sendCommand (c);
}

void Session::sortAndPublish (int64_t keepSelectedUid)
{
    std::sort (fragments.begin(), fragments.end(), [] (const Fragment& a, const Fragment& b) { return a.start < b.start; });
    selectedIndex = -1;
    for (int i = 0; i < (int) fragments.size(); ++i)
        if (fragments[(size_t) i].uid == keepSelectedUid) selectedIndex = i;
    engine.setSelectedFragment (selectedIndex);
    publishTable();
    updateStateCache();
    sendChangeMessage();
    if (hasPattern() && ! busy) renderPatternAsync();
}

void Session::publishTable()
{
    auto t = std::make_shared<FragmentTable>();
    t->fragments.reserve (fragments.size());
    for (auto& f : fragments) t->fragments.push_back ({ f.start, f.end, f.effectiveRoot (-1) });
    engine.publishFragments (std::move (t));
    engine.setSelectedFragment (selectedIndex);
}

//==============================================================================
void Session::setSelection (int64_t s, int64_t e)
{
    if (e < s) std::swap (s, e);
    if (snapshot != nullptr) { s = juce::jlimit<int64_t> (0, snapshot->numSamples(), s); e = juce::jlimit<int64_t> (0, snapshot->numSamples(), e); }
    selStart = s; selEnd = e;
    updateStateCache();
    sendChangeMessage();
}

void Session::setLooping (bool l) { loop = l; updateStateCache(); sendChangeMessage(); }

void Session::playFrom (int64_t position)
{
    if (snapshot == nullptr) return;
    EngineCommand c { EngineCommand::Type::PlaySource };
    if (hasSelection() && (loop || (position >= selStart && position < selEnd)))
    {
        c.a = (position >= selStart && position < selEnd) ? position : selStart;
        c.b = selEnd;
        if (loop) c.a = selStart;
    }
    else
    {
        c.a = position;
        c.b = snapshot->numSamples();
    }
    c.loop = loop && hasSelection();
    engine.sendCommand (c);
}

void Session::togglePlay (int64_t cursor)
{
    if (engine.isPreviewPlaying()) stop();
    else playFrom (cursor);
}

void Session::stop() { engine.sendCommand ({ EngineCommand::Type::StopAll }); }

//==============================================================================
juce::String Session::exportFragment (int index, const juce::File& dest) const
{
    auto snap = getSnapshot();
    if (snap == nullptr) return "No audio loaded";
    if (index < 0 || index >= (int) fragments.size()) return "No fragment selected";
    const auto& f = fragments[(size_t) index];
    return AudioExporter::writeRegion (*snap, f.start, f.end, dest, exportDepth);
}

juce::File Session::renderFragmentForDrag (int index, juce::String& error) const
{
    auto snap = getSnapshot();
    if (snap == nullptr || index < 0 || index >= (int) fragments.size()) { error = "No fragment"; return {}; }
    const auto& f = fragments[(size_t) index];
    auto name = AudioExporter::makeFragmentFileName (*snapshot, index + 1, f.start);
    if (view == View::Vocals && vocals != nullptr) name = name.replace (".wav", "_vocals.wav");
    auto dest = AudioExporter::getDragExportFolder().getChildFile (name);
    error = AudioExporter::writeRegion (*snap, f.start, f.end, dest, exportDepth);
    return error.isEmpty() ? dest : juce::File();
}

//==============================================================================
void Session::updateStateCache()
{
    juce::ValueTree t (stateType);
    t.setProperty (ids::version, 1, nullptr);
    t.setProperty (ids::sensitivity, sensitivity, nullptr);
    t.setProperty (ids::exportBits, (int) exportDepth, nullptr);
    t.setProperty (ids::selected, selectedIndex, nullptr);
    t.setProperty (ids::selStart, (juce::int64) selStart, nullptr);
    t.setProperty (ids::selEnd, (juce::int64) selEnd, nullptr);
    t.setProperty (ids::loop, loop, nullptr);
    t.setProperty (ids::view, (int) view, nullptr);
    t.setProperty (ids::granularity, (int) granularity, nullptr);
    t.setProperty (ids::sort, (int) sort, nullptr);
    t.setProperty (ids::search, searchText, nullptr);
    t.setProperty (ids::vocalsMethod, vocals != nullptr ? vocalsMethodId : pendingVocalsMethod, nullptr);
    t.setProperty (ids::sepMethod, (int) sepOptions.method, nullptr);
    t.setProperty (ids::sepModel, sepOptions.model.file.getFileName(), nullptr);
    {
        juce::StringArray pins;
        for (auto u : pinned) pins.add (juce::String (u));
        t.setProperty (ids::pinned, pins.joinIntoString (","), nullptr);
        t.appendChild (gen.toTree(), nullptr);
        juce::ValueTree pats (ids::patterns);
        pats.setProperty (ids::patternSlot, patternSlot, nullptr);
        for (int i = 0; i < numPatternSlots; ++i)
            pats.appendChild (i == patternSlot ? pattern.toTree() : bank[(size_t) i].toTree(), nullptr);
        t.appendChild (pats, nullptr);
    }

    juce::ValueTree src (ids::source);
    if (snapshot != nullptr)
    {
        src.setProperty (ids::path, snapshot->file.getFullPathName(), nullptr);
        src.setProperty (ids::name, snapshot->displayName, nullptr);
        src.setProperty (ids::size, snapshot->fileSize, nullptr);
        src.setProperty (ids::hash, snapshot->contentHash, nullptr);
        src.setProperty (ids::trimStart, (juce::int64) snapshot->trimStart, nullptr);
        src.setProperty (ids::trimEnd, (juce::int64) snapshot->trimEnd, nullptr);
        src.setProperty (ids::sampleRate, snapshot->sampleRate, nullptr);
        src.setProperty (ids::numSamples, (juce::int64) snapshot->numSamples(), nullptr);
    }
    else if (missingSource != juce::File())
    {
        // keep the reference even though the file isn't here, so saving doesn't lose it
        src.setProperty (ids::path, missingSource.getFullPathName(), nullptr);
        src.setProperty (ids::name, missingSource.getFileName(), nullptr);
        src.setProperty (ids::hash, missingHash, nullptr);
        src.setProperty (ids::trimStart, (juce::int64) missingTrimStart, nullptr);
        src.setProperty (ids::trimEnd, (juce::int64) missingTrimEnd, nullptr);
    }
    t.appendChild (src, nullptr);

    juce::ValueTree frs (ids::fragmentsNode);
    for (auto& f : fragments)
    {
        juce::ValueTree n (ids::frag);
        n.setProperty (ids::uid, (juce::int64) f.uid, nullptr);
        n.setProperty (ids::start, (juce::int64) f.start, nullptr);
        n.setProperty (ids::end, (juce::int64) f.end, nullptr);
        n.setProperty (ids::manual, f.manual, nullptr);
        n.setProperty (ids::detected, f.detectedNote, nullptr);
        n.setProperty (ids::rootOverride, f.rootOverride, nullptr);
        n.setProperty (ids::f0, f.features.medianF0Hz, nullptr);
        n.setProperty (ids::voiced, f.features.voicedRatio, nullptr);
        n.setProperty (ids::stab, f.features.pitchStabilitySemis, nullptr);
        n.setProperty (ids::meanDb, f.features.meanDb, nullptr);
        n.setProperty (ids::peakDb, f.features.peakDb, nullptr);
        n.setProperty (ids::cent, f.features.centroidHz, nullptr);
        frs.appendChild (n, nullptr);
    }
    t.appendChild (frs, nullptr);

    const juce::ScopedLock sl (cacheLock);
    stateCache = t;
}

juce::ValueTree Session::getStateTree() const
{
    const juce::ScopedLock sl (cacheLock);
    return stateCache.createCopy();
}

void Session::setStateTreeFromAnyThread (const juce::ValueTree& tree)
{
    if (! tree.hasType (stateType)) return;
    auto copy = tree.createCopy();
    {
        // a host may call getState right after setState: answer with what it gave us
        const juce::ScopedLock sl (cacheLock);
        stateCache = copy;
    }

    auto aliveFlag = alive;
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        restoreOnMessageThread (copy);
    else
        juce::MessageManager::callAsync ([this, aliveFlag, copy] { if (aliveFlag->load()) restoreOnMessageThread (copy); });
}

void Session::restoreOnMessageThread (const juce::ValueTree& t)
{
    sensitivity = (float) t.getProperty (ids::sensitivity, 0.5f);
    const int bits = (int) t.getProperty (ids::exportBits, 24);
    exportDepth = bits == 16 ? AudioExporter::BitDepth::Int16 : bits == 32 ? AudioExporter::BitDepth::Float32 : AudioExporter::BitDepth::Int24;
    loop = (bool) t.getProperty (ids::loop, false);
    selStart = (juce::int64) t.getProperty (ids::selStart, 0);
    selEnd = (juce::int64) t.getProperty (ids::selEnd, 0);
    view = (View) (int) t.getProperty (ids::view, 0);
    granularity = (Granularity) (int) t.getProperty (ids::granularity, 0);
    sort = (Sort) juce::jlimit (0, 6, (int) t.getProperty (ids::sort, 0));
    searchText = t.getProperty (ids::search).toString();
    search = SearchQuery::parse (searchText);
    pendingVocalsMethod = t.getProperty (ids::vocalsMethod).toString();
    {
        pinned.clear();
        juce::StringArray pins;
        pins.addTokens (t.getProperty (ids::pinned).toString(), ",", "");
        pins.removeEmptyStrings();
        for (auto& pi : pins) pinned.push_back (pi.getLargeIntValue());
        gen = GenSettings::fromTree (t.getChildWithName ("GEN"));
        const auto pats = t.getChildWithName (ids::patterns);
        for (auto& b : bank) b = Pattern();
        int i = 0;
        for (auto pt : pats) if (i < numPatternSlots) bank[(size_t) i++] = Pattern::fromTree (pt);
        patternSlot = juce::jlimit (0, numPatternSlots - 1, (int) pats.getProperty (ids::patternSlot, 0));
        pattern = bank[(size_t) patternSlot];
        rendered.reset();
        patternGeneration.fetch_add (1);
        engine.publishPattern (nullptr);
    }
    {
        const auto m = (VocalSeparator::Method) (int) t.getProperty (ids::sepMethod, (int) sepOptions.method);
        const auto modelName = t.getProperty (ids::sepModel).toString();
        if (m == VocalSeparator::Method::MdxModel)
        {
            for (auto& info : VocalModels::scan())
                if (info.file.getFileName() == modelName) { sepOptions.method = m; sepOptions.model = info; }
        }
        else sepOptions.method = m;
    }

    std::vector<Fragment> restored;
    for (auto n : t.getChildWithName (ids::fragmentsNode))
    {
        Fragment f;
        f.uid = (juce::int64) n.getProperty (ids::uid, 0);
        f.start = (juce::int64) n.getProperty (ids::start, 0);
        f.end = (juce::int64) n.getProperty (ids::end, 0);
        f.manual = (bool) n.getProperty (ids::manual, false);
        f.detectedNote = (int) n.getProperty (ids::detected, -1);
        f.rootOverride = (int) n.getProperty (ids::rootOverride, -1);
        f.features.medianF0Hz = (float) n.getProperty (ids::f0, 0.0f);
        f.features.voicedRatio = (float) n.getProperty (ids::voiced, 0.0f);
        f.features.pitchStabilitySemis = (float) n.getProperty (ids::stab, 0.0f);
        f.features.meanDb = (float) n.getProperty (ids::meanDb, -100.0f);
        f.features.peakDb = (float) n.getProperty (ids::peakDb, -100.0f);
        f.features.centroidHz = (float) n.getProperty (ids::cent, 0.0f);
        if (f.uid == 0) f.uid = nextUid();
        uidCounter = std::max (uidCounter, f.uid);
        if (f.end > f.start) restored.push_back (f);
    }
    selectedIndex = (int) t.getProperty (ids::selected, -1);

    const auto src = t.getChildWithName (ids::source);
    const juce::String path = src.getProperty (ids::path).toString();
    const int64_t trimS = (juce::int64) src.getProperty (ids::trimStart, 0);
    const int64_t trimE = (juce::int64) src.getProperty (ids::trimEnd, -1);
    const juce::String hash = src.getProperty (ids::hash).toString();

    // drop whatever is loaded now
    generation.fetch_add (1);
    snapshot.reset();
    vocals.reset();
    frames.reset();
    engine.publishSource (nullptr);
    fragments = restored;
    publishTable();

    if (path.isEmpty())
    {
        missingSource = juce::File();
        updateStateCache();
        setStatus ("Drop a WAV or MP3 to begin");
        return;
    }

    const juce::File file (path);
    if (! file.existsAsFile())
    {
        missingSource = file;
        missingTrimStart = trimS; missingTrimEnd = trimE; missingHash = hash;
        updateStateCache();
        setStatus ("Source missing: " + file.getFileName() + " - click LOCATE to find it. Fragments are kept.", true);
        return;
    }

    missingSource = juce::File();
    startLoad (file, trimS, trimE, false, std::move (restored), hash);
}

//==============================================================================
void Session::publishPlayback()
{
    engine.sendCommand ({ EngineCommand::Type::StopAll });
    engine.publishSource (getSnapshot());
}

void Session::setView (View v)
{
    if (v == View::Vocals && vocals == nullptr) return;
    view = v;
    publishPlayback();
    if (hasPattern()) renderPatternAsync(); // chops come from what you hear
    updateStateCache();
    sendChangeMessage();
}

void Session::separate()
{
    if (snapshot == nullptr) { setStatus ("Import audio first", true); return; }
    if (busy) return;
    startSeparation (false);
}

void Session::startSeparation (bool restoring)
{
    if (snapshot == nullptr) return;
    auto opts = sepOptions;
    if (opts.method == VocalSeparator::Method::None) { startFrameAnalysis (! restoring); return; }

    const int jobGen = generation.load();
    busy = true;
    progress = 0.0f;
    setStatus ("Separating vocals: " + VocalSeparator::methodLabel (opts) + "...");

    auto src = snapshot;
    auto aliveFlag = alive;
    runJob ("separate", [this, aliveFlag, jobGen, src, opts, restoring]
    {
        auto keepGoing = [this, aliveFlag, jobGen] { return aliveFlag->load() && generation.load() == jobGen; };
        auto result = VocalSeparator::separate (*src, opts, [this] (float p) { progress = p; }, keepGoing);

        juce::MessageManager::callAsync ([this, aliveFlag, jobGen, result, opts, restoring]
        {
            if (! aliveFlag->load() || generation.load() != jobGen) return;
            busy = false;
            if (result.vocals == nullptr)
            {
                if (restoring) view = View::Original;
                setStatus ("Separation failed: " + result.error, true);
                if (restoring) startFrameAnalysis (false);
                return;
            }
            vocals = result.vocals;
            vocalsMethodId = VocalSeparator::methodId (opts);
            vocalsLabel = VocalSeparator::methodLabel (opts);
            pendingVocalsMethod.clear();
            if (! restoring) view = View::Vocals;
            publishPlayback();
            frames.reset();
            updateStateCache();
            setStatus ("Vocals isolated with " + vocalsLabel + (result.fromCache ? " (cached)" : ""));
            // re-analyse on the isolated vocals: fresh separation re-detects fragments
            startFrameAnalysis (! restoring);
        });
    });
}

//==============================================================================
void Session::setSort (Sort s)
{
    sort = s;
    updateStateCache();
    sendChangeMessage();
}

void Session::setSearchText (const juce::String& text)
{
    searchText = text.trim();
    search = SearchQuery::parse (searchText);
    if (! search.isEmpty()) sort = Sort::Match;
    else if (sort == Sort::Match) sort = Sort::Time;
    updateStateCache();
    sendChangeMessage();
}

float Session::sortValue (int index) const
{
    if (index < 0 || index >= (int) fragments.size()) return 0.0f;
    const auto& f = fragments[(size_t) index];
    if (sort == Sort::Time)  return 0.0f;
    if (sort == Sort::Match) return search.match (f);
    return f.scores[(int) sort - 1];
}

std::vector<int> Session::getDisplayOrder() const
{
    std::vector<int> order ((size_t) fragments.size());
    std::iota (order.begin(), order.end(), 0);
    if (sort != Sort::Time)
        std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return sortValue (a) > sortValue (b); });
    return order;
}

//==============================================================================
// Generation
//==============================================================================
std::vector<GenSlot> Session::currentGenSlots() const
{
    if (! pinned.empty())
    {
        std::vector<GenSlot> out;
        for (auto uid : pinned)
            for (auto& f : fragments)
                if (f.uid == uid)
                    out.push_back ({ f.uid, f.effectiveRoot (60), f.features.durationSec, PatternGenerator::modeScore (f, gen.mode) });
        if (! out.empty()) return out;
    }
    return PatternGenerator::pickSlots (fragments, gen.mode, 6);
}

void Session::generate (bool newSeed)
{
    if (snapshot == nullptr || fragments.empty()) { setStatus ("Discover some fragments first", true); return; }
    if (newSeed)
    {
        // fresh seed from the clock, but only 6 digits so it's easy to note down
        gen.seed = (uint32_t) (juce::Time::getHighResolutionTicks() % 1000000);
    }
    pattern = PatternGenerator::generate (gen, currentGenSlots(), getBpm());
    bank[(size_t) patternSlot] = pattern;
    updateStateCache();
    setStatus (juce::String (genModeName (gen.mode)) + " pattern, seed " + juce::String ((int) gen.seed)
               + ", " + juce::String (pattern.numEvents()) + " hits");
    renderPatternAsync();
}

void Session::setPattern (const Pattern& p)
{
    pattern = p;
    bank[(size_t) patternSlot] = pattern;
    updateStateCache();
    sendChangeMessage();
    renderPatternAsync();
}

void Session::selectPatternSlot (int slot)
{
    slot = juce::jlimit (0, numPatternSlots - 1, slot);
    bank[(size_t) patternSlot] = pattern;
    patternSlot = slot;
    pattern = bank[(size_t) slot];
    updateStateCache();
    sendChangeMessage();
    if (hasPattern()) renderPatternAsync();
    else { rendered.reset(); engine.publishPattern (nullptr); }
}

void Session::togglePin (int index)
{
    if (index < 0 || index >= (int) fragments.size()) return;
    const auto uid = fragments[(size_t) index].uid;
    auto it = std::find (pinned.begin(), pinned.end(), uid);
    if (it != pinned.end()) pinned.erase (it);
    else if (pinned.size() < 8) pinned.push_back (uid);
    updateStateCache();
    sendChangeMessage();
}

bool Session::isPinned (int index) const
{
    if (index < 0 || index >= (int) fragments.size()) return false;
    return std::find (pinned.begin(), pinned.end(), fragments[(size_t) index].uid) != pinned.end();
}

int Session::fragmentIndexForSlot (int slot) const
{
    if (slot < 0 || slot >= (int) pattern.slotUids.size()) return -1;
    for (int i = 0; i < (int) fragments.size(); ++i)
        if (fragments[(size_t) i].uid == pattern.slotUids[(size_t) slot]) return i;
    return -1;
}

void Session::setPatternPlaying (bool run)
{
    engine.sendCommand ({ run ? EngineCommand::Type::SeqStart : EngineCommand::Type::SeqStop });
}

void Session::renderPatternAsync()
{
    auto snap = getSnapshot();
    if (snap == nullptr || ! hasPattern()) return;

    std::vector<std::pair<int64_t, int64_t>> bounds;
    for (auto uid : pattern.slotUids)
    {
        std::pair<int64_t, int64_t> b { 0, 0 };
        for (auto& f : fragments) if (f.uid == uid) b = { f.start, f.end };
        bounds.push_back (b);
    }

    const int pgen = patternGeneration.fetch_add (1) + 1;
    const double bpm = getBpm();
    renderingPattern = true;
    auto pat = pattern;
    auto aliveFlag = alive;
    runJob ("pattern", [this, aliveFlag, pgen, snap, pat, bounds, bpm]
    {
        auto keepGoing = [this, aliveFlag, pgen] { return aliveFlag->load() && patternGeneration.load() == pgen; };
        auto rp = renderPattern (pat, *snap, bounds, bpm, keepGoing);
        juce::MessageManager::callAsync ([this, aliveFlag, pgen, rp, bpm]
        {
            if (! aliveFlag->load() || patternGeneration.load() != pgen) return;
            renderingPattern = false;
            if (rp == nullptr) return;
            rendered = rp;
            renderedBpm = bpm;
            engine.publishPattern (rp);
            sendChangeMessage();
        });
    });
    sendChangeMessage();
}

void Session::timerCallback()
{
    if (! hasPattern() || renderingPattern || rendered == nullptr) return;
    bool needsTempo = false;
    for (int i = 0; i < pattern.length; ++i)
        if (pattern.active[(size_t) i] && pattern.events[(size_t) i].fitToGate) needsTempo = true;
    if (needsTempo && std::abs (getBpm() - renderedBpm) > 0.5)
        renderPatternAsync();
}

static juce::String writeWav (const juce::AudioBuffer<float>& audio, double sr, const juce::File& dest, AudioExporter::BitDepth depth)
{
    SourceSnapshot tmp;
    tmp.audio = audio;
    tmp.sampleRate = sr;
    return AudioExporter::writeRegion (tmp, 0, audio.getNumSamples(), dest, depth);
}

juce::String Session::exportLoop (const juce::File& dest, int loops) const
{
    if (rendered == nullptr) return "Generate a pattern first.";
    auto audio = PatternExport::renderLoop (*rendered, renderedBpm > 0 ? renderedBpm : getBpm(), loops, true);
    return writeWav (audio, rendered->sampleRate, dest, exportDepth);
}

juce::String Session::exportMidi (const juce::File& dest) const
{
    if (! hasPattern()) return "Generate a pattern first.";
    auto mf = PatternExport::toMidi (pattern, getBpm());
    dest.deleteFile();
    juce::FileOutputStream os (dest);
    if (! os.openedOk()) return "Cannot write " + dest.getFullPathName();
    return mf.writeTo (os, 1) ? juce::String() : juce::String ("MIDI write failed");
}

static juce::String patternFileStem (const GenSettings& g, double bpm)
{
    return "GHOSTCUT_" + juce::String (genModeName (g.mode)) + "_" + ScaleQuantizer::keyNames()[g.key]
           + juce::String (g.scale == ScaleQuantizer::Scale::Major || g.scale == ScaleQuantizer::Scale::PentatonicMajor ? "maj" : "min")
           + "_" + juce::String ((int) std::lround (bpm)) + "bpm_seed" + juce::String ((int) g.seed);
}

juce::File Session::renderLoopForDrag (juce::String& error) const
{
    auto f = AudioExporter::getDragExportFolder().getSiblingFile ("Loops").getChildFile (patternFileStem (pattern.madeWith, getBpm()) + ".wav");
    f.getParentDirectory().createDirectory();
    error = exportLoop (f, 1);
    return error.isEmpty() ? f : juce::File();
}

juce::File Session::renderMidiForDrag (juce::String& error) const
{
    auto f = AudioExporter::getDragExportFolder().getSiblingFile ("MIDI").getChildFile (patternFileStem (pattern.madeWith, getBpm()) + ".mid");
    f.getParentDirectory().createDirectory();
    error = exportMidi (f);
    return error.isEmpty() ? f : juce::File();
}

} // namespace ghostcut
