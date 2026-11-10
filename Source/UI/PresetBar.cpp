#include "PresetBar.h"
#include "GhostLookAndFeel.h"

namespace ghostcut
{

PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    prev.onClick = [this] { presets.step (-1); };
    next.onClick = [this] { presets.step (1); };
    save.onClick = [this] { showSaveDialog(); };
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    save.setTooltip ("Save the current sound (parameters + GENERATE settings) as a user preset");
    for (auto* b : { &prev, &next, &save }) addAndMakeVisible (b);
    presets.addChangeListener (this);
}

PresetBar::~PresetBar() { presets.removeChangeListener (this); }

void PresetBar::paint (juce::Graphics& g)
{
    auto r = nameArea.toFloat().reduced (0.5f);
    g.setColour (colours::bg);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (GhostLookAndFeel::label (12.0f, true));
    g.drawText (presets.getCurrentName(), nameArea.reduced (10, 0), juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    juce::Path arrow;
    const float ax = (float) nameArea.getRight() - 12.0f, ay = (float) nameArea.getCentreY();
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.fillPath (arrow);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    save.setBounds (r.removeFromRight (58));
    r.removeFromRight (6);
    next.setBounds (r.removeFromRight (26));
    r.removeFromRight (2);
    prev.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (2);
    nameArea = r;
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    if (nameArea.contains (e.getPosition())) showMenu();
}

void PresetBar::showMenu()
{
    const auto all = presets.list();
    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> byCat;
    for (int i = 0; i < all.size(); ++i)
    {
        const auto& info = all.getReference (i);
        byCat[info.category].addItem (i + 1, info.name + (info.factory ? "" : "  (user)"), true, info.id() == presets.getCurrentId());
    }
    for (auto& cat : PresetManager::categories())
        if (byCat.count (cat)) menu.addSubMenu (cat, byCat[cat]);
    menu.addSeparator();
    juce::PopupMenu del;
    for (int i = 0; i < all.size(); ++i)
        if (! all.getReference (i).factory) del.addItem (10000 + i, all.getReference (i).category + " / " + all.getReference (i).name);
    menu.addSubMenu ("Delete user preset", del, del.getNumItems() > 0);
    menu.addItem (20000, "Open presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, all] (int r)
    {
        juce::String err;
        if (r >= 1 && r <= all.size()) presets.load (all[r - 1], err);
        else if (r >= 10000 && r < 20000) presets.deleteUser (all[r - 10000], err);
        else if (r == 20000) { auto f = PresetManager::getUserFolder(); f.createDirectory(); f.startAsProcess(); }
        if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", err);
    });
}

void PresetBar::showSaveDialog()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "Saves parameters, effects and GENERATE settings (no audio).",
                                                  juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", presets.getCurrentName().upToFirstOccurrenceOf (" (", false, false), "Name");
    dialog->addComboBox ("category", PresetManager::categories(), "Category");
    if (auto* cb = dialog->getComboBoxComponent ("category")) cb->setText ("User", juce::dontSendNotification);
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && dialog != nullptr)
        {
            juce::String err;
            const auto name = dialog->getTextEditorContents ("name");
            const auto cat = dialog->getComboBoxComponent ("category")->getText();
            if (! presets.saveUser (name, cat, err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved", err);
        }
        dialog.reset();
    }), false);
}

} // namespace ghostcut
