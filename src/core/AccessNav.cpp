#include "core/AccessNav.h"

#include <algorithm>

namespace lynx::accessnav
{
BarBeat barBeatAt (const Song& song, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    const auto bar = song.barAt (tick);
    const auto into = std::max<std::int64_t> (0, tick - bar.tick);
    return { bar.number, (int) (into / ppq) + 1, into % ppq };
}

std::vector<int> clipsOnTrack (const std::vector<Clip>& clips, int track)
{
    std::vector<int> out;
    for (int i = 0; i < (int) clips.size(); ++i)
        if (clips[(std::size_t) i].track == track)
            out.push_back (i);
    std::stable_sort (out.begin(), out.end(), [&clips] (int a, int b) { return clips[(std::size_t) a].start < clips[(std::size_t) b].start; });
    return out;
}

int nextClip (const std::vector<Clip>& clips, int track, std::int64_t from, int direction)
{
    const auto onTrack = clipsOnTrack (clips, track);

    if (direction > 0)
    {
        for (auto i : onTrack)
            if (clips[(std::size_t) i].start > from)
                return i;
        return -1;
    }

    for (auto it = onTrack.rbegin(); it != onTrack.rend(); ++it)
        if (clips[(std::size_t) *it].start < from)
            return *it;
    return -1;
}

int clipAt (const std::vector<Clip>& clips, int track, std::int64_t tick)
{
    int found = -1;
    for (auto i : clipsOnTrack (clips, track))
        if (clips[(std::size_t) i].start <= tick && tick < clips[(std::size_t) i].end())
            found = i;
    return found;
}

std::vector<int> chordAt (const Pattern& pattern, int channel, std::int64_t start)
{
    std::vector<int> out;
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
        if (pattern.notes[(std::size_t) i].channel == channel && pattern.notes[(std::size_t) i].start == start)
            out.push_back (i);
    std::sort (out.begin(), out.end(), [&pattern] (int a, int b) { return pattern.notes[(std::size_t) a].key < pattern.notes[(std::size_t) b].key; });
    return out;
}

std::int64_t nextChordStart (const Pattern& pattern, int channel, std::int64_t from, int direction)
{
    std::int64_t best = -1;
    for (const auto& n : pattern.notes)
    {
        if (n.channel != channel)
            continue;
        if (direction > 0 ? n.start > from && (best < 0 || n.start < best) : n.start < from && n.start > best)
            best = n.start;
    }
    return best;
}

int noteAt (const Pattern& pattern, int channel, int key, std::int64_t tick)
{
    int found = -1;
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        if (n.channel == channel && n.key == key && n.start <= tick && tick < n.start + std::max<std::int64_t> (1, n.length)
            && (found < 0 || n.start >= pattern.notes[(std::size_t) found].start))
            found = i;
    }
    return found;
}
int noteStartingAt (const Pattern& pattern, int channel, int key, std::int64_t tick)
{
    for (int i = 0; i < (int) pattern.notes.size(); ++i)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        if (n.channel == channel && n.key == key && n.start == tick)
            return i;
    }
    return -1;
}
} // namespace lynx::accessnav
