#include "ui/AccessDialogs.h"

#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
// Gives `target` the keyboard focus once its window is on screen.
void focusWhenShown (juce::Component& target)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&target)] {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

// Arrows step (JUCE's own keys); Page Up / Down move ten steps, Home / End go to the ends. Unlike JUCE's sliders it
// takes the keyboard, so Tab reaches it.
class ValueSlider final : public juce::Slider
{
public:
    ValueSlider() : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox) { setWantsKeyboardFocus (true); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto step = getInterval() > 0.0 ? getInterval() : getRange().getLength() / 100.0;
        if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey)
            setValue (getValue() + (key == juce::KeyPress::pageUpKey ? 10.0 : -10.0) * step, juce::sendNotificationSync);
        else if (key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey)
            setValue (key == juce::KeyPress::homeKey ? getMinimum() : getMaximum(), juce::sendNotificationSync);
        else
            return juce::Slider::keyPressed (key);
        return true;
    }
};

class ParameterPanel final : public juce::Component
{
public:
    explicit ParameterPanel (std::vector<AccessParam> list) : params (std::move (list))
    {
        filter.setTitle (tr ("Filter"));
        filter.onTextChange = [this] { refill(); };
        parameter.setTitle (tr ("Parameter"));
        parameter.onChange = [this] { showParameter(); };
        value.setTitle (tr ("Value"));
        value.getProperties().set ("lynxOwnContract", true); // (no app-wide slider menu: this one has its own field)
        value.onValueChange = [this] {
            if (const auto* p = current(); p != nullptr && ! updating)
            {
                p->set (value.getValue());
                showValue();
            }
        };
        typed.setTitle (tr ("Type a value"));
        typed.onReturnKey = [this] { applyTyped(); };

        for (auto* c : std::initializer_list<juce::Component*> { &filter, &parameter, &value, &typed })
            addAndMakeVisible (c);

        refill();
        setSize (420, 4 * rowHeight + 5 * gap);
        focusWhenShown (parameter);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (gap);
        for (auto* c : std::initializer_list<juce::Component*> { &filter, &parameter, &value, &typed })
        {
            c->setBounds (area.removeFromTop (rowHeight));
            area.removeFromTop (gap);
        }
    }

private:
    static constexpr int rowHeight = 28, gap = 8;

    const AccessParam* current() const
    {
        const auto row = parameter.getSelectedItemIndex();
        return juce::isPositiveAndBelow (row, (int) shown.size()) ? &params[(std::size_t) shown[(std::size_t) row]] : nullptr;
    }

    void refill()
    {
        const auto* before = current();
        const auto words = juce::StringArray::fromTokens (filter.getText().toLowerCase(), true);
        shown.clear();
        parameter.clear (juce::dontSendNotification);
        int keep = 0;
        for (int i = 0; i < (int) params.size(); ++i)
        {
            const auto name = params[(std::size_t) i].name.toLowerCase();
            if (std::all_of (words.begin(), words.end(), [&name] (const juce::String& w) { return name.contains (w); }))
            {
                if (&params[(std::size_t) i] == before)
                    keep = (int) shown.size();
                shown.push_back (i);
                parameter.addItem (params[(std::size_t) i].name, (int) shown.size());
            }
        }
        parameter.setSelectedItemIndex (shown.empty() ? -1 : keep, juce::dontSendNotification);
        showParameter();
    }

    void showParameter()
    {
        const auto* p = current();
        value.setEnabled (p != nullptr);
        typed.setEnabled (p != nullptr);
        if (p == nullptr)
            return;
        const juce::ScopedValueSetter<bool> quiet (updating, true);
        value.setRange (p->minimum, p->maximum, p->step);
        value.textFromValueFunction = p->text;
        value.valueFromTextFunction = [this] (const juce::String& t) { return parse (t).value_or (value.getValue()); };
        value.setValue (p->get(), juce::dontSendNotification);
        showValue();
    }

    void showValue() { typed.setText (value.getTextFromValue (value.getValue()), false); }

    std::optional<double> parse (const juce::String& text) const
    {
        const auto* p = current();
        return p == nullptr ? std::nullopt : ui::inverseOf (p->text, p->minimum, p->maximum) (text);
    }

    void applyTyped()
    {
        if (const auto v = parse (typed.getText()))
            value.setValue (*v, juce::sendNotificationSync);
        else
            showValue();
    }

    std::vector<AccessParam> params;
    std::vector<int> shown; // indices into params, as listed
    bool updating = false;
    juce::TextEditor filter, typed;
    juce::ComboBox parameter;
    ValueSlider value;
};

class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (int& m, const juce::StringArray& modeNames, std::vector<AccessSettingsItem> list, std::function<void()> onChange)
        : mode (m), items (std::move (list)), changed (std::move (onChange))
    {
        modeBox.setTitle (tr ("Accessibility mode"));
        modeBox.addItemList (modeNames, 1);
        modeBox.setSelectedItemIndex (mode, juce::dontSendNotification);
        modeBox.onChange = [this] {
            mode = modeBox.getSelectedItemIndex();
            changed();
        };
        addAndMakeVisible (modeBox);

        for (auto& item : items)
        {
            auto& b = *toggles.add (new juce::ToggleButton (item.name));
            b.setToggleState (*item.value, juce::dontSendNotification);
            b.onClick = [this, &b, &item] {
                *item.value = b.getToggleState();
                changed();
            };
            addAndMakeVisible (b);
        }
        setSize (420, (1 + (int) items.size()) * (rowHeight + gap) + gap);
        focusWhenShown (modeBox);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (gap);
        modeBox.setBounds (area.removeFromTop (rowHeight));
        for (auto* b : toggles)
        {
            area.removeFromTop (gap);
            b->setBounds (area.removeFromTop (rowHeight));
        }
    }

private:
    static constexpr int rowHeight = 26, gap = 8;
    int& mode;
    std::vector<AccessSettingsItem> items;
    std::function<void()> changed;
    juce::ComboBox modeBox;
    juce::OwnedArray<juce::ToggleButton> toggles;
};
} // namespace

void showParameterWindow (const juce::String& title, std::vector<AccessParam> params)
{
    openToolWindow (std::make_unique<ParameterPanel> (std::move (params)), title, "accessParameters");
}

void showAccessSettingsWindow (int& mode, const juce::StringArray& modeNames, std::vector<AccessSettingsItem> toggles, std::function<void()> changed)
{
    openToolWindow (std::make_unique<SettingsPanel> (mode, modeNames, std::move (toggles), std::move (changed)), tr ("Accessibility"), "accessSettings");
}
} // namespace lynx
