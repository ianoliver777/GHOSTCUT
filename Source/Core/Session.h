#pragma once

#include "Fragment.h"
#include "SourceSnapshot.h"
#include "../Analysis/PhraseSegmenter.h"
#include "../Analysis/VocalSeparator.h"
#include "../Analysis/SearchQuery.h"
#include "../Generation/PatternGenerator.h"
#include "../Generation/PatternExport.h"
#include "../Audio/AudioImporter.h"
#include "../Audio/AudioExporter.h"
#include "../Audio/SamplerEngine.h"
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ghostcut
{

/**
    The document model: current source, fragments, selection and settings.

    Threading rules
      - All mutation happens on the message thread.
      - Heavy work (decode, analysis, trim) runs on a private worker thread and
        posts its result back to the message thread. Stale results (from a job that
        was superseded by a newer import) are discarded by a generation counter.
      - The audio thread only ever sees immutable SourceSnapshot / FragmentTable
        objects, handed over through SamplerEngine's lock-free exchanges.
      - getStateTree() may be called from any thread (hosts do this); it returns a
        cached copy guarded by a lock that is never taken on the audio thread.
*/
class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    explicit Session (SamplerEngine& engine);
    ~Session() override;

    // ---- source ----
    void importFile (const juce::File& f);              // fresh import + auto-analysis
    void relocateMissingSource (const juce::File& f);   // keeps saved fragments
    void trimToSelection();
    bool hasSource() const noexcept { return snapshot != nullptr; }
    /** The audio currently shown / played: isolated vocals when the VOCALS view is on. */
    SnapshotPtr getSnapshot() const noexcept { return view == View::Vocals && vocals != nullptr ? vocals : snapshot; }
    SnapshotPtr getOriginalSnapshot() const noexcept { return snapshot; }

    // ---- vocal separation ----
    enum class View { Original = 0, Vocals = 1 };
    void separate();                                     // runs the selected method (cache first)
    bool hasVocals() const noexcept { return vocals != nullptr; }
    View getView() const noexcept { return view; }
    void setView (View v);
    VocalSeparator::Options getSeparationOptions() const { return sepOptions; }
    void setSeparationOptions (const VocalSeparator::Options& o) { sepOptions = o; updateStateCache(); sendChangeMessage(); }
    juce::String getVocalsLabel() const { return vocalsLabel; }
    juce::File getMissingSource() const { return missingSource; }
    AudioImporter& getImporter() noexcept { return importer; }

    // ---- analysis ----
    void analyze();
    using Granularity = PhraseSegmenter::Settings::Granularity;
    Granularity getGranularity() const noexcept { return granularity; }
    void setGranularity (Granularity g) { granularity = g; updateStateCache(); }
    float getSensitivity() const noexcept { return sensitivity; }
    void setSensitivity (float s) { sensitivity = juce::jlimit (0.0f, 1.0f, s); updateStateCache(); }

    // ---- fragments ----
    const std::vector<Fragment>& getFragments() const noexcept { return fragments; }
    int getSelectedIndex() const noexcept { return selectedIndex; }
    void selectFragment (int index);
    void setFragmentBounds (int index, int64_t start, int64_t end);
    void setFragmentRootOverride (int index, int note);
    void deleteFragment (int index);
    void clearFragments();
    int  addFragmentFromSelection();                    // returns new index or -1
    void auditionFragment (int index);

    // ---- ranking + search ----
    enum class Sort { Time = 0, Ethereal, Haunted, Nostalgic, Heavenly, Experimental, Match };
    Sort getSort() const noexcept { return sort; }
    void setSort (Sort s);
    void setSearchText (const juce::String& text);
    juce::String getSearchText() const { return searchText; }
    const SearchQuery& getSearch() const noexcept { return search; }
    /** Fragment indices in display order (sorted by the current sort / search). */
    std::vector<int> getDisplayOrder() const;
    float sortValue (int index) const;

    // ---- generation (GENERATE / SEQUENCE) ----
    static constexpr int numPatternSlots = 8;
    const GenSettings& getGenSettings() const noexcept { return gen; }
    void setGenSettings (const GenSettings& g) { gen = g; updateStateCache(); sendChangeMessage(); }
    /** Builds a new pattern. newSeed = roll a fresh seed first (unless you want to reproduce). */
    void generate (bool newSeed);
    const Pattern& getPattern() const noexcept { return pattern; }
    void setPattern (const Pattern& p);                  // edits from the SEQUENCE page
    bool hasPattern() const noexcept { return pattern.numEvents() > 0 && ! pattern.slotUids.empty(); }
    int getPatternSlot() const noexcept { return patternSlot; }
    void selectPatternSlot (int slot);                   // A..H bank, current pattern is kept in its slot
    bool isPatternSlotUsed (int slot) const { return slot >= 0 && slot < numPatternSlots && bank[(size_t) slot].numEvents() > 0; }
    void togglePin (int fragmentIndex);
    bool isPinned (int fragmentIndex) const;
    int numPinned() const noexcept { return (int) pinned.size(); }
    std::vector<GenSlot> currentGenSlots() const;
    void setPatternPlaying (bool run);
    bool isRenderingPattern() const noexcept { return renderingPattern; }
    double getBpm() const noexcept { return engine.getLastBpm(); }
    juce::String exportLoop (const juce::File& dest, int loops) const;
    juce::String exportMidi (const juce::File& dest) const;
    juce::File renderLoopForDrag (juce::String& error) const;
    juce::File renderMidiForDrag (juce::String& error) const;
    /** Fragment index for a pattern slot (or -1 if that fragment no longer exists). */
    int fragmentIndexForSlot (int slot) const;

    // ---- waveform selection / transport ----
    int64_t getSelectionStart() const noexcept { return selStart; }
    int64_t getSelectionEnd() const noexcept { return selEnd; }
    bool hasSelection() const noexcept { return selEnd > selStart; }
    void setSelection (int64_t s, int64_t e);
    bool isLooping() const noexcept { return loop; }
    void setLooping (bool l);
    void playFrom (int64_t position);
    void togglePlay (int64_t cursor);
    void stop();

    // ---- export ----
    AudioExporter::BitDepth getExportDepth() const noexcept { return exportDepth; }
    void setExportDepth (AudioExporter::BitDepth d) { exportDepth = d; updateStateCache(); }
    juce::String exportFragment (int index, const juce::File& dest) const;
    juce::File renderFragmentForDrag (int index, juce::String& error) const;

    // ---- status ----
    bool isBusy() const noexcept { return busy; }
    float getProgress() const noexcept { return progress.load(); }
    juce::String getStatusText() const { return status; }
    bool hasError() const noexcept { return statusIsError; }

    // ---- persistence ----
    juce::ValueTree getStateTree() const;                // any thread
    void setStateTreeFromAnyThread (const juce::ValueTree& tree);

    static constexpr const char* stateType = "SESSION";

private:
    void restoreOnMessageThread (const juce::ValueTree& tree);
    void startLoad (const juce::File& f, int64_t trimStart, int64_t trimEnd, bool autoAnalyze,
                    std::vector<Fragment> keepFragments, const juce::String& expectedHash);
    void startFrameAnalysis (bool segmentAfter);
    void renderPatternAsync();
    void timerCallback() override;
    void startSeparation (bool silentIfMissingCache);
    SnapshotPtr analysisSnapshot() const { return vocals != nullptr ? vocals : snapshot; }
    void publishPlayback();
    void sortAndPublish (int64_t keepSelectedUid);
    void publishTable();
    void updateStateCache();
    void setStatus (const juce::String& s, bool error = false);
    int64_t nextUid() { return ++uidCounter; }

    template <typename Fn> void runJob (const juce::String& name, Fn&& fn);

    SamplerEngine& engine;
    AudioImporter importer;
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withThreadName ("GHOSTCUT worker").withNumberOfThreads (1) };

    SnapshotPtr snapshot, vocals;
    View view = View::Original;
    VocalSeparator::Options sepOptions;
    juce::String vocalsLabel, vocalsMethodId, pendingVocalsMethod;
    Granularity granularity = Granularity::Phrases;
    Sort sort = Sort::Time;
    juce::String searchText;
    SearchQuery search;
    GenSettings gen;
    Pattern pattern;
    std::array<Pattern, numPatternSlots> bank;
    int patternSlot = 0;
    std::vector<int64_t> pinned;
    std::shared_ptr<const RenderedPattern> rendered;
    double renderedBpm = 0.0;
    bool renderingPattern = false;
    std::atomic<int> patternGeneration { 0 };
    std::shared_ptr<const FrameFeatures> frames;
    std::vector<Fragment> fragments;
    int selectedIndex = -1;
    int64_t selStart = 0, selEnd = 0;
    bool loop = false;
    float sensitivity = 0.5f;
    AudioExporter::BitDepth exportDepth = AudioExporter::BitDepth::Int24;
    juce::File missingSource;
    int64_t missingTrimStart = 0, missingTrimEnd = -1;
    juce::String missingHash;
    int64_t uidCounter = 0;

    bool busy = false;
    std::atomic<float> progress { 0.0f };
    std::atomic<int> generation { 0 };
    juce::String status { "Drop a WAV or MP3 to begin" };
    bool statusIsError = false;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    mutable juce::CriticalSection cacheLock;
    juce::ValueTree stateCache { stateType };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};

} // namespace ghostcut
