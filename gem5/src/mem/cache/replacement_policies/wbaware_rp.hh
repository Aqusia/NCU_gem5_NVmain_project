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

/**
 * @file
 * Declaration of a Writeback-Aware (clean-preferring) LRU replacement policy.
 *
 * Bonus: designed to lower the energy of a PCM-based main memory. PCM write
 * energy is far higher than read energy, so this policy tries to avoid dirty
 * writebacks from the last-level cache: among the LRU candidates it evicts the
 * least-recently-used CLEAN block first (a clean block has an up-to-date copy
 * in memory, so evicting it costs no PCM write). Only when every candidate is
 * dirty does it fall back to plain LRU. Dirty lines are thus kept longer,
 * coalescing writes and reducing the number of PCM write operations.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_WBAWARE_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_WBAWARE_RP_HH__

#include "mem/cache/replacement_policies/base.hh"

struct WBAwareRPParams;

class WBAwareRP : public BaseReplacementPolicy
{
  protected:
    /** Writeback-aware replacement data: a plain LRU timestamp. */
    struct WBAwareReplData : ReplacementData
    {
        /** Tick on which the entry was last touched. */
        Tick lastTouchTick;

        /** Default constructor. Invalidate data. */
        WBAwareReplData() : lastTouchTick(0) {}
    };

  public:
    /** Convenience typedef. */
    typedef WBAwareRPParams Params;

    WBAwareRP(const Params *p);

    ~WBAwareRP() {}

    /**
     * Invalidate replacement data to set it as the next probable victim.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                              const override;

    /**
     * Touch an entry to update its last touch tick.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Find replacement victim: the LRU clean block if any clean candidate
     * exists, otherwise the overall LRU block.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /** Instantiate a replacement data entry. */
    std::shared_ptr<ReplacementData> instantiateEntry() override;
};

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_WBAWARE_RP_HH__
