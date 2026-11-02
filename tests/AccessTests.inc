// The screen reader layer (ui/Accessibility.h): its keys and the navigation helpers (core/AccessNav.h).

// Its keys are a layer of their own: found only by findAccess, and never clashing with the panels' keys.
TEST (access_keys_are_a_layer_of_their_own)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.findAccess ({ up }, Context::playlist, false) == "access.playlist.previousTrack");
    CHECK (map.findAccess ({ up }, Context::pianoRoll, false) == "access.pianoRoll.up");
    CHECK (map.findAccess ({ 'P', alt | shift }, Context::mixer, false) == "access.reportPosition"); // global ones everywhere
    CHECK (map.find ({ up }, Context::playlist, false).rfind ("access.", 0) == std::string::npos);
    CHECK (map.findAccess ({ 'C' }, Context::playlist, false).empty()); // the Knife stays a panel key
    CHECK (map.find ({ 'A', ctrl | alt | shift }, Context::playlist, false) == "access.toggle"); // works with the layer off
}

TEST (access_positions_follow_time_signatures)
{
    using namespace lynx;
    Song song;
    song.beatsPerBar = 4;
    const std::int64_t bar = 4 * ppq;
    CHECK (accessnav::barBeatAt (song, 0).bar == 1);
    const auto p = accessnav::barBeatAt (song, bar + 2 * ppq + ppq / 2);
    CHECK (p.bar == 2 && p.beat == 3 && p.ticks == ppq / 2);

    Marker threeFour;
    threeFour.tick = 2 * bar;
    threeFour.numerator = 3;
    song.addMarker (threeFour);
    const auto q = accessnav::barBeatAt (song, 2 * bar + 3 * ppq); // the first beat of bar 4 in 3/4
    CHECK (q.bar == 4 && q.beat == 1 && q.ticks == 0);
}

TEST (access_clips_and_chords_in_order)
{
    using namespace lynx;
    auto clip = [] (int track, std::int64_t start) {
        Clip c;
        c.track = track;
        c.start = start;
        c.length = 100;
        return c;
    };
    const std::vector<Clip> clips { clip (0, 500), clip (1, 0), clip (0, 0), clip (0, 250) };
    CHECK ((accessnav::clipsOnTrack (clips, 0) == std::vector<int> { 2, 3, 0 }));
    CHECK (accessnav::nextClip (clips, 0, -1, 1) == 2);
    CHECK (accessnav::nextClip (clips, 0, 0, 1) == 3);
    CHECK (accessnav::nextClip (clips, 0, 500, 1) == -1);
    CHECK (accessnav::nextClip (clips, 0, 500, -1) == 3);
    CHECK (accessnav::clipAt (clips, 0, 299) == 3);
    CHECK (accessnav::clipAt (clips, 0, 350) == -1);

    Pattern p;
    auto note = [&p] (int channel, int key, std::int64_t start) {
        Note n;
        n.channel = channel;
        n.key = key;
        n.start = start;
        n.length = 240;
        p.notes.push_back (n);
    };
    note (0, 64, 0);
    note (0, 60, 0);
    note (1, 50, 120); // another channel: skipped
    note (0, 67, 480);
    CHECK ((accessnav::chordAt (p, 0, 0) == std::vector<int> { 1, 0 })); // lowest key first
    CHECK (accessnav::nextChordStart (p, 0, 0, 1) == 480);
    CHECK (accessnav::nextChordStart (p, 0, 480, -1) == 0);
    CHECK (accessnav::nextChordStart (p, 0, 480, 1) == -1);
    CHECK (accessnav::noteAt (p, 0, 60, 239) == 1);
    CHECK (accessnav::noteAt (p, 0, 60, 240) == -1);
}
