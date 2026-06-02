# Simulation Data

Verified, clean run outputs for Q1-Q5 and the Bonus. This file explains **how each run was
produced and how to read the numbers**; the matching code changes are described
in [../gem5/README.md](../gem5/README.md).

## Structure

- `Q1/`: hello world baseline.
- `Q2/`: hello world with L3 enabled.
- `Q3/2way/`: quicksort with 2-way L3.
- `Q3/fullway/`: quicksort with full-way L3.
- `Q4/LRU/`: quicksort with LRU replacement.
- `Q4/FBR/`: quicksort with FBR (frequency-based + aging) replacement.
- `Q5/writeback/`: multiply with write-back cache policy.
- `Q5/writethrough/`: multiply with write-through cache policy.
- `Bonus/LRU_baseline/`: quicksort with plain LRU (bonus baseline).
- `Bonus/modified/`: quicksort with the writeback-aware (WBA) policy.
- `benchmarks/`: `quicksort.c` and `multiply.c` (auto-compiled by `gem5sim`).

Every result directory contains only:

- `output.log` — full terminal stdout/stderr (this is the **only** place NVMain
  energy numbers appear; they are not written to `stats.txt`).
- `stats.txt` — gem5's statistics dump (cache hits/misses, memory traffic, cycles).

## Experiment Settings

All runs use the PCM configuration `NVmain/Config/PCM_ISSCC_2012_4GB.config` and
this cache hierarchy:

| Cache | Size | Associativity |
|---|---:|---:|
| L1-I | 32kB | 2 |
| L1-D | 64kB | 2 |
| L2 | 128kB | 8 |
| L3 | 256kB | 16 |

The 256kB L3 is intentional: the quicksort working set (~400 kB) is larger than
both L2 and L3, so the Q3 associativity and Q4 replacement-policy experiments
exercise real L3 evictions instead of sitting entirely in cache. With 64-byte
cache lines, `--assoc full` expands to `4096` ways.

## Reproduction Commands

From the repo root, after `./setup.sh` and `source env.sh` (see the top-level
README), either run everything with `./run_all.sh` or one question at a time:

```bash
gem5sim hello --save Q1
gem5sim hello --l3 --save Q2
gem5sim quicksort --l3 --assoc 2    --save Q3/2way
gem5sim quicksort --l3 --assoc full --save Q3/fullway
gem5sim quicksort --l3 --repl LRU --save Q4/LRU
gem5sim quicksort --l3 --repl FBR --save Q4/FBR
gem5sim multiply --l3 --assoc 4      --save Q5/writeback
gem5sim multiply --l3 --assoc 4 --wt --save Q5/writethrough
```

`gem5sim` auto-detects paths from `env.sh`; override `GEM5_DIR`, `NVMAIN_CONFIG`,
`ANSWER_DIR`, or `BENCHMARK_DIR` if your layout differs.

---

## Q1 — Baseline

