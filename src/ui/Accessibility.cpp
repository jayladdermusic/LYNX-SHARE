#include "ui/Accessibility.h"

#include "core/AccessNav.h"
#include "core/Effects.h"
#include "core/NoteEdit.h"
#include "core/NoteNames.h"
#include "core/RackOps.h"
#include "engine/PluginHost.h"
#include "platform/ScreenReader.h"
#include "ui/AutomationUi.h"
#include "ui/BrowserComponent.h"
#include "ui/ChannelRackComponent.h"
#include "ui/EditorTools.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"

namespace lynx
{
namespace
{
using keys::Context;

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::String joined (const juce::StringArray& parts) { return parts.joinIntoString (", "); }

juce::String dbText (float gain)
{
    return gain <= 0.0f ? tr ("minus infinity dB") : tr ("{0} dB", { juce::String (juce::Decibels::gainToDecibels (gain), 1) });
}

juce::String panText (float pan)
{
    const auto percent = juce::roundToInt (std::abs (pan) * 100.0f);
    if (percent == 0)
        return tr ("centre");
    return pan < 0.0f ? tr ("{0}% left", { juce::String (percent) }) : tr ("{0}% right", { juce::String (percent) });
}

juce::String onOff (bool on) { return on ? tr ("on") : tr ("off"); }

juce::String keyName (int key) { return utf8 (notenames::keyName (key)); }

const Pattern* currentPattern (const Song& song)
{
    return song.patterns.empty() ? nullptr : &song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)];
}

juce::String patternName (const Song& song, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) song.patterns.size()))
        return {};
    const auto& name = song.patterns[(std::size_t) index].name;
    return name.empty() ? tr ("Pattern {0}", { juce::String (index + 1) }) : utf8 (name);
}

// the next grid line after `tick`, or the one before it (off the grid: the line it is past)
std::int64_t stepped (std::int64_t tick, std::int64_t grid, bool forward)
{
    const auto line = tick / grid * grid;
    return forward ? line + grid : (line == tick ? tick - grid : line);
}

// the start of the next bar, or of this bar (at its start: the previous one)
std::int64_t barStep (const Song& song, std::int64_t tick, bool forward)
{
    const auto bar = song.barAt (tick);
    return forward ? bar.tick + bar.length : (tick > bar.tick || bar.tick == 0 ? bar.tick : song.barAt (bar.tick - 1).tick);
}

// a toggle of the parameter dialog: 0 / 1, read as off / on
AccessParam toggleParam (const juce::String& name, std::function<bool()> get, std::function<void (bool)> set)
{
    return { name, 0.0, 1.0, 1.0, [get] { return get() ? 1.0 : 0.0; }, [set] (double v) { set (v >= 0.5); },
             [] (double v) { return onOff (v >= 0.5); } };
}

// " (Space)" and other key reminders at the end of a tooltip's first line
juce::String withoutKeys (const juce::String& tooltip)
{
    auto line = tooltip.upToFirstOccurrenceOf ("\n", false, false).trim();
    if (line.endsWithChar (')') && line.containsChar ('('))
        line = line.upToLastOccurrenceOf ("(", false, false).trim();
    return line;
}
} // namespace

AccessLayer::AccessLayer (AudioEngine& e, Panels p, bool keepSettings) : engine (e), panels (std::move (p)), persistent (keepSettings)
{
    if (persistent)
        load();
    else
        settings.mode = Settings::off; // off screen: on only by its key, whatever screen reader runs
    readerRunning = platform::screenReaderRunning();
}

AccessLayer::~AccessLayer()
{
    timerCallback();
}

bool AccessLayer::isOn() const noexcept
{
    return settings.mode == Settings::on || (settings.mode == Settings::automatic && readerRunning);
}

void AccessLayer::speak (const juce::String& text) const
{
    if (! isOn() || text.isEmpty())
        return;
    lastSpoken = text;
    if (persistent) // off screen nothing reaches the running screen reader
        juce::AccessibilityHandler::postAnnouncement (text, juce::AccessibilityHandler::AnnouncementPriority::high);
}

// --- settings ----------------------------------------------------------------------------------

namespace
{
juce::File settingsFile() { return PluginHost::settingsFolder().getChildFile ("accessibility.json"); }

struct BoolSetting
{
    const char* key;
    bool AccessLayer::Settings::*member;
};
const BoolSetting boolSettings[] = {
    { "timePositions", &AccessLayer::Settings::timePositions }, { "reportMoves", &AccessLayer::Settings::reportMoves },
    { "reportTransport", &AccessLayer::Settings::reportTransport }, { "reportUndo", &AccessLayer::Settings::reportUndo },
    { "previewNotes", &AccessLayer::Settings::previewNotes },   { "reportNumbers", &AccessLayer::Settings::reportNumbers },
};
} // namespace

void AccessLayer::load()
{
    const auto json = juce::JSON::parse (settingsFile());
    if (! json.isObject())
        return;
    settings.mode = juce::jlimit (0, 2, (int) json.getProperty ("mode", settings.mode));
    for (const auto& b : boolSettings)
        settings.*b.member = (bool) json.getProperty (b.key, settings.*b.member);
}

void AccessLayer::save() const
{
    if (! persistent)
        return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("mode", settings.mode);
    for (const auto& b : boolSettings)
        o->setProperty (b.key, settings.*b.member);
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

void AccessLayer::showSettings()
{
    showAccessSettingsWindow (settings.mode, { tr ("Automatic (with a screen reader)"), tr ("On"), tr ("Off") },
                              { { tr ("Positions in minutes and seconds"), &settings.timePositions },
                                { tr ("Report the position when the cursor moves"), &settings.reportMoves },
                                { tr ("Report play, stop, record and modes"), &settings.reportTransport },
                                { tr ("Report undo and redo"), &settings.reportUndo },
                                { tr ("Play notes while moving in the Piano Roll"), &settings.previewNotes },
                                { tr ("Report track, channel and insert numbers"), &settings.reportNumbers } },
                              [this] { save(); });
}

// --- the main window's timer -------------------------------------------------------------------

void AccessLayer::update (Context focus)
{
    if (++readerCheck >= 30) // about once a second
    {
        readerCheck = 0;
        readerRunning = platform::screenReaderRunning();
    }

    attachFocusTargets (isOn());
    if (! isOn())
    {
        lastFocus = lastKeyboardPanel = Context::global;
        return;
    }

    // a window that just came to the front: its icon buttons get their tooltip as their name
    if (auto* window = juce::TopLevelWindow::getActiveTopLevelWindow(); window != lastWindow.getComponent())
    {
        lastWindow = window;
        if (window != nullptr)
            labelControls (*window);
    }

    const auto keyboardPanel = panelContaining (juce::Component::getCurrentlyFocusedComponent());
    if (focus != lastFocus) // another panel got the focus (F5..F9, a click): the keyboard focus follows
    {
        lastFocus = lastKeyboardPanel = focus;
        if (focus != Context::global && keyboardPanel != focus)
            focusPanel (focus);
    }
    else if (keyboardPanel != Context::global && keyboardPanel != lastKeyboardPanel) // the keyboard focus moved into a panel
    {
        lastKeyboardPanel = keyboardPanel;
        if (keyboardPanel != focus && panels.activate)
        {
            panels.activate (keyboardPanel);
            lastFocus = keyboardPanel;
        }
    }
}

AccessLayer::PanelFocus::PanelFocus (AccessLayer& l, Context c) : layer (l), context (c)
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (true);
}

std::unique_ptr<juce::AccessibilityHandler> AccessLayer::PanelFocus::createAccessibilityHandler()
{
    struct Handler final : juce::AccessibilityHandler
    {
        explicit Handler (PanelFocus& f) : juce::AccessibilityHandler (f, juce::AccessibilityRole::group), focus (f) {}
        juce::String getTitle() const override { return focus.layer.panelText (focus.context); }
        PanelFocus& focus;
    };
    return std::make_unique<Handler> (*this);
}

