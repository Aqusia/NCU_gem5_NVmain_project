# gem5 Source Overlay

This folder contains **only** the gem5 source files changed for the assignment,
stored at the same relative paths as the original gem5 tree. It is an overlay,
**not** a complete standalone gem5 tree.

Each section below lists the files touched per question and explains **what was
changed and how it works** — the "how I did it" for the code.

## Applying the overlay

`setup.sh` (in the repo root) does this automatically. To do it by hand onto an
existing full gem5 checkout:

```bash
cp -r configs src /path/to/your/gem5/
cd /path/to/your/gem5
scons EXTRAS=../NVmain build/X86/gem5.opt -j"$(nproc)"
```

Rebuilding is required after applying the overlay (the Q4/Q5/Bonus changes are
C++). Q2's changes are Python config only, but a full `gem5.opt` already
includes them.

## NVMain compatibility

`configs/common/Options.py` carries the NVMain compatibility logic that accepts
the dynamic `--nvmain-*` configuration overrides. This is required to build the
clean upstream gem5 commit together with NVMain (`scons EXTRAS=../NVmain`).

---

## Q2 — L3 last-level cache wiring

Files:

- `configs/common/Caches.py` — adds the `L3Cache` class (and an `L3XBar`).
- `configs/common/Options.py` — adds the `--l3cache`, `--l3_size`, `--l3_assoc` options.
- `configs/common/CacheConfig.py` — connects the topology.
- `src/cpu/BaseCPU.py` — adds the L3 hierarchy connection points.
- `src/mem/XBar.py` — adds the L3 crossbar definition.

How it works: stock gem5 only knows about L1 and L2. To insert a last-level
cache, `Caches.py` defines an `L3Cache` (size/assoc/latency defaults), and
`CacheConfig.py` rewires the data path so that instead of **L2 → membus** it
becomes **L2 → L3XBar → L3 → membus**. `BaseCPU.py` and `XBar.py` provide the
ports/crossbar that connection hangs off. `Options.py` exposes the knobs so the
hierarchy is configurable from the command line (`--l3cache`, `--l3_assoc=N`,
`--l3_size=…`).

One guard worth noting: requesting `--l3cache` **without** `--l2cache` is a
`fatal()` in `CacheConfig.py` rather than a silent no-op, so a malformed run
can't quietly produce L2-only results while looking like it has an L3.

Q3 reuses this L3 unchanged and only varies `--l3_assoc`, so it adds no core
gem5 code of its own.

---

## Q4 — FBR replacement policy (frequency-based, with aging)

Files:

- `src/mem/cache/replacement_policies/fbr_rp.cc` — the policy implementation.
- `src/mem/cache/replacement_policies/fbr_rp.hh` — its declaration.
- `src/mem/cache/replacement_policies/ReplacementPolicies.py` — registers the `FBRRP` SimObject.
- `src/mem/cache/replacement_policies/SConscript` — adds `fbr_rp.cc` to the build.
- `configs/common/Options.py` — adds `--l3_repl` (LRU/FBR) and `--l3_decay_period`.
- `configs/common/CacheConfig.py` — selects the policy from `--l3_repl`.

### The idea

Plain **LFU** (least-frequently-used) evicts the block with the lowest reference
count. Its classic flaw: a block that was hot early in the run accumulates a huge
count and then **squats in the cache forever**, even after it goes cold, because
newcomers can't out-count it. **FBR fixes this with aging** — every block's count
**decays over time** so stale popularity fades and recently-hot blocks can win.

### Per-block state (`fbr_rp.hh`)

```cpp
struct FBRReplData : ReplacementData {
    unsigned refCount;   // references since last reset
    uint64_t lastEpoch;  // the decay epoch refCount was last brought up to date
};
```

Plus one shared counter on the policy object:

```cpp
mutable uint64_t globalAccesses;          // ++ on every touch and reset
const   uint64_t decayPeriod;             // accesses per decay epoch (--l3_decay_period)
uint64_t currentEpoch() const { return globalAccesses / decayPeriod; }
```

So "time" is measured in **accesses**, and one **epoch** is `decayPeriod`
accesses (default 100000). `currentEpoch()` is just `globalAccesses /
decayPeriod`.

### Aging = lazy halving (`applyDecay`)

