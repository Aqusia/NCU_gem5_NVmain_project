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
 * Declaration of a Frequency-Based Replacement policy with aging.
 *
 * The victim is chosen by reference frequency, like LFU. Unlike plain LFU,
 * every block's reference count decays (is halved) once per "decay epoch"
 * in which the block was not referenced. This prevents stale-but-formerly-hot
 * blocks from holding the cache forever and lets newly hot blocks compete.
 *
 * One decay epoch = decay_period accesses (touch + reset) to this policy.
 * Decay is applied lazily (only when a block is touched or evaluated as a
 * victim), so no periodic full-array scan is needed.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_FBR_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_FBR_RP_HH__

#include "mem/cache/replacement_policies/base.hh"

struct FBRRPParams;

class FBRRP : public BaseReplacementPolicy
{
  protected:
    /** FBR-specific implementation of replacement data. */
    struct FBRReplData : ReplacementData
    {
        /** Number of references to this entry since it was reset. */
        unsigned refCount;

        /** Decay epoch in which refCount was last brought up to date. */
        uint64_t lastEpoch;

        /** Default constructor. Invalidate data. */
        FBRReplData() : refCount(0), lastEpoch(0) {}
    };

    /**
     * Global access counter, shared by all sets. Incremented on every
     * touch and reset. Used to derive the current decay epoch.
     * Mutable because the replacement-policy interface is const.
     */
    mutable uint64_t globalAccesses;

    /** Number of accesses that make up one decay epoch. */
    const uint64_t decayPeriod;

    /** Current decay epoch number. */
    uint64_t currentEpoch() const { return globalAccesses / decayPeriod; }

    /**
     * Bring a block's refCount up to date: halve it once for each whole
     * decay epoch that elapsed since it was last referenced.
     */
    void applyDecay(const std::shared_ptr<FBRReplData>& data) const;

  public:
    /** Convenience typedef. */
    typedef FBRRPParams Params;

    FBRRP(const Params *p);

    ~FBRRP() {}

    /**
     * Invalidate replacement data to set it as the next probable victim.
     * Clear the number of references.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                              const override;

    /**
     * Touch an entry to update its replacement data.
     * Apply pending decay, then increase the reference count.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Set the reference count to 1 and stamp the current epoch.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;

    /**
     * Find replacement victim using decayed reference frequency.
     * The candidate with the lowest aged refCount is evicted.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /** Instantiate a replacement data entry. */
    std::shared_ptr<ReplacementData> instantiateEntry() override;
};

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_FBR_RP_HH__