juce::Component& AccessLayer::panelComponent (Context context) const
{
    switch (context)
    {
        case Context::pianoRoll: return panels.pianoRoll;
        case Context::channelRack: return panels.rack;
        case Context::mixer: return panels.mixer;
        case Context::browser: return panels.browser;
        case Context::playlist:
        case Context::global: break;
    }
    return panels.playlist;
}

Context AccessLayer::panelContaining (const juce::Component* component) const
{
    for (auto context : { Context::playlist, Context::pianoRoll, Context::channelRack, Context::mixer, Context::browser })
        if (component != nullptr && (component == &panelComponent (context) || panelComponent (context).isParentOf (component)))
            return context;
    return Context::global;
}

void AccessLayer::attachFocusTargets (bool attach)
{
    if (! attach)
    {
        focusTargets.clear(); // (each leaves its panel)
        return;
    }
    if (focusTargets.empty())
        for (auto context : { Context::playlist, Context::pianoRoll, Context::channelRack, Context::mixer, Context::browser })
        {
            auto& target = *focusTargets.emplace_back (std::make_unique<PanelFocus> (*this, context));
            panelComponent (context).addAndMakeVisible (target);
            target.toBack();
        }
    for (auto& target : focusTargets) // follows the panel's size
        target->setBounds (panelComponent (target->context).getLocalBounds());
}

void AccessLayer::focusPanel (Context context)
{
    PanelFocus* target = nullptr;
    for (auto& t : focusTargets)
        if (t->context == context)
            target = t.get();
    // not out of another window (a dialog, a plugin's editor): there the panel is only said
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (target != nullptr && target->isShowing() && (focused == nullptr || focused->getTopLevelComponent() == target->getTopLevelComponent()))
        target->grabKeyboardFocus(); // (the screen reader reads the panel's name and current item)
    else
        speak (panelText (context));
}

void AccessLayer::labelControls (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (child->getTitle().isEmpty())
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (child))
                if (const auto name = withoutKeys (tip->getTooltip()); name.isNotEmpty())
                    child->setTitle (name);
        labelControls (*child);
    }
}

// --- reading out -------------------------------------------------------------------------------

juce::String AccessLayer::positionText (std::int64_t tick) const
{
    const auto& song = engine.getSong();
    if (settings.timePositions)
    {
        const auto seconds = song.secondsForTicks (std::max<std::int64_t> (0, tick));
        const auto minutes = (int) (seconds / 60.0);
        return tr ("{0} min {1} s", { juce::String (minutes), juce::String (seconds - minutes * 60.0, 2) });
    }
    const auto p = accessnav::barBeatAt (song, tick);
    if (p.ticks == 0)
        return tr ("Bar {0} beat {1}", { juce::String (p.bar), juce::String (p.beat) });
    return tr ("Bar {0} beat {1} {2}%", { juce::String (p.bar), juce::String (p.beat), juce::String ((int) (p.ticks * 100 / ppq)) });
}

juce::String AccessLayer::lengthText (std::int64_t ticks) const
{
    const auto bar = barTicksAt (0);
    struct Unit
    {
        std::int64_t size;
        const char *one, *many;
    };
    const Unit units[] = { { bar, TR_NOOP ("1 bar"), TR_NOOP ("{0} bars") },
                           { ppq, TR_NOOP ("1 beat"), TR_NOOP ("{0} beats") },
                           { ticksPerStep, TR_NOOP ("1 step"), TR_NOOP ("{0} steps") },
                           { 1, TR_NOOP ("1 tick"), TR_NOOP ("{0} ticks") } };
    for (const auto& u : units)
        if (ticks > 0 && ticks % u.size == 0)
            return ticks == u.size ? tr (u.one) : tr (u.many, { juce::String (ticks / u.size) });
    return tr ("{0} ticks", { juce::String (ticks) });
}

juce::String AccessLayer::trackText (int track) const
{
    const auto& song = engine.getSong();
    const auto info = track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track] : PlaylistTrack {};
    juce::StringArray parts;
    const auto number = juce::String (track + 1);
    if (info.name.empty())
        parts.add (tr ("Track {0}", { number }));
    else
        parts.add (settings.reportNumbers ? number + " " + utf8 (info.name) : utf8 (info.name));
    if (info.muted)
        parts.add (tr ("muted"));
    const auto clips = accessnav::clipsOnTrack (song.playlist, track).size();
    parts.add (clips == 0 ? tr ("empty") : clips == 1 ? tr ("1 clip") : tr ("{0} clips", { juce::String ((int) clips) }));
    return joined (parts);
}

juce::String AccessLayer::clipText (int clip) const
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (clip, (int) song.playlist.size()))
        return {};
    const auto& c = song.playlist[(std::size_t) clip];
    juce::String name;
    if (c.kind == ClipKind::audio && juce::isPositiveAndBelow (c.channel, (int) song.channels.size()))
        name = utf8 (song.channels[(std::size_t) c.channel].name);
    else if (c.kind == ClipKind::automation && juce::isPositiveAndBelow (c.automation, (int) song.automations.size()))
        name = tr ("Automation {0}", { utf8 (song.automations[(std::size_t) c.automation].name) });
    else
        name = patternName (song, c.pattern);
    juce::StringArray parts { name, positionText (c.start), lengthText (c.length) };
    if (c.muted)
        parts.add (tr ("muted"));
    return joined (parts);
}

juce::String AccessLayer::notesText (const std::vector<int>& notes) const
{
    const auto* p = currentPattern (engine.getSong());
    if (p == nullptr || notes.empty())
        return {};
    juce::StringArray names;
    const Note* first = nullptr;
    for (auto i : notes)
        if (juce::isPositiveAndBelow (i, (int) p->notes.size()))
        {
            first = first != nullptr ? first : &p->notes[(std::size_t) i];
            names.add (keyName (p->notes[(std::size_t) i].key));
        }
    if (first == nullptr)
        return {};
    juce::StringArray parts { names.joinIntoString (" "), positionText (first->start), lengthText (first->length) };
    if (names.size() == 1)
    {
        parts.add (tr ("velocity {0}%", { juce::String (juce::roundToInt (first->velocity * 100.0f)) }));
        if (first->muted)
            parts.add (tr ("muted"));
    }
    return joined (parts);
}

juce::String AccessLayer::channelText (int channel) const
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
        return tr ("No channel");
    const auto& c = song.channels[(std::size_t) channel];
    juce::StringArray parts { settings.reportNumbers ? juce::String (channel + 1) + " " + utf8 (c.name) : utf8 (c.name) };
    if (c.muted)
        parts.add (tr ("muted"));
    if (c.mixerTrack > 0)
        parts.add (tr ("insert {0}", { juce::String (c.mixerTrack) }));
    return joined (parts);
}

juce::String AccessLayer::volumeText (int track) const
{
    const auto& song = engine.getSong();
    return juce::isPositiveAndBelow (track, (int) song.mixer.size()) ? dbText (faderGain (song.mixer[(std::size_t) track].volume)) : juce::String();
}

juce::String AccessLayer::insertName (int track) const
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (track, (int) song.mixer.size()))
        return {};
    const auto& name = song.mixer[(std::size_t) track].name;
    if (track == 0)
        return tr ("Master");
    if (name.empty() || name == "Insert " + std::to_string (track)) // (holds the number already)
        return tr ("Insert {0}", { juce::String (track) });
    return settings.reportNumbers ? juce::String (track) + " " + utf8 (name) : utf8 (name);
}

juce::String AccessLayer::mixerTrackText (int track) const
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (track, (int) song.mixer.size()))
        return {};
    const auto& m = song.mixer[(std::size_t) track];
    juce::StringArray parts { insertName (track), volumeText (track), panText (m.pan) };
    if (m.muted)
        parts.add (tr ("muted"));
    if (m.solo)
        parts.add (tr ("solo"));
    if (! m.effects.empty())
        parts.add (m.effects.size() == 1 ? tr ("1 effect") : tr ("{0} effects", { juce::String ((int) m.effects.size()) }));
    return joined (parts);
}

