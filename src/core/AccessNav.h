#pragma once

// Navigation for the screen reader layer (ui/Accessibility.h): where the keyboard cursor goes next in the Playlist
// and the Piano Roll, and how a position reads. No JUCE, so the tests cover it.

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::accessnav
{
// A song position as bar, beat (quarter notes, from 1) and the ticks past that beat, following the markers' time
// signature changes.
struct BarBeat
{
    int bar = 1, beat = 1;
    std::int64_t ticks = 0;
};
BarBeat barBeatAt (const Song& song, std::int64_t tick);

// Playlist clips of `track` in start order (indices into `clips`).
std::vector<int> clipsOnTrack (const std::vector<Clip>& clips, int track);
// The clip of `track` after (direction > 0) or before (< 0) the one starting at `from`: the first starting later /
// the last starting earlier. -1 when there is none.
int nextClip (const std::vector<Clip>& clips, int track, std::int64_t from, int direction);
// The clip of `track` playing at `tick` (the latest to start when several overlap), or -1.
int clipAt (const std::vector<Clip>& clips, int track, std::int64_t tick);

// The notes of `channel` starting at `start`, lowest key first (a chord; one note is a chord of one).
std::vector<int> chordAt (const Pattern& pattern, int channel, std::int64_t start);
// The start of the next (direction > 0) or previous (< 0) chord of `channel` after / before `from`; -1 when none.
std::int64_t nextChordStart (const Pattern& pattern, int channel, std::int64_t from, int direction);
// The note of `channel` on `key` that sounds at `tick` (the latest to start), or -1.
int noteAt (const Pattern& pattern, int channel, int key, std::int64_t tick);
} // namespace lynx::accessnav
