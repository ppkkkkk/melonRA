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

#include "Rewind.h"

#include <cstring>
#include <zstd.h>

#include "NDS.h"
#include "Savestate.h"
#include "RetroAchievements/RAClient.h"

using namespace melonDS;

RewindBuffer::RewindBuffer()
{
    worker = std::thread([this] { work(); });
}

RewindBuffer::~RewindBuffer()
{
    {
        std::lock_guard<std::mutex> g(lock);
        quit = true;
    }
    wake.notify_all();
    worker.join();
}

void RewindBuffer::setLength(int seconds)
{
    std::lock_guard<std::mutex> g(lock);
    maxSnapshots = (size_t)std::max(1, seconds * 60 / kInterval);
    while (snapshots.size() > maxSnapshots) snapshots.pop_front();
}

void RewindBuffer::frameDone(NDS* nds, RA::Manager* ra)
{
    if (++frames < kInterval) return;
    frames = 0;

    {
        // still compressing the last one (a slow computer): skip this one
        std::lock_guard<std::mutex> g(lock);
        if (pending) return;
    }

    // one buffer, reused: a fresh state (with its header) is written into it
    if (scratch.empty()) scratch.resize(Savestate::DEFAULT_SIZE);
    Savestate state(scratch.data(), (u32)scratch.size(), true);
    if (state.Error || !nds->DoSavestate(&state) || state.Error) return;

    auto snap = std::make_unique<Snapshot>();
    snap->rawSize = state.Length();
    snap->data.assign(scratch.data(), scratch.data() + state.Length());
    if (ra) snap->progress = ra->serializeProgress();

    {
        std::lock_guard<std::mutex> g(lock);
        pending = std::move(snap);
    }
    wake.notify_one();
}

void RewindBuffer::work()
{
    std::unique_lock<std::mutex> g(lock);
    for (;;)
    {
        wake.wait(g, [this] { return quit || pending; });
        if (quit) return;

        std::unique_ptr<Snapshot> snap = std::move(pending);
        busy = true;
        g.unlock();

        std::vector<u8> out(ZSTD_compressBound(snap->data.size()));
        const size_t len = ZSTD_compress(out.data(), out.size(), snap->data.data(), snap->data.size(), 1);
        const bool ok = !ZSTD_isError(len);
        if (ok)
        {
            out.resize(len);
            out.shrink_to_fit();
            snap->data = std::move(out);
        }

        g.lock();
        if (ok)
        {
            snapshots.push_back(std::move(*snap));
            while (snapshots.size() > maxSnapshots) snapshots.pop_front();
        }
        busy = false;
        idle.notify_all();
    }
}

bool RewindBuffer::stepBack(NDS* nds, RA::Manager* ra)
{
    Snapshot snap;
    {
        std::unique_lock<std::mutex> g(lock);
        idle.wait(g, [this] { return !busy && !pending; });
        if (snapshots.empty()) return false;
        snap = std::move(snapshots.back());
        snapshots.pop_back();
    }
    frames = 0;

    std::vector<u8> raw(snap.rawSize);
    const size_t len = ZSTD_decompress(raw.data(), raw.size(), snap.data.data(), snap.data.size());
    if (ZSTD_isError(len) || len != snap.rawSize) return false;

    Savestate load(raw.data(), (u32)raw.size(), false);
    if (!nds->DoSavestate(&load) || load.Error) return false;
    if (ra) ra->deserializeProgress(snap.progress.empty() ? nullptr : snap.progress.data(), snap.progress.size());
    return true;
}

void RewindBuffer::clear()
{
    std::unique_lock<std::mutex> g(lock);
    idle.wait(g, [this] { return !busy; });
    pending.reset();
    snapshots.clear();
    frames = 0;
}

size_t RewindBuffer::size()
{
    std::lock_guard<std::mutex> g(lock);
    return snapshots.size();
}