juce::String AccessLayer::slotText (int track, int slot) const
{
    const auto& song = engine.getSong();
    const auto number = tr ("Slot {0}", { juce::String (slot + 1) });
    if (! juce::isPositiveAndBelow (track, (int) song.mixer.size()) || ! juce::isPositiveAndBelow (slot, (int) song.mixer[(std::size_t) track].effects.size()))
        return number + ", " + tr ("empty");
    const auto& s = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
    juce::StringArray parts { number, s.kind == EffectKind::plugin ? utf8 (s.plugin.name) : utf8 (effectInfo (s.kind).name) };
    if (! s.enabled)
        parts.add (tr ("bypassed"));
    if (s.mix < 1.0f)
        parts.add (tr ("mix {0}%", { juce::String (juce::roundToInt (s.mix * 100.0f)) }));
    return joined (parts);
}

juce::String AccessLayer::statusText() const
{
    const auto& song = engine.getSong();
    const auto& t = engine.getTransport();
    juce::StringArray parts { engine.isRecording() ? tr ("Recording") : t.playing.load() ? tr ("Playing") : tr ("Stopped"),
                              song.mode == PlayMode::song ? tr ("Song mode") : tr ("Pattern mode"),
                              patternName (song, song.currentPattern) };
    if (t.metronome.load())
        parts.add (tr ("metronome on"));
    return joined (parts);
}

juce::String AccessLayer::panelName (Context focus) const
{
    switch (focus)
    {
        case Context::playlist: return tr ("Playlist");
        case Context::pianoRoll: return tr ("Piano Roll");
        case Context::channelRack: return tr ("Channel Rack");
        case Context::mixer: return tr ("Mixer");
        case Context::browser: return tr ("Browser");
        case Context::global: break;
    }
    return {};
}

juce::String AccessLayer::panelText (Context focus) const
{
    juce::StringArray parts { panelName (focus), currentText (focus) };
    parts.removeEmptyStrings();
    return joined (parts);
}

juce::String AccessLayer::currentText (Context focus) const
{
    switch (focus)
    {
        case Context::playlist:
        {
            const auto clip = currentClip();
            return clip >= 0 ? clipText (clip) : trackText (juce::jmax (0, panels.playlist.getCurrentTrack()));
        }
        case Context::pianoRoll:
        {
            const auto* p = currentPattern (engine.getSong());
            const auto note = p != nullptr ? accessnav::noteAt (*p, engine.getSelectedChannel(), pianoKey, pianoCursor) : -1;
            return note >= 0 ? notesText ({ note }) : keyName (pianoKey) + ", " + positionText (pianoCursor);
        }
        case Context::channelRack:
        {
            const auto* p = currentPattern (engine.getSong());
            const auto on = p != nullptr && p->hasStep (engine.getSelectedChannel(), rackStep);
            return channelText (engine.getSelectedChannel()) + ", " + tr ("step {0}", { juce::String (rackStep + 1) }) + (on ? ", " + tr ("on") : juce::String());
        }
        case Context::mixer:
        {
            const auto track = panels.mixer.getSelectedTrack();
            return onSlot ? slotText (track, juce::jmax (0, panels.mixer.getSelectedSlot())) : mixerTrackText (track);
        }
        case Context::browser: return panels.browser.describeSelection();
        case Context::global: break;
    }
    return statusText();
}

// --- cursors -----------------------------------------------------------------------------------

std::int64_t AccessLayer::barTicksAt (std::int64_t tick) const
{
    return std::max<std::int64_t> (1, engine.getSong().barAt (tick).length);
}

std::int64_t AccessLayer::cursorOf (Context focus) const
{
    if (focus == Context::playlist)
        return playlistCursor;
    if (focus == Context::pianoRoll)
        return pianoCursor;
    if (focus == Context::channelRack)
        return (std::int64_t) rackStep * ticksPerStep;
    return (std::int64_t) engine.getTransport().positionTicks.load();
}

void AccessLayer::moveCursor (Context focus, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    (focus == Context::playlist ? playlistCursor : pianoCursor) = tick;

    // stopped in the mode this panel plays, the playhead follows (Space plays from the cursor)
    const auto songPanel = focus == Context::playlist;
    if (! engine.getTransport().playing.load() && (engine.getSong().mode == PlayMode::song) == songPanel)
        engine.seek ((double) tick);

    if (! settings.reportMoves)
        return;
    auto text = positionText (tick);
    if (focus == Context::pianoRoll)
        if (const auto* p = currentPattern (engine.getSong()))
            if (const auto chord = accessnav::chordAt (*p, engine.getSelectedChannel(), tick); ! chord.empty())
                text << ", " << notesText (chord);
    speak (text);
}

int AccessLayer::currentClip() const
{
    const auto& selection = panels.playlist.getSelection();
    return selection.size() == 1 && selection.front() == tabbedClip && tabbedClip < (int) engine.getSong().playlist.size() ? tabbedClip : -1;
}

void AccessLayer::preview (const std::vector<int>& keyList)
{
    if (! settings.previewNotes)
        return;
    timerCallback(); // the previous preview ends
    const auto channel = engine.getSelectedChannel();
    for (auto key : keyList)
    {
        engine.sendLiveNote (channel, key, 0.78f, true);
        sounding.push_back ({ channel, key });
    }
    startTimer (350);
}

void AccessLayer::timerCallback()
{
    stopTimer();
    for (const auto& [channel, key] : sounding)
        engine.sendLiveNote (channel, key, 0.0f, false);
    sounding.clear();
}

// --- actions -----------------------------------------------------------------------------------

