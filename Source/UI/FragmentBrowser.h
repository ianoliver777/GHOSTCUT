#pragma once

#include "../Core/Session.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** Fragment results browser. Rows follow the session's display order (time, a
    character score, or search match). Click to select + audition, drag a row out
    to the DAW to drop it as a WAV. */
class FragmentList : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::ChangeListener
{
public:
    FragmentList (Session&, SamplerEngine&, std::function<int()> baseNoteGetter, std::function<bool()> slicesMode);
    ~FragmentList() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();
    const std::vector<int>& getOrder() const noexcept { return order; }

private:
    class Row;
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Session& session;
    SamplerEngine& engine;
    std::function<int()> getBaseNote;
    std::function<bool()> isSlices;
    juce::ListBox list;
    std::vector<int> order;
    juce::String lastRankKey;
};

/** Details for the selected fragment: measurements, heuristic character scores
    (with the reasons behind them) and per-fragment actions. */
class FragmentInspector : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit FragmentInspector (Session&);
    ~FragmentInspector() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DragHandle;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void update();
    void exportSelected();

    Session& session;
    juce::ComboBox rootBox;
    juce::TextButton audition { "Audition" }, exportBtn { "Export WAV" }, del { "Delete" };
    std::unique_ptr<DragHandle> dragHandle;
    std::unique_ptr<juce::FileChooser> chooser;
};

/** Draws a small horizontal 0..1 bar. */
void drawScoreBar (juce::Graphics&, juce::Rectangle<float>, float value, juce::Colour);

} // namespace ghostcut
