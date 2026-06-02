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

#include "mem/cache/replacement_policies/fbr_rp.hh"

#include <cassert>
#include <memory>

#include "params/FBRRP.hh"

FBRRP::FBRRP(const Params *p)
    : BaseReplacementPolicy(p),
      globalAccesses(0),
      decayPeriod(p->decay_period == 0 ? 1 : p->decay_period)
{
}

void
FBRRP::applyDecay(const std::shared_ptr<FBRReplData>& data) const
{
    uint64_t epoch = currentEpoch();
    if (epoch > data->lastEpoch) {
        uint64_t shift = epoch - data->lastEpoch;
        // Halve the reference count once per elapsed epoch. A shift of 32 or
        // more drives any 32-bit count to zero, so saturate to avoid UB.
        data->refCount = (shift >= 32) ? 0 : (data->refCount >> shift);
        data->lastEpoch = epoch;
    }
}

void
FBRRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
const
{
    // Reset reference count so this block is the most likely next victim.
    std::static_pointer_cast<FBRReplData>(replacement_data)->refCount = 0;
}

void
FBRRP::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    std::shared_ptr<FBRReplData> data =
        std::static_pointer_cast<FBRReplData>(replacement_data);

    // Advance global time, decay any stale frequency, then count this access.
    globalAccesses++;
    applyDecay(data);
    data->refCount++;
}

void
FBRRP::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    std::shared_ptr<FBRReplData> data =
        std::static_pointer_cast<FBRReplData>(replacement_data);

    globalAccesses++;
    data->refCount = 1;
    data->lastEpoch = currentEpoch();
}

ReplaceableEntry*
FBRRP::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Decay every candidate to "now" before comparing, so a formerly hot
    // block that has gone cold is correctly seen as a low-frequency block.
    ReplaceableEntry* victim = candidates[0];
    std::shared_ptr<FBRReplData> victimData =
        std::static_pointer_cast<FBRReplData>(victim->replacementData);
    applyDecay(victimData);

    for (const auto& candidate : candidates) {
        std::shared_ptr<FBRReplData> candData =
            std::static_pointer_cast<FBRReplData>(candidate->replacementData);
        applyDecay(candData);

        if (candData->refCount < victimData->refCount) {
            victim = candidate;
            victimData = candData;
        }
    }

    return victim;
}

std::shared_ptr<ReplacementData>
FBRRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new FBRReplData());
}

FBRRP*
FBRRPParams::create()
{
    return new FBRRP(this);
}