bool AccessLayer::run (const std::string& id, Context focus)
{
    if (id == "access.toggle")
    {
        const auto wasOn = isOn();
        if (wasOn)
            speak (tr ("Accessibility mode off"));
        settings.mode = wasOn ? Settings::off : Settings::on;
        save();
        if (! wasOn)
            speak (tr ("Accessibility mode on"));
        return true;
    }
    if (id.rfind ("access.", 0) != 0 || ! isOn())
        return false;

    if (id == "access.settings") showSettings();
    else if (id == "access.menuBar") panels.menuBar.showMenu (0);
    else if (id == "access.contextMenu") showContextMenu (focus);
    else if (id == "access.parameters") showParameters (focus);
    else if (id == "access.projectParameters") showParameterWindow (tr ("Project"), projectParams());
    else if (id == "access.reportPosition")
        speak (positionText (engine.getTransport().playing.load() ? (std::int64_t) engine.getTransport().positionTicks.load() : cursorOf (focus)));
    else if (id == "access.reportTempo")
    {
        const auto& song = engine.getSong();
        const auto bar = song.barAt (cursorOf (focus));
        speak (tr ("{0} BPM, {1} beats per bar", { juce::String (song.bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."),
                                                   juce::String ((int) (bar.length / ppq)) }));
    }
    else if (id == "access.reportStatus") speak (statusText());
    else if (id == "access.reportCurrent") speak (currentText (focus));
    else if (id == "access.mute") toggleMute (focus);
    else if (id == "access.solo") toggleSolo (focus);
    else if (id == "access.previousMarker" || id == "access.nextMarker") jumpToMarker (id == "access.nextMarker" ? 1 : -1);
    else if (id.rfind ("access.playlist.", 0) == 0) return runPlaylist (id.substr (16));
    else if (id.rfind ("access.pianoRoll.", 0) == 0) return runPianoRoll (id.substr (17));
    else if (id.rfind ("access.rack.", 0) == 0) return runRack (id.substr (12));
    else if (id.rfind ("access.mixer.", 0) == 0) return runMixer (id.substr (13));
    else if (id.rfind ("access.browser.", 0) == 0) return runBrowser (id.substr (15));
    else return false;
    return true;
}

bool AccessLayer::runPlaylist (const std::string& id)
{
    auto& playlist = panels.playlist;
    const auto& song = engine.getSong();
    const auto track = juce::jmax (0, playlist.getCurrentTrack());
    const auto grid = std::max<std::int64_t> (1, playlist.gridTicks());

    if (id == "gridCoarser" || id == "gridFiner")
        return speak (grid::step (playlist.getSnapBox(), grid, song.beatsPerBar, id == "gridCoarser" ? 1 : -1)), true;

    if (id == "previousTrack" || id == "nextTrack")
    {
        const auto next = juce::jlimit (0, juce::jmax (0, playlist.getNumTracks() - 1), track + (id == "nextTrack" ? 1 : -1));
        playlist.setCurrentTrack (next);
        tabbedClip = -1;
        speak (trackText (next));
    }
    else if (id == "left" || id == "right")
        moveCursor (Context::playlist, stepped (playlistCursor, grid, id == "right"));
    else if (id == "previousBar" || id == "nextBar")
        moveCursor (Context::playlist, barStep (song, playlistCursor, id == "nextBar"));
    else if (id == "start") moveCursor (Context::playlist, 0);
    else if (id == "end") moveCursor (Context::playlist, song.songLength());
    else if (id == "previousClip" || id == "nextClip")
    {
        const auto from = currentClip() >= 0 ? song.playlist[(std::size_t) currentClip()].start : playlistCursor + (id == "nextClip" ? -1 : 1);
        const auto clip = accessnav::nextClip (song.playlist, track, from, id == "nextClip" ? 1 : -1);
        if (clip < 0)
            return speak (accessnav::clipsOnTrack (song.playlist, track).empty() ? tr ("No clips on this track") : tr ("No more clips")), true;
        playlist.setSelection ({ clip });
        tabbedClip = clip;
        playlistCursor = song.playlist[(std::size_t) clip].start;
        if (! engine.getTransport().playing.load() && song.mode == PlayMode::song)
            engine.seek ((double) playlistCursor);
        speak (clipText (clip));
    }
    else if (id == "open")
    {
        const auto clip = currentClip() >= 0 ? currentClip() : accessnav::clipAt (song.playlist, track, playlistCursor);
        if (clip < 0)
            return speak (tr ("No clip")), true;
        playlist.openClipSource (clip, false);
    }
    else if (id == "place")
    {
        tabbedClip = playlist.placeBrushClip (track, playlistCursor);
        speak (tr ("Placed {0}", { clipText (tabbedClip) }));
    }
    else if (id == "selectionStart" || id == "selectionEnd")
    {
        auto from = song.hasLoop() ? song.loopStart : playlistCursor;
        auto to = song.hasLoop() ? song.loopEnd : playlistCursor;
        (id == "selectionStart" ? from : to) = playlistCursor;
        if (to > from)
            playlist.setTimeSelection (from, to);
        speak (tr ("Time selection {0} to {1}", { positionText (from), positionText (to) }));
    }
    else
        return false;
    return true;
}

bool AccessLayer::runPianoRoll (const std::string& id)
{
    auto& pianoRoll = panels.pianoRoll;
    const auto& song = engine.getSong();
    const auto* pattern = currentPattern (song);
    const auto channel = engine.getSelectedChannel();
    const auto grid = std::max<std::int64_t> (1, pianoRoll.gridTicks());
    if (pattern == nullptr)
        return false;

    if (id == "gridCoarser" || id == "gridFiner")
        return speak (grid::step (pianoRoll.getSnapBox(), grid, song.beatsPerBar, id == "gridCoarser" ? 1 : -1)), true;

    if (id == "up" || id == "down")
    {
        pianoKey = juce::jlimit (0, 127, pianoKey + (id == "up" ? 1 : -1));
        preview ({ pianoKey });
        speak (currentText (Context::pianoRoll));
    }
    else if (id == "left" || id == "right")
        moveCursor (Context::pianoRoll, stepped (pianoCursor, grid, id == "right"));
    else if (id == "previousBar" || id == "nextBar")
        moveCursor (Context::pianoRoll, barStep (song, pianoCursor, id == "nextBar"));
    else if (id == "start") moveCursor (Context::pianoRoll, 0);
    else if (id == "end") moveCursor (Context::pianoRoll, pattern->effectiveLength (song.beatsPerBar));
    else if (id == "previousChord" || id == "nextChord")
    {
        // from the chord Tab reached; otherwise a chord right at the cursor comes first
        const auto here = accessnav::chordAt (*pattern, channel, pianoCursor);
        const auto forward = id == "nextChord";
        const auto from = ! here.empty() && pianoRoll.getSelection() == here ? pianoCursor : pianoCursor + (forward ? -1 : 1);
        const auto start = accessnav::nextChordStart (*pattern, channel, from, forward ? 1 : -1);
        if (start < 0)
            return speak (tr ("No more notes")), true;
        const auto chord = accessnav::chordAt (*pattern, channel, start);
        pianoCursor = start;
        pianoKey = pattern->notes[(std::size_t) chord.front()].key;
        pianoRoll.setSelection (chord);
        std::vector<int> chordKeys;
        for (auto i : chord)
            chordKeys.push_back (pattern->notes[(std::size_t) i].key);
        preview (chordKeys);
        speak (notesText (chord));
    }
    else if (id == "higherNote" || id == "lowerNote")
    {
        auto chord = accessnav::chordAt (*pattern, channel, pianoCursor);
        if (id == "lowerNote")
            std::reverse (chord.begin(), chord.end());
        const auto it = std::find_if (chord.begin(), chord.end(), [&] (int i) {
            const auto key = pattern->notes[(std::size_t) i].key;
            return id == "higherNote" ? key > pianoKey : key < pianoKey;
        });
        if (it == chord.end())
            return speak (tr ("No more notes")), true;
        pianoKey = pattern->notes[(std::size_t) *it].key;
        pianoRoll.setSelection ({ *it });
        preview ({ pianoKey });
        speak (notesText ({ *it }));
    }
    else if (id == "toggleNote")
    {
        // Enter on a note's start deletes it; inside a longer note it adds one there and the earlier note ends at it
        if (const auto existing = accessnav::noteStartingAt (*pattern, channel, pianoKey, pianoCursor); existing >= 0)
        {
            pianoRoll.setSelection ({});
            engine.editSong ([existing] (Song& s) {
                auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
                notes.erase (notes.begin() + existing);
            });
            speak (tr ("Deleted {0}", { keyName (pianoKey) }));
            return true;
        }
        Note note;
        note.start = pianoCursor;
        note.length = std::max<std::int64_t> (1, pianoRoll.newNoteLength());
        note.channel = channel;
        note.key = pianoKey;
        const auto held = accessnav::noteAt (*pattern, channel, pianoKey, pianoCursor);
        engine.editSong ([note, held] (Song& s) {
            auto& p = s.patterns[(std::size_t) s.currentPattern];
            if (held >= 0)
                p.notes[(std::size_t) held].length = note.start - p.notes[(std::size_t) held].start;
            p.notes.push_back (note);
            p.sortNotes();
        });
        const auto added = accessnav::noteStartingAt (*currentPattern (engine.getSong()), channel, pianoKey, pianoCursor);
        pianoRoll.setSelection (added >= 0 ? std::vector<int> { added } : std::vector<int> {});
        preview ({ pianoKey });
        speak (tr ("Added {0}", { notesText ({ added }) }));
    }
    else if (id == "toggleSelection")
    {
        const auto note = accessnav::noteAt (*pattern, channel, pianoKey, pianoCursor);
        if (note < 0)
            return speak (tr ("No note")), true;
        auto selection = pianoRoll.getSelection();
        const auto it = std::find (selection.begin(), selection.end(), note);
        const auto selecting = it == selection.end();
        if (selecting)
            selection.push_back (note);
        else
            selection.erase (it);
        pianoRoll.setSelection (selection);
        const auto name = keyName (pianoKey), count = juce::String ((int) selection.size());
        speak (selecting ? tr ("Selected {0}, {1} selected", { name, count }) : tr ("Deselected {0}, {1} selected", { name, count }));
    }
    else
        return false;
    return true;
}

bool AccessLayer::runBrowser (const std::string& id)
{
    auto& browser = panels.browser;
    if (id == "preview")
    {
        if (! browser.togglePreview())
            speak (tr ("No file selected"));
        return true;
    }

    const auto file = browser.getSelectedFile();
    if (! file.existsAsFile())
        return speak (tr ("No file selected")), true;
    if (id == "toPlaylist")
    {
        const auto track = juce::jmax (0, panels.playlist.getCurrentTrack());
        if (! BrowserComponent::isAudioFile (file) || ! panels.playlist.addAudioClipAt (file, playlistCursor, track))
            return speak (tr ("Not an audio file")), true;
        return speak (tr ("{0} added to {1}, {2}", { file.getFileNameWithoutExtension(), tr ("Track {0}", { juce::String (track + 1) }),
                                                     positionText (playlistCursor) })), true;
    }
    if (id == "newChannel")
    {
        const auto channels = engine.getSong().channels.size();
        browser.openFile (file);
        if (engine.getSong().channels.size() > channels)
            speak (channelText (engine.getSelectedChannel()));
        return true;
    }
    return false;
}

bool AccessLayer::runRack (const std::string& id)
{
    const auto steps = juce::jmax (1, panels.rack.numSteps());
    const auto channel = engine.getSelectedChannel();
    if (id == "left" || id == "right")
        rackStep = juce::jlimit (0, steps - 1, rackStep + (id == "right" ? 1 : -1));
    else if (id == "start" || id == "end")
        rackStep = id == "start" ? 0 : steps - 1;
    else if (id == "toggleStep")
    {
        const auto& song = engine.getSong();
        if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()) || song.patterns.empty()
            || song.channels[(std::size_t) channel].instrument == InstrumentKind::audioClip)
            return speak (tr ("No steps")), true;
        engine.editSong ([channel, step = rackStep] (Song& s) {
            auto& p = s.patterns[(std::size_t) s.currentPattern];
            p.toggleStep (channel, step);
            if (p.hasStep (channel, step)) // as a click on a step past the pattern's end
                p.lengthTicks = lengthWithStep (p.lengthTicks, step, ticksPerStep, (std::int64_t) ppq * std::max (1, s.beatsPerBar));
        });
    }
    else
        return false;

    const auto* p = currentPattern (engine.getSong());
    speak (tr ("Step {0}", { juce::String (rackStep + 1) }) + (p != nullptr && p->hasStep (channel, rackStep) ? ", " + tr ("on") : juce::String()));
    return true;
}

