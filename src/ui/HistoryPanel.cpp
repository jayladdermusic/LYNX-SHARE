#include "ui/HistoryPanel.h"

#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class HistoryPanel final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener
{
public:
    explicit HistoryPanel (AudioEngine& e) : engine (e)
    {
        title.setText (tr ("Undo history"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        note.setText (tr ("Click a step to go back (or forward) to it. Newest at the bottom."), juce::dontSendNotification);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (note);

        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
        engine.addChangeListener (this);
        refresh();
        setSize (340, 420);
    }

    ~HistoryPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        title.setBounds (area.removeFromTop (24));
        note.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        list.setBounds (area);
    }

private:
    void refresh()
    {
        labels.clear();
        for (int i = 0; i < engine.getHistorySize(); ++i)
        {
            const auto l = engine.getHistoryLabel (i);
            labels.add (tr (l.text, { juce::String::fromUTF8 (l.arg.c_str()) }));
        }

        position = engine.getHistoryPosition();
        list.updateContent();
        list.scrollToEnsureRowIsOnscreen (position);
        list.repaint();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (engine.getHistorySize() != labels.size() || engine.getHistoryPosition() != position)
            refresh();
        else if (labels.size() > 0) // the newest step may have been extended by a drag
        {
            const auto l = engine.getHistoryLabel (labels.size() - 1);
            labels.set (labels.size() - 1, tr (l.text, { juce::String::fromUTF8 (l.arg.c_str()) }));
            list.repaintRow (labels.size() - 1);
        }
    }

    int getNumRows() override { return labels.size(); }

    juce::String getNameForRow (int row) override
    {
        return labels[row] + (row == position ? ", " + tr ("current") : row > position ? ", " + tr ("undone") : juce::String());
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto current = row == position;
        const auto future = row > position;

        if (current)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
            g.setColour (theme::colour::accent);
            g.fillRect (0, 2, 3, height - 4);
        }

        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textDisabled);
        g.drawText (juce::String (row), 8, 0, 28, height, juce::Justification::centredRight);
        g.setFont (theme::font (theme::type::body, current));
        g.setColour (future ? theme::colour::textDisabled : theme::colour::textPrimary);
        g.drawText (labels[row], 44, 0, width - 50, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { engine.jumpToHistory (row); }

    AudioEngine& engine;
    juce::Label title, note;
    juce::ListBox list;
    juce::StringArray labels;
    int position = 0;
};
} // namespace

std::unique_ptr<juce::Component> makeHistoryPanel (AudioEngine& engine)
{
    return std::make_unique<HistoryPanel> (engine);
}
} // namespace lynx