`hello` (gem5's built-in test program) on the L1+L2 hierarchy with the PCM main
memory, no L3. This is the "is the environment alive" run: it confirms gem5 +
NVMain are wired together and that `stats.txt` carries cache hit/miss counters
and the log carries NVMain energy.

## Q2 — L3 enabled

Same `hello`, now with `--l3` so the new L3 last-level cache is inserted between
L2 and the memory bus. `hello` is tiny, so this run is about **proving the L3
exists and is connected** (it shows up as `system.l3.*` in `stats.txt`), not
about stressing it. The real L3 stress starts in Q3.

## Q3 — L3 associativity (2-way vs full-way)

How it was run: same quicksort, same 256kB L3, only the associativity changes
(`--assoc 2` vs `--assoc full`).

| L3 associativity | L3 hits | L3 misses | L3 miss rate |
|---|---:|---:|---:|
| 2-way | 8040 | 15783 | 0.662511 |
| full-way | 7255 | 16568 | 0.695462 |

Both runs issue the **same total** number of L3 accesses, which confirms they
execute the identical workload. Counter-intuitively, **2-way slightly beats
full-way** here: when a linear scan touches more distinct blocks than the cache
can hold, global LRU over a fully-associative array evicts blocks in a different
order than set-partitioned LRU, and for this access pattern the set-partitioned
version happens to retain more useful blocks. This is the same family of effect
as Belady's anomaly and is worth a sentence in a write-up.

## Q4 — FBR vs LRU (frequency-based replacement with aging)

How it was run: quicksort on the 256kB L3, switching only the replacement policy
(`--repl LRU` vs `--repl FBR`; FBR uses the default `--decay 100000`).

| L3 policy | L3 hits | L3 misses | L3 miss rate | Memory writes | NVMain energy |
|---|---:|---:|---:|---:|---:|
| LRU | 7356 | 16467 | 0.691223 | 11948 | 2.10693e+06nJ |
| FBR | 12235 | 11584 | 0.486334 | 5169 | 1.80913e+06nJ |

FBR keeps frequently-reused blocks (e.g. the pivots and the partition
boundaries revisited across recursion) instead of evicting them on recency
alone, cutting the L3 miss rate by **~29.6%** and NVMain energy by **~14.1%** on
this run. The **aging/decay** mechanism is what stops a block that was hot early
on from squatting in the cache forever — see the algorithm walk-through in
[../gem5/README.md](../gem5/README.md).

## Q5 — Write-back vs write-through

How it was run: the `multiply` benchmark (300×300 matrix multiply) on a 4-way L3,
toggling only the write policy (`--wt` adds write-through; default is write-back).

Write-back:

- `sim_ticks`: `2391854220000`
- `system.cpu.numCycles`: `4783708440`
- `system.mem_ctrls.bytes_written::total`: `1087808`
- `system.mem_ctrls.num_writes::total`: `16997`
- `system.l3.writebacks::total`: `16997`
- NVMain energy: `4.3915e+07nJ`

Write-through:

- `sim_ticks`: `5171513377000`
- `system.cpu.numCycles`: `10343026754`
- `system.mem_ctrls.bytes_written::total`: `1378903808`
- `system.mem_ctrls.num_writes::total`: `21545372`
- `system.l3.WriteClean_accesses::total`: `21545372`
- NVMain energy: `8.38338e+08nJ`

The contrast is the whole point of the question. Under **write-back**, a store
only dirties the cached block; memory is written once, lazily, when the block is
evicted — hence `num_writes` ≈ `l3.writebacks` ≈ 17 k. Under **write-through**,
**every** store is forwarded down as a clean write immediately, so memory writes
explode to ~21.5 M and all of them show up as `l3.WriteClean_accesses` (with
`l3.writebacks` effectively 0 — there are no dirty evictions left). That ~1268×
increase in write traffic is why write-through is far slower and burns ~19× the
PCM energy here, which matters a lot for non-volatile memory whose writes are
expensive. The mechanism that produces those `WriteClean` packets is described
in [../gem5/README.md](../gem5/README.md).

---

## Bonus — Writeback-aware LLC policy (lower PCM energy)

Goal of the bonus: design a last-level-cache replacement policy that reduces
**PCM main-memory energy** versus LRU. PCM writes cost far more energy than
reads, so the win is in avoiding write-backs. The **WBA** (writeback-aware)
policy is a clean-preferring LRU: among the LRU eviction candidates it evicts
the least-recently-used **clean** block first — a clean block already has an
up-to-date copy in memory, so evicting it triggers **no** PCM write. Only when
every candidate is dirty does it fall back to plain LRU. Dirty lines therefore
stay cached longer, writes coalesce, and the number of PCM writes drops. The
implementation is described in [../gem5/README.md](../gem5/README.md).

How it was run: quicksort on the default 256kB / 16-way L3, switching only the
replacement policy (`--repl LRU` vs `--repl WBA`).

| metric | LRU baseline | WBA (modified) | change |
|---|---:|---:|---:|
| `system.l3.overall_miss_rate::total` | 0.691684 | 0.369269 | −46.6% |
| `system.l3.overall_hits::total` | 7341 | 15036 | +7695 |
| `system.mem_ctrls.num_writes::total` | 11947 | 2200 | **−81.6%** |
| `system.mem_ctrls.bytes_written::total` | 764608 | 140800 | −81.6% |
| `system.cpu.numCycles` | 184496846 | 178002756 | −3.5% |
| NVMain energy (rank0 `totalEnergy`) | 2.10685e6 nJ | 1.68465e6 nJ | **−20.0%** |

WBA cuts PCM write requests by ~82% and rank energy by ~20% on this quicksort
run. (The baseline matches the Q4 LRU numbers — it is the same quicksort+LRU
configuration, included here as the bonus's own reference point.)

Reproduce:

```bash
gem5sim quicksort --l3 --repl LRU --save Bonus/LRU_baseline
gem5sim quicksort --l3 --repl WBA --save Bonus/modified
```