bool AccessLayer::runMixer (const std::string& id)
{
    auto& mixer = panels.mixer;
    const auto& song = engine.getSong();
    const auto count = (int) song.mixer.size();
    const auto track = juce::jlimit (0, juce::jmax (0, count - 1), mixer.getSelectedTrack());
    if (count == 0)
        return false;

    auto select = [&] (int t) {
        mixer.selectTrack (t);
        onSlot = false;
        speak (mixerTrackText (t));
    };

    if (id == "previous" || id == "next") select (juce::jlimit (0, count - 1, track + (id == "next" ? 1 : -1)));
    else if (id == "master") select (0);
    else if (id == "last")
    {
        auto last = count - 1;
        while (last > 0 && ! song.insertInUse (last))
            --last;
        select (last);
    }
    else if (id == "volumeUp" || id == "volumeDown")
    {
        const auto gain = faderGain (song.mixer[(std::size_t) track].volume);
        const auto db = (gain > 0.0f ? juce::Decibels::gainToDecibels (gain) : -61.0f) + (id == "volumeUp" ? 1.0f : -1.0f);
        const auto position = db < -60.0f ? 0.0f : juce::jmin (1.0f, faderPosition (juce::Decibels::decibelsToGain (db)));
        setTargetValueFromUi (engine, { AutomationTargetKind::mixerVolume, track }, position);
        speak (volumeText (track));
    }
    else if (id == "panLeft" || id == "panRight")
    {
        const auto pan = juce::jlimit (-1.0f, 1.0f, song.mixer[(std::size_t) track].pan + (id == "panRight" ? 0.05f : -0.05f));
        engine.editSong ([track, pan] (Song& s) { s.mixer[(std::size_t) track].pan = pan; });
        speak (panText (pan));
    }
    else if (id == "previousSlot" || id == "nextSlot")
    {
        const auto slot = onSlot ? juce::jlimit (0, maxEffectSlots - 1, mixer.getSelectedSlot() + (id == "nextSlot" ? 1 : -1)) : (id == "nextSlot" ? 0 : maxEffectSlots - 1);
        onSlot = true;
        mixer.selectSlot (slot);
        speak (slotText (track, slot));
    }
    else if (id == "openSlot")
    {
        const auto slot = juce::jmax (0, mixer.getSelectedSlot());
        if (slot < (int) song.mixer[(std::size_t) track].effects.size())
            mixer.openSlotEditor (track, slot);
        else
            mixer.showSlotMenuHere (track, slot);
    }
    else
        return false;
    return true;
}

void AccessLayer::jumpToMarker (int direction)
{
    const auto& markers = engine.getSong().markers();
    const auto from = playlistCursor;
    const Marker* found = nullptr;
    for (const auto& m : markers)
        if (direction > 0 ? m.tick > from && found == nullptr : m.tick < from)
            found = &m;
    if (found == nullptr)
        return speak (tr ("No more markers"));
    const auto name = found->name.empty() ? tr ("Marker") : utf8 (found->name);
    playlistCursor = found->tick;
    if (engine.getTransport().playing.load())
        engine.jumpTo ((double) found->tick);
    else if (engine.getSong().mode == PlayMode::song)
        engine.seek ((double) found->tick);
    speak (name + ", " + positionText (found->tick));
}

void AccessLayer::toggleMute (Context focus)
{
    const auto& song = engine.getSong();
    if (focus == Context::playlist)
    {
        if (const auto clip = currentClip(); clip >= 0)
        {
            engine.editSong ([clip] (Song& s) { s.playlist[(std::size_t) clip].muted = ! s.playlist[(std::size_t) clip].muted; });
            return speak (engine.getSong().playlist[(std::size_t) clip].muted ? tr ("Clip muted") : tr ("Clip unmuted"));
        }
        const auto track = juce::jmax (0, panels.playlist.getCurrentTrack());
        panels.playlist.editTrack (track, [] (PlaylistTrack& t) { t.muted = ! t.muted; });
        return speak (engine.getSong().isTrackMuted (track) ? tr ("Muted") : tr ("Unmuted"));
    }
    if (focus == Context::pianoRoll)
    {
        const auto selection = panels.pianoRoll.getSelection();
        const auto* p = currentPattern (song);
        if (selection.empty() || p == nullptr)
            return speak (tr ("No notes selected"));
        const auto mute = ! p->notes[(std::size_t) selection.front()].muted;
        engine.editSong ([&selection, mute] (Song& s) {
            for (auto i : selection)
                s.patterns[(std::size_t) s.currentPattern].notes[(std::size_t) i].muted = mute;
        });
        return speak (mute ? tr ("Notes muted") : tr ("Notes unmuted"));
    }
    if (focus == Context::channelRack)
    {
        const auto channel = engine.getSelectedChannel();
        if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        panels.rack.toggleMute (channel);
        return speak (engine.getSong().channels[(std::size_t) channel].muted ? tr ("Muted") : tr ("Unmuted"));
    }
    if (focus == Context::mixer)
    {
        const auto track = panels.mixer.getSelectedTrack();
        if (! juce::isPositiveAndBelow (track, (int) song.mixer.size()))
            return;
        engine.editSong ([track] (Song& s) { s.mixer[(std::size_t) track].muted = ! s.mixer[(std::size_t) track].muted; });
        return speak (engine.getSong().mixer[(std::size_t) track].muted ? tr ("Muted") : tr ("Unmuted"));
    }
}

void AccessLayer::toggleSolo (Context focus)
{
    if (focus == Context::playlist)
    {
        const auto track = juce::jmax (0, panels.playlist.getCurrentTrack());
        const auto count = panels.playlist.getNumTracks();
        engine.editSong ([track, count] (Song& s) { noteedit::soloTrack (s, track, count); });
        const auto& song = engine.getSong();
        auto alone = ! song.isTrackMuted (track);
        for (int t = 0; t < count && alone; ++t)
            alone = t == track || song.isTrackMuted (t);
        return speak (alone ? tr ("Solo") : tr ("Solo off"));
    }
    if (focus == Context::channelRack)
    {
        const auto channel = engine.getSelectedChannel();
        const auto& channels = engine.getSong().channels;
        if (! juce::isPositiveAndBelow (channel, (int) channels.size()))
            return;
        panels.rack.solo (channel);
        const auto& after = engine.getSong().channels;
        const auto alone = ! after[(std::size_t) channel].muted
                           && std::all_of (after.begin(), after.end(), [&] (const ChannelDef& c) { return &c == &after[(std::size_t) channel] || c.muted; });
        return speak (alone ? tr ("Solo") : tr ("Solo off"));
    }
    if (focus == Context::mixer)
    {
        const auto track = panels.mixer.getSelectedTrack();
        panels.mixer.runAction ("mixer.soloSelected");
        const auto& mixer = engine.getSong().mixer;
        return speak (juce::isPositiveAndBelow (track, (int) mixer.size()) && mixer[(std::size_t) track].solo ? tr ("Solo") : tr ("Solo off"));
    }
}

