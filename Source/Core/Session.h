#pragma once

#include "Fragment.h"
#include "SourceSnapshot.h"
#include "../Analysis/PhraseSegmenter.h"
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
class Session : public juce::ChangeBroadcaster
{
public:
    explicit Session (SamplerEngine& engine);
    ~Session() override;

    // ---- source ----
    void importFile (const juce::File& f);              // fresh import + auto-analysis
    void relocateMissingSource (const juce::File& f);   // keeps saved fragments
    void trimToSelection();
    bool hasSource() const noexcept { return snapshot != nullptr; }
    SnapshotPtr getSnapshot() const noexcept { return snapshot; }
    juce::File getMissingSource() const { return missingSource; }
    AudioImporter& getImporter() noexcept { return importer; }

    // ---- analysis ----
    void analyze();
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
    void sortAndPublish (int64_t keepSelectedUid);
    void publishTable();
    void updateStateCache();
    void setStatus (const juce::String& s, bool error = false);
    int64_t nextUid() { return ++uidCounter; }

    template <typename Fn> void runJob (const juce::String& name, Fn&& fn);

    SamplerEngine& engine;
    AudioImporter importer;
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withThreadName ("GHOSTCUT worker").withNumberOfThreads (1) };

    SnapshotPtr snapshot;
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
