#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/KeyMap.h"
#include "engine/AudioEngine.h"
#include "ui/AccessDialogs.h"

namespace lynx
{
class PlaylistComponent;
class PianoRollComponent;
class ChannelRackComponent;
class MixerComponent;
class BrowserComponent;

// The screen reader layer, after OSARA for REAPER (docs/ACCESSIBILITY.md): keyboard navigation of the Playlist, Piano
// Roll, Channel Rack and Mixer, spoken announcements (JUCE's AccessibilityHandler::postAnnouncement: VoiceOver, NVDA,
// JAWS, Narrator), the menus of the current item and an accessible parameter dialog. It draws nothing: it moves the
// panels' own selection (current track, selected clips and notes, channel, insert, effect slot), so every existing
// command acts on what it selected. Its keys are the key map's `access.*` actions, looked up only while it is on.
class AccessLayer final : private juce::Timer
{
public:
    struct Panels
    {
        PlaylistComponent& playlist;
        PianoRollComponent& pianoRoll;
        ChannelRackComponent& rack;
        MixerComponent& mixer;
        BrowserComponent& browser;
        juce::MenuBarComponent& menuBar;
        std::function<void (keys::Context)> activate; // makes a panel the focused one (as a click on it)
        std::function<void (double)> playFrom;        // the playhead goes there, and Space's stop comes back to it
    };

    // Saved in <settings>/accessibility.json.
    struct Settings
    {
        enum Mode { automatic, on, off };
        int mode = automatic;        // automatic: on while a screen reader runs
        bool timePositions = false;  // positions in minutes and seconds instead of bars and beats
        bool reportMoves = true;     // the position when the cursor moves
        bool reportTransport = true; // play, stop, record, song / pattern mode, metronome
        bool reportUndo = true;      // what undo and redo changed
        bool previewNotes = true;    // the Piano Roll plays the notes the cursor reaches
        bool reportNumbers = true;   // track, channel and insert numbers
    };

    // persistent: the settings are loaded and saved and the screen reader speaks (not for --snapshot and --accesstest)
    AccessLayer (AudioEngine& engine, Panels panels, bool persistent);
    ~AccessLayer() override;

    bool isOn() const noexcept;
    bool reportsUndo() const noexcept { return isOn() && settings.reportUndo; }
    // An access.* action (and "access.toggle", which works with the mode off); false when it is not one.
    bool run (const std::string& id, keys::Context focus);
    // After an existing action ran: says what changed (transport, pattern, selected channel...).
    void afterAction (const std::string& id, keys::Context focus);
    // After the focused panel used a key itself (Delete, Ctrl+C / V / B / A, the arrows moving notes...).
    void afterPanelKey (const keys::Chord& chord, keys::Context focus);
    // The main window's timer: the screen reader starting or stopping, the keyboard focus following the focused panel
    // (and the other way round), a new window to label.
    void update (keys::Context focus);
    // Speaks when the layer is on.
    void speak (const juce::String& text) const;
    // Moves the keyboard to the panel's cover (only said when it cannot have the focus).
    void focusPanel (keys::Context context);
    juce::String insertName (int track) const; // "Master", "Insert 3" or "3 Drums" (as the settings say)
    const juce::String& getLastSpoken() const noexcept { return lastSpoken; } // (--accesstest)
    void showSettings();

private:
    // A focusable, invisible cover over a panel (only while the layer is on): the keyboard focus goes there when the
    // panel gets the focus, so the screen reader lands in the panel and reads its name and current item. It takes no
    // clicks and draws nothing; its keys reach the main window like the panel's.
    struct PanelFocus final : juce::Component
    {
        PanelFocus (AccessLayer& layer, keys::Context context);
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        AccessLayer& layer;
        const keys::Context context;
    };

    void timerCallback() override; // ends the note previews
    juce::Component& panelComponent (keys::Context context) const;
    keys::Context panelContaining (const juce::Component* component) const; // global when none
    void attachFocusTargets (bool attach);

    // reading things out
    juce::String positionText (std::int64_t tick) const;
    juce::String lengthText (std::int64_t ticks) const;
    juce::String trackText (int track) const;
    juce::String clipText (int clip) const;
    juce::String notesText (const std::vector<int>& notes) const;
    juce::String channelText (int channel) const;
    juce::String mixerTrackText (int track) const;
    juce::String slotText (int track, int slot) const;
    juce::String volumeText (int track) const;
    juce::String statusText() const;
    juce::String currentText (keys::Context focus) const;
    juce::String panelName (keys::Context focus) const;
    juce::String panelText (keys::Context focus) const; // the name and the current item

    // cursors
    std::int64_t cursorOf (keys::Context focus) const;
    void moveCursor (keys::Context focus, std::int64_t tick);
    void followCursor (keys::Context focus, std::int64_t tick);
    std::int64_t barTicksAt (std::int64_t tick) const;
    int currentClip() const; // the clip Tab reached while it is still the only one selected, else -1
    bool runPlaylist (const std::string& id);
    bool runPianoRoll (const std::string& id);
    bool runRack (const std::string& id);
    bool runBrowser (const std::string& id);
    bool runMixer (const std::string& id);
    void jumpToMarker (int direction);
    void toggleMute (keys::Context focus);
    void toggleSolo (keys::Context focus);
    void showContextMenu (keys::Context focus);
    void showParameters (keys::Context focus);
    void preview (const std::vector<int>& keys);

    // parameter lists
    std::vector<AccessParam> clipParams (int clip);
    std::vector<AccessParam> noteParams (std::vector<int> notes);
    std::vector<AccessParam> channelParams (int channel);
    std::vector<AccessParam> mixerTrackParams (int track);
    std::vector<AccessParam> slotParams (int track, int slot);
    std::vector<AccessParam> projectParams();
    AccessParam targetParam (const AutomationTarget& target, const juce::String& name);

    void labelControls (juce::Component& root);
    void load();
    void save() const;

    AudioEngine& engine;
    Panels panels;
    Settings settings;
    const bool persistent;
    mutable juce::String lastSpoken;
    bool readerRunning = false;
    int readerCheck = 0;
    bool wasPlaying = false;
    keys::Context lastFocus = keys::Context::global, lastKeyboardPanel = keys::Context::global;
    std::vector<std::unique_ptr<PanelFocus>> focusTargets; // while on: one per panel
    juce::Component::SafePointer<juce::Component> lastWindow;

    std::int64_t playlistCursor = 0, pianoCursor = 0;
    int pianoKey = 60, rackStep = 0;
    int tabbedClip = -1;     // Playlist: Tab reached this clip (the menus and parameters act on it)
    bool onSlot = false;     // Mixer: Tab reached the effect slots (until Left / Right picks another insert)
    std::vector<std::pair<int, int>> sounding; // previewed (channel, key)
};
} // namespace lynx