void AccessLayer::showContextMenu (Context focus)
{
    if (focus == Context::playlist)
    {
        if (const auto clip = currentClip(); clip >= 0)
            panels.playlist.showClipMenuAt (clip, playlistCursor);
        else
            panels.playlist.showTrackMenuAt (juce::jmax (0, panels.playlist.getCurrentTrack()));
    }
    else if (focus == Context::pianoRoll)
    {
        juce::PopupMenu menu;
        const auto selection = panels.pianoRoll.getSelection();
        menu.addItem (tr ("Note properties..."), ! selection.empty(), false, [this, selection] {
            showNoteInspector (engine, selection, selection.front(), panels.pianoRoll.getScreenBounds().withSizeKeepingCentre (1, 1));
        });
        menu.addItem (tr ("Parameters..."), ! selection.empty(), false, [this] { showParameters (Context::pianoRoll); });
        menu.addSubMenu (tr ("Select"), panels.pianoRoll.selectMenu());
        menu.addSubMenu (tr ("Tools"), panels.pianoRoll.toolsMenu());
        menu.addSubMenu (tr ("MIDI"), panels.pianoRoll.midiMenu());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&panels.pianoRoll));
    }
    else if (focus == Context::channelRack)
        panels.rack.showChannelMenuHere (engine.getSelectedChannel());
    else if (focus == Context::mixer)
    {
        const auto track = panels.mixer.getSelectedTrack();
        if (onSlot)
            panels.mixer.showSlotMenuHere (track, juce::jmax (0, panels.mixer.getSelectedSlot()));
        else
            panels.mixer.showTrackMenuHere (track);
    }
    else if (focus == Context::browser)
        panels.browser.showSelectedMenu();
    else
        speak (tr ("No menu here"));
}

void AccessLayer::showParameters (Context focus)
{
    const auto& song = engine.getSong();
    if (focus == Context::playlist)
    {
        if (const auto clip = currentClip(); clip >= 0)
            return showParameterWindow (clipText (clip), clipParams (clip));
        const auto track = juce::jmax (0, panels.playlist.getCurrentTrack());
        return showParameterWindow (trackText (track), { toggleParam (tr ("Mute"), [this, track] { return engine.getSong().isTrackMuted (track); },
                                                                      [this, track] (bool on) { panels.playlist.editTrack (track, [on] (PlaylistTrack& t) { t.muted = on; }); }) });
    }
    if (focus == Context::pianoRoll)
    {
        const auto selection = panels.pianoRoll.getSelection();
        if (selection.empty())
            return speak (tr ("No notes selected"));
        return showParameterWindow (notesText (selection), noteParams (selection));
    }
    if (focus == Context::channelRack && juce::isPositiveAndBelow (engine.getSelectedChannel(), (int) song.channels.size()))
        return showParameterWindow (channelText (engine.getSelectedChannel()), channelParams (engine.getSelectedChannel()));
    if (focus == Context::mixer)
    {
        const auto track = panels.mixer.getSelectedTrack();
        if (onSlot)
        {
            const auto slot = juce::jmax (0, panels.mixer.getSelectedSlot());
            if (juce::isPositiveAndBelow (track, (int) song.mixer.size()) && slot < (int) song.mixer[(std::size_t) track].effects.size())
                return showParameterWindow (slotText (track, slot), slotParams (track, slot));
            return speak (tr ("Empty slot"));
        }
        return showParameterWindow (mixerTrackText (track), mixerTrackParams (track));
    }
    showParameterWindow (tr ("Project"), projectParams());
}

// --- parameter lists ---------------------------------------------------------------------------

AccessParam AccessLayer::targetParam (const AutomationTarget& target, const juce::String& name)
{
    return { name, 0.0, 1.0, 0.01, [this, target] { return (double) juce::jmax (0.0f, engine.currentTargetValue (target)); },
             [this, target] (double v) { setTargetValueFromUi (engine, target, (float) v); },
             [this, target] (double v) { return describeTargetValue (engine, target, (float) v); } };
}

std::vector<AccessParam> AccessLayer::clipParams (int clip)
{
    // the clip's index changes when an edit reorders the playlist (kept sorted by start): `at` follows it
    const auto at = std::make_shared<int> (clip);
    auto with = [this, at] (auto read) {
        const auto& clips = engine.getSong().playlist;
        return juce::isPositiveAndBelow (*at, (int) clips.size()) ? read (clips[(std::size_t) *at]) : 0.0;
    };
    auto edit = [this, at] (auto change, bool moves = false) {
        if (! juce::isPositiveAndBelow (*at, (int) engine.getSong().playlist.size()))
            return;
        engine.editSong ([at, change] (Song& s) { change (s.playlist[(std::size_t) *at]); });
        if (! moves)
            return;
        panels.playlist.setSelection ({ *at }); // the playlist's own sort (it also routes audio clips moved to an audio track)
        panels.playlist.sortClips();
        *at = tabbedClip = panels.playlist.getSelection().front();
    };
    const auto& c = engine.getSong().playlist[(std::size_t) clip];
    const auto grid = (double) std::max<std::int64_t> (1, panels.playlist.gridTicks());
    const auto end = (double) (engine.getSong().songLength() + 64 * barTicksAt (0));
    auto position = [this] (double v) { return positionText ((std::int64_t) v); };
    auto length = [this] (double v) { return lengthText ((std::int64_t) v); };

    std::vector<AccessParam> list {
        { tr ("Start"), 0.0, end, grid, [with] { return with ([] (const Clip& x) { return (double) x.start; }); },
          [edit] (double v) { edit ([v] (Clip& x) { x.start = (std::int64_t) v; }, true); }, position },
        { tr ("Length"), grid, end, grid, [with] { return with ([] (const Clip& x) { return (double) x.length; }); },
          [edit] (double v) { edit ([v] (Clip& x) { x.length = (std::int64_t) v; }); }, length },
        { tr ("Track"), 0.0, (double) juce::jmax (1, panels.playlist.getNumTracks() - 1), 1.0, [with] { return with ([] (const Clip& x) { return (double) x.track; }); },
          [edit] (double v) { edit ([v] (Clip& x) { x.track = (int) v; }, true); }, [] (double v) { return juce::String ((int) v + 1); } },
        toggleParam (tr ("Mute"), [with] { return with ([] (const Clip& x) { return x.muted ? 1.0 : 0.0; }) > 0.5; },
                     [edit] (bool on) { edit ([on] (Clip& x) { x.muted = on; }); }),
    };
    if (c.kind == ClipKind::audio)
    {
        list.push_back ({ tr ("Gain"), -60.0, 36.0, 0.1, [with] { return with ([] (const Clip& x) { return (double) juce::Decibels::gainToDecibels (x.gain, -60.0f); }); },
                          [edit] (double v) { edit ([v] (Clip& x) { x.gain = v <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) v); }); },
                          [] (double v) { return dbText (v <= -60.0 ? 0.0f : juce::Decibels::decibelsToGain ((float) v)); } });
        list.push_back ({ tr ("Pan"), -1.0, 1.0, 0.01, [with] { return with ([] (const Clip& x) { return (double) x.pan; }); },
                          [edit] (double v) { edit ([v] (Clip& x) { x.pan = (float) v; }); }, [] (double v) { return panText ((float) v); } });
        list.push_back ({ tr ("Pitch"), -12.0, 12.0, 1.0, [with] { return with ([] (const Clip& x) { return (double) x.pitch; }); },
                          [edit] (double v) { edit ([v] (Clip& x) { x.pitch = (float) v; }); }, [] (double v) { return tr ("{0} semitones", { juce::String ((int) v) }); } });
        list.push_back (toggleParam (tr ("Reverse"), [with] { return with ([] (const Clip& x) { return x.reversed ? 1.0 : 0.0; }) > 0.5; },
                                     [edit] (bool on) { edit ([on] (Clip& x) { x.reversed = on; }); }));
        list.push_back ({ tr ("Fade in"), 0.0, end, grid / 4.0, [with] { return with ([] (const Clip& x) { return (double) x.fadeIn; }); },
                          [edit] (double v) { edit ([v] (Clip& x) { x.fadeIn = (std::int64_t) v; }); }, length });
        list.push_back ({ tr ("Fade out"), 0.0, end, grid / 4.0, [with] { return with ([] (const Clip& x) { return (double) x.fadeOut; }); },
                          [edit] (double v) { edit ([v] (Clip& x) { x.fadeOut = (std::int64_t) v; }); }, length });
    }
    return list;
}

