#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// One editable value of the parameter dialog (a clip's gain, a note's velocity, a plugin's parameter...).
struct AccessParam
{
    juce::String name;
    double minimum = 0.0, maximum = 1.0, step = 0.01;
    std::function<double()> get;
    std::function<void (double)> set;
    std::function<juce::String (double)> text; // the value as it is read out
};

// OSARA's "View parameters" for the screen reader layer (ui/Accessibility.h): a filter field, the parameter list, a
// value slider (arrows step, Page Up / Down ten steps, Home / End the ends) and a field to type a value. Standard JUCE
// controls only, so every screen reader reads them. Opening another one replaces it.
void showParameterWindow (const juce::String& title, std::vector<AccessParam> params);

// The accessibility settings window: one combo box (the mode) and toggles. `changed` runs after each change.
struct AccessSettingsItem
{
    juce::String name;
    bool* value;
};
void showAccessSettingsWindow (int& mode, const juce::StringArray& modeNames, std::vector<AccessSettingsItem> toggles, std::function<void()> changed);
} // namespace lynx
