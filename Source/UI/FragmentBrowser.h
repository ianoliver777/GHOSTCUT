#pragma once

#include "../Core/Session.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostcut
{

/** List of detected / manual fragments. Click to select + audition, drag a row
    out to the DAW to drop it as a WAV. */
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
};

/** Details for the selected fragment + per-fragment actions. */
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

} // namespace ghostcut