std::vector<AccessParam> AccessLayer::noteParams (std::vector<int> notes)
{
    // read from the first note, written to every one of them; the notes stay sorted by start and `tracked` follows them
    const auto tracked = std::make_shared<std::vector<int>> (std::move (notes));
    auto with = [this, tracked] (auto read) {
        const auto* p = currentPattern (engine.getSong());
        const auto first = tracked->front();
        return p != nullptr && juce::isPositiveAndBelow (first, (int) p->notes.size()) ? read (p->notes[(std::size_t) first]) : 0.0;
    };
    auto edit = [this, tracked] (auto change) {
        engine.editSong ([tracked, change] (Song& s) {
            auto& list = s.patterns[(std::size_t) s.currentPattern].notes;
            for (auto i : *tracked)
                if (juce::isPositiveAndBelow (i, (int) list.size()))
                    change (list[(std::size_t) i]);
            if (! std::is_sorted (list.begin(), list.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }))
                noteedit::sortByStartTracking (list, *tracked);
        });
        panels.pianoRoll.setSelection (*tracked);
    };
    auto percent = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    auto number = [with, edit, percent] (const juce::String& name, float Note::*member, double minimum, double maximum, double step,
                                         std::function<juce::String (double)> text) {
        return AccessParam { name, minimum, maximum, step, [with, member] { return with ([member] (const Note& n) { return (double) (n.*member); }); },
                             [edit, member] (double v) { edit ([member, v] (Note& n) { n.*member = (float) v; }); }, text ? text : percent };
    };
    auto flag = [with, edit] (const juce::String& name, bool Note::*member) {
        return toggleParam (name, [with, member] { return with ([member] (const Note& n) { return n.*member ? 1.0 : 0.0; }) > 0.5; },
                            [edit, member] (bool on) { edit ([member, on] (Note& n) { n.*member = on; }); });
    };
    const auto grid = (double) std::max<std::int64_t> (1, panels.pianoRoll.gridTicks());
    const auto end = (double) (64 * barTicksAt (0));

    return {
        { tr ("Pitch"), 0.0, 127.0, 1.0, [with] { return with ([] (const Note& n) { return (double) n.key; }); },
          [edit] (double v) { edit ([v] (Note& n) { n.key = (int) v; }); }, [] (double v) { return keyName ((int) v); } },
        { tr ("Start"), 0.0, end, grid, [with] { return with ([] (const Note& n) { return (double) n.start; }); },
          [edit] (double v) { edit ([v] (Note& n) { n.start = (std::int64_t) v; }); }, [this] (double v) { return positionText ((std::int64_t) v); } },
        { tr ("Length"), 1.0, end, grid, [with] { return with ([] (const Note& n) { return (double) n.length; }); },
          [edit] (double v) { edit ([v] (Note& n) { n.length = (std::int64_t) v; }); }, [this] (double v) { return lengthText ((std::int64_t) v); } },
        number (tr ("Velocity"), &Note::velocity, 0.0, 1.0, 0.01, {}),
        number (tr ("Pan"), &Note::pan, -1.0, 1.0, 0.01, [] (double v) { return panText ((float) v); }),
        number (tr ("Release"), &Note::release, 0.0, 1.0, 0.01, {}),
        number (tr ("Fine pitch"), &Note::finePitch, -120.0, 120.0, 1.0, [] (double v) { return tr ("{0} cents", { juce::String ((int) v) }); }),
        number (tr ("Mod X"), &Note::modX, 0.0, 1.0, 0.01, {}),
        number (tr ("Mod Y"), &Note::modY, 0.0, 1.0, 0.01, {}),
        flag (tr ("Mute"), &Note::muted),
        flag (tr ("Slide"), &Note::slide),
        flag (tr ("Porta"), &Note::porta),
        { tr ("Colour"), 0.0, (double) noteColourCount - 1, 1.0, [with] { return with ([] (const Note& n) { return (double) n.colour; }); },
          [edit] (double v) { edit ([v] (Note& n) { n.colour = (int) v; }); }, [] (double v) { return juce::String ((int) v + 1); } },
    };
}

std::vector<AccessParam> AccessLayer::channelParams (int channel)
{
    const auto& song = engine.getSong();
    const auto& c = song.channels[(std::size_t) channel];
    std::vector<AccessParam> list { targetParam ({ AutomationTargetKind::channelVolume, channel }, tr ("Volume")),
                                    targetParam ({ AutomationTargetKind::channelPan, channel }, tr ("Pan")),
                                    toggleParam (tr ("Mute"), [this, channel] { return engine.getSong().channels[(std::size_t) channel].muted; },
                                                 [this, channel] (bool on) { engine.editSong ([channel, on] (Song& s) { s.channels[(std::size_t) channel].muted = on; }); }),
                                    { tr ("Mixer insert"), 0.0, (double) juce::jmax (1, (int) song.mixer.size() - 1), 1.0,
                                      [this, channel] { return (double) engine.getSong().channels[(std::size_t) channel].mixerTrack; },
                                      [this, channel] (double v) { engine.editSong ([channel, v] (Song& s) { s.channels[(std::size_t) channel].mixerTrack = (int) v; }); },
                                      [] (double v) { return v < 0.5 ? tr ("Master") : tr ("Insert {0}", { juce::String ((int) v) }); } } };

    auto add = [&] (int count) {
        for (int i = 0; i < count; ++i)
        {
            const AutomationTarget t { AutomationTargetKind::channelParam, channel, 0, i };
            auto name = describeAutomationTarget (engine, t).fromFirstOccurrenceOf (" - ", false, false);
            list.push_back (targetParam (t, name));
        }
    };
    if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        add (plugin->getInstance().getParameters().size());
    else if (c.instrument == InstrumentKind::sampler)
        add (samplerparam::count);
    return list;
}

std::vector<AccessParam> AccessLayer::mixerTrackParams (int track)
{
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (track, (int) song.mixer.size()))
        return {};
    auto flag = [this, track] (const juce::String& name, bool MixerTrack::*member) {
        return toggleParam (name, [this, track, member] { return engine.getSong().mixer[(std::size_t) track].*member; },
                            [this, track, member] (bool on) { engine.editSong ([track, member, on] (Song& s) { s.mixer[(std::size_t) track].*member = on; }); });
    };
    std::vector<AccessParam> list { targetParam ({ AutomationTargetKind::mixerVolume, track }, tr ("Volume")),
                                    targetParam ({ AutomationTargetKind::mixerPan, track }, tr ("Pan")),
                                    { tr ("Stereo separation"), -1.0, 1.0, 0.01, [this, track] { return (double) engine.getSong().mixer[(std::size_t) track].stereo; },
                                      [this, track] (double v) { engine.editSong ([track, v] (Song& s) { s.mixer[(std::size_t) track].stereo = (float) v; }); },
                                      [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } },
                                    flag (tr ("Mute"), &MixerTrack::muted),
                                    flag (tr ("Solo"), &MixerTrack::solo),
                                    flag (tr ("Invert phase"), &MixerTrack::invertPhase),
                                    flag (tr ("Swap left and right"), &MixerTrack::swapChannels) };
    if (track > 0)
    {
        const auto& m = song.mixer[(std::size_t) track];
        if (m.toMaster)
            list.push_back (targetParam ({ AutomationTargetKind::mixerSend, track, 0 }, tr ("Send to Master")));
        for (const auto& send : m.sends)
            list.push_back (targetParam ({ AutomationTargetKind::mixerSend, track, send.target },
                                         tr ("Send to {0}", { mixerTrackText (send.target).upToFirstOccurrenceOf (",", false, false) })));
    }
    return list;
}