```cpp
void FBRRP::applyDecay(data) const {
    uint64_t epoch = currentEpoch();
    if (epoch > data->lastEpoch) {
        uint64_t shift = epoch - data->lastEpoch;       // epochs skipped while idle
        data->refCount = (shift >= 32) ? 0 : (data->refCount >> shift);  // halve per epoch
        data->lastEpoch = epoch;
    }
}
```

The reference count is **halved once for every whole epoch the block went
untouched** (a right shift by the number of elapsed epochs; ≥32 saturates to 0
to avoid undefined behavior). Crucially this is **lazy**: decay is applied only
when a block is touched or evaluated as a victim — there is **no periodic scan of
the whole cache**, so aging costs nothing until a block is actually looked at.

### The hooks

```cpp
touch(data):       globalAccesses++; applyDecay(data); data->refCount++;   // a hit: age, then count it
reset(data):       globalAccesses++; data->refCount = 1; data->lastEpoch = currentEpoch();  // insert
invalidate(data):  data->refCount = 0;                                     // make it the prime victim
```

### Choosing the victim (`getVictim`)

```cpp
// Decay every candidate up to "now" first, THEN pick the lowest count.
for (cand : candidates) { applyDecay(cand); if (cand->refCount < victim->refCount) victim = cand; }
```

Because `applyDecay` runs on each candidate before the comparison, a block that
was hot long ago but has since gone cold is correctly seen as low-frequency and
becomes evictable — that's the aging paying off at eviction time.

`Options.py`/`CacheConfig.py` wire `--l3_repl=FBR` to instantiate `FBRRP` and
pass `--l3_decay_period` into `decayPeriod`; `ReplacementPolicies.py` +
`SConscript` make it a buildable, selectable gem5 SimObject.

---

## Q5 — Write-through policy

Files:

- `configs/common/Options.py` — adds `--write_through`.
- `configs/common/CacheConfig.py` — propagates it to the L1D/L2/L3 hierarchy.
- `src/mem/cache/Cache.py` — adds the SimObject param `write_through = Param.Bool(False, …)`.
- `src/mem/cache/base.hh` — adds the C++ member `const bool writeThrough;`.
- `src/mem/cache/base.cc` — the actual behavior (`handleWriteThrough`).
- `src/mem/cache/cache.cc` — calls it after a miss fill.

### What changes

In stock gem5 a write **hit** just marks the block dirty (`BlkDirty`) and memory
is updated lazily, only when the block is evicted (write-back). Write-through
means memory must be updated on **every** write. The new `handleWriteThrough()`
in `base.cc` implements exactly that:

```cpp
void BaseCache::handleWriteThrough(PacketPtr pkt, CacheBlk *blk, PacketList &writebacks) {
    if (writeThrough && blk && blk->isDirty() && pkt->isWrite()) {
        Addr wt_addr = regenerateBlkAddr(blk);

        // Coalescing write buffer: if a write-through for this line is already
        // pending, just refresh its payload so the newest store still reaches
        // memory; otherwise enqueue a fresh WriteClean packet downward.
        WriteQueueEntry *wt_entry = writeBuffer.findMatch(wt_addr, blk->isSecure());
        PacketPtr pending = wt_entry ? wt_entry->getTarget()->pkt : nullptr;
        if (pending && pending->cmd == MemCmd::WriteClean && pending->writeThrough()) {
            pending->setDataFromBlock(blk->data, blkSize);          // refresh in place
        } else {
            PacketPtr wt = new Packet(req, MemCmd::WriteClean, blkSize, pkt->id);
            wt->setWriteThrough();
            wt->setDataFromBlock(blk->data, blkSize);
            writebacks.push_back(wt);                               // send a clean copy down
        }
        blk->status &= ~BlkDirty;     // memory is (or will be) current -> keep our copy CLEAN
    }
}
```

Two key points:

