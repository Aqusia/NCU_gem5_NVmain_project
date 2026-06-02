/**
 * Copyright (c) 2018 Inria
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/replacement_policies/wbaware_rp.hh"

#include <cassert>
#include <memory>

#include "mem/cache/blk.hh"
#include "params/WBAwareRP.hh"

WBAwareRP::WBAwareRP(const Params *p)
    : BaseReplacementPolicy(p)
{
}

void
WBAwareRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
const
{
    // Reset last touch timestamp
    std::static_pointer_cast<WBAwareReplData>(
        replacement_data)->lastTouchTick = Tick(0);
}

void
WBAwareRP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Update last touch timestamp
    std::static_pointer_cast<WBAwareReplData>(
        replacement_data)->lastTouchTick = curTick();
}

void
WBAwareRP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Set last touch timestamp
    std::static_pointer_cast<WBAwareReplData>(
        replacement_data)->lastTouchTick = curTick();
}

ReplaceableEntry*
WBAwareRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Track the LRU candidate overall, and the LRU candidate among clean
    // blocks. Evicting a clean block costs no PCM write, so we prefer it.
    ReplaceableEntry* lruVictim = candidates[0];
    ReplaceableEntry* cleanVictim = nullptr;

    for (const auto& candidate : candidates) {
        Tick tick = std::static_pointer_cast<WBAwareReplData>(
                        candidate->replacementData)->lastTouchTick;

        // Overall LRU.
        if (tick < std::static_pointer_cast<WBAwareReplData>(
                       lruVictim->replacementData)->lastTouchTick) {
            lruVictim = candidate;
        }

        // LRU among clean blocks. The candidate is a CacheBlk in the cache,
        // so we can inspect its dirty state.
        CacheBlk* blk = static_cast<CacheBlk*>(candidate);
        if (!blk->isDirty()) {
            if (cleanVictim == nullptr ||
                tick < std::static_pointer_cast<WBAwareReplData>(
                           cleanVictim->replacementData)->lastTouchTick) {
                cleanVictim = candidate;
            }
        }
    }

    // Prefer evicting a clean block (no PCM writeback). If every candidate is
    // dirty, fall back to the overall LRU block.
    return (cleanVictim != nullptr) ? cleanVictim : lruVictim;
}

std::shared_ptr<ReplacementData>
WBAwareRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new WBAwareReplData());
}

WBAwareRP*
WBAwareRPParams::create()
{
    return new WBAwareRP(this);
}