std::vector<AccessParam> AccessLayer::slotParams (int track, int slot)
{
    const auto& s = engine.getSong().mixer[(std::size_t) track].effects[(std::size_t) slot];
    std::vector<AccessParam> list { toggleParam (tr ("On"), [this, track, slot] { return engine.getSong().mixer[(std::size_t) track].effects[(std::size_t) slot].enabled; },
                                                 [this, track, slot] (bool on) {
                                                     engine.editSong ([track, slot, on] (Song& song) {
                                                         auto& fx = song.mixer[(std::size_t) track].effects;
                                                         if (slot < (int) fx.size())
                                                             fx[(std::size_t) slot].enabled = on;
                                                     });
                                                 }),
                                    targetParam ({ AutomationTargetKind::effectMix, track, slot }, tr ("Mix")) };
    auto count = (int) effectInfo (s.kind).params.size();
    if (auto* plugin = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get()))
        count = plugin->getInstance().getParameters().size();
    for (int i = 0; i < count; ++i)
    {
        const AutomationTarget t { AutomationTargetKind::effectParam, track, slot, i };
        list.push_back (targetParam (t, describeAutomationTarget (engine, t).fromFirstOccurrenceOf (" - ", false, false)));
    }
    return list;
}

std::vector<AccessParam> AccessLayer::projectParams()
{
    return { targetParam ({ AutomationTargetKind::tempo }, tr ("Tempo")),
             { tr ("Beats per bar"), 1.0, 16.0, 1.0, [this] { return (double) engine.getSong().beatsPerBar; },
               [this] (double v) { engine.editSong ([v] (Song& s) { s.beatsPerBar = (int) v; }); }, [] (double v) { return juce::String ((int) v); } },
             { tr ("Swing"), 0.0, 1.0, 0.01, [this] { return (double) engine.getSong().swing; },
               [this] (double v) { engine.editSong ([v] (Song& s) { s.swing = (float) v; }); },
               [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; } },
             { tr ("Master pitch"), -1200.0, 1200.0, 1.0, [this] { return (double) engine.getSong().masterPitch; },
               [this] (double v) { engine.editSong ([v] (Song& s) { s.masterPitch = (float) v; }); },
               [] (double v) { return tr ("{0} cents", { juce::String ((int) v) }); } } };
}

// --- feedback on the existing actions ---------------------------------------------------------

void AccessLayer::afterAction (const std::string& id, Context focus)
{
    if (! isOn())
        return;
    auto starts = [&id] (const char* prefix) { return id.rfind (prefix, 0) == 0; };
    const auto& song = engine.getSong();
    const auto& t = engine.getTransport();

    if (settings.reportTransport)
    {
        if (id == "transport.playStop" || id == "transport.pause" || id == "transport.playFromStart")
            return speak (t.playing.load() || engine.isCountingIn() ? tr ("Play") : tr ("Stop"));
        if (id == "transport.record")
            return speak (engine.isRecording() ? tr ("Recording on") : tr ("Recording off"));
        if (id == "transport.toggleMode")
            return speak (song.mode == PlayMode::song ? tr ("Song mode") : tr ("Pattern mode"));
        if (id == "transport.metronome")
            return speak (tr ("Metronome {0}", { onOff (t.metronome.load()) }));
        if (id == "record.countIn")
            return speak (tr ("Count-in {0}", { onOff (engine.getRecordOptions().countInBars > 0) }));
        if (id == "record.waitForInput")
            return speak (tr ("Wait for input {0}", { onOff (engine.getRecordOptions().waitForInput) }));
    }
    if (starts ("pattern.") && id != "pattern.splitByChannel" && id != "pattern.focusSelector") // (these report themselves)
        return speak (patternName (song, song.currentPattern));
    if (starts ("rack.select") || starts ("rack.extend") || starts ("rack.move") || id == "rack.clone" || id == "rack.deleteSelected"
        || starts ("pianoRoll.previousChannel") || starts ("pianoRoll.nextChannel"))
        return speak (channelText (engine.getSelectedChannel()));
    if (starts ("rack.mute") || starts ("rack.solo"))
    {
        const auto channel = std::stoi (id.substr (9)) - 1;
        if (juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            speak (channelText (channel));
        return;
    }
    if (id == "browser.next" || id == "browser.previous" || id == "browser.openFolder" || id == "browser.closeFolder" || starts ("browser.swap"))
        return speak (panels.browser.describeSelection());
    if (id == "browser.load" && panels.browser.getSelectedFile().existsAsFile()) // the channel that has it now
        return speak (channelText (engine.getSelectedChannel()));
    if (starts ("rack.fillEach")) // the channel and how many steps it has now
    {
        const auto channel = engine.getSelectedChannel();
        const auto* pattern = currentPattern (song);
        const auto steps = pattern == nullptr ? 0 : std::count_if (pattern->notes.begin(), pattern->notes.end(), [channel] (const Note& n) { return n.channel == channel; });
        return speak (channelText (channel) + ", " + (steps == 1 ? tr ("1 note") : tr ("{0} notes", { juce::String ((int) steps) })));
    }
    if (id == "rack.sendToInserts" || id == "rack.linkToInsert" || id == "mixer.linkChannels" || id == "mixer.linkChannelsFrom") // each channel and its insert
    {
        juce::StringArray linked;
        for (const auto channel : engine.getSelectedChannels())
            linked.add (channelText (channel));
        return speak (linked.joinIntoString (". "));
    }
    if (id == "mixer.moveLeft" || id == "mixer.moveRight" || id == "mixer.soloSelected" || id == "mixer.altSoloSelected")
        return speak (mixerTrackText (panels.mixer.getSelectedTrack()));
    if (id == "playlist.ripple")
        return speak (tr ("Ripple editing {0}", { onOff (panels.playlist.isRippleOn()) }));
    juce::ignoreUnused (focus);
}

void AccessLayer::afterPanelKey (const keys::Chord& chord, Context focus)
{
    if (! isOn() || (focus != Context::playlist && focus != Context::pianoRoll))
        return;
    const auto notes = focus == Context::pianoRoll;
    const auto count = (int) (notes ? panels.pianoRoll.getSelection().size() : panels.playlist.getSelection().size());
    const auto selectedText = [&] {
        return count == 1 ? (notes ? notesText (panels.pianoRoll.getSelection()) : clipText (panels.playlist.getSelection().front()))
                          : tr ("{0} selected", { juce::String (count) });
    };
    const auto ctrl = (chord.mods & keys::ctrl) != 0;

    if (chord.key == keys::del)
        speak (tr ("Deleted"));
    else if (chord.key == keys::escape)
        speak (tr ("Selection cleared"));
    else if (ctrl && chord.key == 'C')
        speak (tr ("Copied"));
    else if (ctrl && chord.key == 'X')
        speak (tr ("Cut"));
    else if (ctrl && (chord.key == 'V' || chord.key == 'B' || chord.key == 'A'))
        speak (selectedText());
    else if (notes && count > 0 && chord.key >= keys::up && chord.key <= keys::right) // the arrows moved the notes
    {
        const auto& selection = panels.pianoRoll.getSelection();
        if (const auto* p = currentPattern (engine.getSong()); p != nullptr && juce::isPositiveAndBelow (selection.front(), (int) p->notes.size()))
        {
            pianoKey = p->notes[(std::size_t) selection.front()].key;
            pianoCursor = p->notes[(std::size_t) selection.front()].start;
        }
        speak (selectedText());
    }
}
} // namespace lynx