1. **`WriteClean`, not `Writeback`.** The data goes down as a `MemCmd::WriteClean`
   packet (gem5's native "push a clean copy to memory" command) and our own block
   is immediately un-dirtied (`status &= ~BlkDirty`). So under write-through there
   are essentially **no dirty evictions** — `system.l3.writebacks::total` stays ~0
   and the traffic instead shows up as `system.l3.WriteClean_accesses::total`,
   which equals the memory write count.
2. **Coalescing write buffer.** If a `WriteClean` for the same line is still
   draining toward memory, its payload is overwritten with the latest block data
   instead of queuing a second packet — this models a realistic write buffer and
   makes sure the most recent store is the one that lands.

It is called from **two** places so both ways a write can land trigger a clean
push-down:

- `base.cc` (the write-hit path) — right after the store dirties the block.
- `cache.cc` (`handleWriteThrough(tgt_pkt, blk, writebacks)` after a **miss fill**)
  — so a write that missed, allocated, and filled also writes through.

`CacheConfig.py` only enables this on the data-side hierarchy (L1D/L2/L3) when
`--write_through` is set; instruction caches are read-only and unaffected.

### Why two files are NOT in the overlay

The upstream gem5 commit already ships the `WRITE_THROUGH` packet flag in
`src/mem/packet.hh` (with `setWriteThrough()` / `writeThrough()`), and
`src/mem/cache/write_queue_entry.cc` already lets `WriteClean` packets sit in the
write queue. Both are unchanged from upstream, so they are deliberately left out
of this overlay — only the files we actually edited are included.

---

## Bonus — Writeback-aware (clean-preferring) replacement policy

Files:

- `src/mem/cache/replacement_policies/wbaware_rp.cc` — the policy implementation.
- `src/mem/cache/replacement_policies/wbaware_rp.hh` — its declaration.
- `src/mem/cache/replacement_policies/ReplacementPolicies.py` — registers `WBAwareRP`.
- `src/mem/cache/replacement_policies/SConscript` — adds `wbaware_rp.cc` to the build.
- `configs/common/Options.py` — adds `WBA` to the `--l3_repl` choices.
- `configs/common/CacheConfig.py` — maps `--l3_repl=WBA` to `WBAwareRP`.

### The idea

PCM write energy is much higher than read energy, so the cheapest block to evict
from the last-level cache is a **clean** one: clean means memory already holds an
up-to-date copy, so evicting it costs **no** PCM write. `WBAwareRP` is therefore
a **clean-preferring LRU**.

### Per-block state (`wbaware_rp.hh`)

```cpp
struct WBAwareReplData : ReplacementData {
    Tick lastTouchTick;   // plain LRU timestamp
};
```

`touch` and `reset` stamp `lastTouchTick = curTick()`; `invalidate` sets it to 0.
So the recency bookkeeping is exactly LRU — the twist is only in victim choice.

### Choosing the victim (`wbaware_rp.cc`)

```cpp
ReplaceableEntry* WBAwareRP::getVictim(candidates) const {
    ReplaceableEntry* lruVictim   = candidates[0];   // overall LRU
    ReplaceableEntry* cleanVictim = nullptr;         // LRU among clean blocks
    for (cand : candidates) {
        Tick t = data(cand)->lastTouchTick;
        if (t < data(lruVictim)->lastTouchTick) lruVictim = cand;     // track overall LRU
        CacheBlk* blk = static_cast<CacheBlk*>(cand);
        if (!blk->isDirty() &&                                        // clean candidate?
            (cleanVictim == nullptr || t < data(cleanVictim)->lastTouchTick))
            cleanVictim = cand;                                       // track LRU-clean
    }
    return (cleanVictim != nullptr) ? cleanVictim : lruVictim;        // prefer clean
}
```

In one pass it finds both the overall LRU block and the LRU block **among the
clean ones**, then evicts the clean one if any exists. Only when **every**
candidate in the set is dirty does it fall back to plain LRU. The effect: dirty
lines are kept longer, their writes coalesce, and the number of PCM write
operations (and thus write energy) drops — ~82% fewer memory writes and ~20%
lower NVMain rank energy on the quicksort run (see [../data/README.md](../data/README.md)).

`Options.py` adds `WBA` to the `--l3_repl` choices and `CacheConfig.py` selects
`WBAwareRP()` for it; `ReplacementPolicies.py` + `SConscript` make it a buildable,
selectable SimObject — exactly mirroring how FBR was added in Q4.

---

## Source comparison

These files were diffed against a clean clone of upstream gem5 at commit
`525ce650e1a5bbe71c39d4b15598d6c003cc9f9e`:

```bash
diff -qr --exclude=.git --exclude=build --exclude=m5out <original-gem5> <modified-gem5>
```

The files listed above are the complete, meaningful source differences for Q2,
Q4, Q5, and the Bonus.
