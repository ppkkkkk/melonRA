/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef REWIND_H
#define REWIND_H

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "types.h"

namespace melonDS { class NDS; class Savestate; }
namespace RA { class Manager; }

// The last stretch of play, kept in memory as compressed save states taken
// every half second, so the game can be stepped back through. Compression runs
// on a thread of its own so taking a snapshot costs the emulation little.
class RewindBuffer
{
public:
    static constexpr int kInterval = 30;   // frames between snapshots

    RewindBuffer();
    ~RewindBuffer();

    // how many seconds to keep
    void setLength(int seconds);

    // emulation thread, after each emulated frame
    void frameDone(melonDS::NDS* nds, RA::Manager* ra);

    // goes back to the newest snapshot (and drops it); false if there's none
    bool stepBack(melonDS::NDS* nds, RA::Manager* ra);

    void clear();
    size_t size();

private:
    struct Snapshot
    {
        std::vector<melonDS::u8> data;     // zstd-compressed state
        size_t rawSize = 0;
        std::vector<melonDS::u8> progress; // RetroAchievements runtime state
    };

    void work();

    std::vector<melonDS::u8> scratch;
    int frames = 0;
    size_t maxSnapshots = 60;

    std::mutex lock;
    std::condition_variable wake, idle;
    std::deque<Snapshot> snapshots;
    std::unique_ptr<Snapshot> pending;   // waiting to be compressed
    bool busy = false;
    bool quit = false;
    std::thread worker;
};

#endif // REWIND_H
