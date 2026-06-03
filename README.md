# Computer Organization Final — gem5 + NVMain Cache Experiments

Cache-system experiments built on the **gem5** CPU simulator with the **NVMain**
non-volatile-memory (PCM) model. Five questions:

| Q | Topic | What was done |
|---|---|---|
| **Q1** | Environment + hello world | Stand up gem5 + NVMain, run the built-in `hello`, capture baseline stats |
| **Q2** | Enable an L3 last-level cache | Add an `L3Cache` and wire L2 → L3 → memory bus (config-only, no rebuild) |
| **Q3** | L3 associativity (2-way vs full) | Run quicksort and compare set-associative vs fully-associative L3 |
| **Q4** | FBR replacement policy | Add a **frequency-based replacement with aging** policy and compare vs LRU |
| **Q5** | Write-back vs write-through | Add a write-through option and compare write traffic / PCM energy on a matrix multiply |
| **Bonus** | Writeback-aware LLC policy | Add a clean-preferring L3 replacement policy that cuts PCM write energy vs LRU |

> Per-question **code** details live in [gem5/README.md](gem5/README.md);
> per-question **results and reasoning** live in [data/README.md](data/README.md).
> Q1-Q5 **and** the Bonus are all included and reproducible.

---

## Repository layout

```
.
├── README.md          # this file — setup + command tutorial
├── Containerfile       # Podman/Docker image with the gem5 toolchain
├── .gitignore
├── scripts/
│   ├── setup.sh        # clone gem5@commit + NVMain, apply overlay, build gem5
│   ├── env.sh          # source this so gem5sim finds the built tree
│   ├── run_all.sh      # reproduce Q1-Q5 + Bonus into data/ in one shot
│   └── gem5sim         # the wrapper you actually run simulations with
├── gem5/               # SOURCE OVERLAY: only the gem5 files we changed,
│   ├── configs/...     #   kept at their original gem5-relative paths
│   └── src/...         #   (NOT a full gem5 tree — see gem5/README.md)
└── data/               # verified outputs: data/<Q>/{output.log,stats.txt}
    └── benchmarks/     # quicksort.c, multiply.c (auto-compiled on first run)
```

`gem5/` is an **overlay**: it contains only the ~13 files we modified/added,
stored at their real gem5 paths so they can be copied straight onto a full gem5
checkout. `scripts/setup.sh` does exactly that for you.

---

## Environment requirements

gem5 at the pinned commit (`525ce650…`, ~2019) has specific needs:

- **Python 2.7** — gem5's SCons build runs under, and embeds, Python 2.7.
  **Python 3 will not build it.** This is the single most common setup failure.
- **SCons**, **g++ / build-essential**, `m4`, `zlib`, `protobuf`, `libgoogle-perftools`.
- **Linux x86-64** (the binary is built for the X86 ISA).
- Benchmarks are compiled with `gcc -static -O0`:
  - `-static` — gem5's syscall-emulation (SE) mode has limited dynamic-linker support.
  - `-O0` — keeps the compiler from optimizing away the quicksort/multiply loops.

Because Python 2.7 is awkward on modern distros, the **recommended path is the
Podman container** below, which pins a known-good Ubuntu 18.04 toolchain.

---

## Quick start — Podman (recommended)

Works the same with Docker (swap `podman` → `docker`).

```bash
# 1. Build the toolchain image (fast — just installs compilers/SCons/Python 2.7)
podman build -t cofinal .

# 2. Start a long-running container with this repo mounted at /root/COFINAL
#    (run once; use `podman exec` afterwards to re-enter)
podman run -d --name cofinal \
  -v "$PWD":/root/COFINAL \
  -v /path/to/gem5:/root/gem5 \
  -v /path/to/NVmain:/root/NVmain \
  cofinal sleep infinity

# 3. Enter the container
#    Note: if your shell's cwd is outside the container mounts, cd ~ first:
cd ~ && podman exec -it -w /root/COFINAL cofinal bash

# --- inside the container ---------------------------------------------------
# 4. One-time: clone gem5@commit + NVMain, apply the overlay, build gem5.
#    The gem5 compile takes ~30-90 min the first time. Output goes to ./_work,
#    which is on the host mount, so it persists and is reused next time.
scripts/setup.sh

# 5. Point the wrapper at the freshly built tree
source scripts/env.sh

# 6. Reproduce everything, or run a single question
scripts/run_all.sh
gem5sim quicksort --l3 --assoc 2 --save Q3/2way
# ----------------------------------------------------------------------------
```

`-v "$PWD":/root/COFINAL` mounts the repo into the container, so `_work/` (the
built gem5) and any new `data/` results land back on your host automatically.

> **Re-entering the container** after it's already running:
> ```bash
> cd ~ && podman exec -it -w /root/COFINAL cofinal bash
> ```
> The `cd ~` avoids the `error getting current working directory` error that
> occurs when your shell's cwd is a path the container can't see.

> Fully-baked image (optional): if you'd rather have gem5 pre-compiled *inside*
> the image instead of via `setup.sh`, add `COPY . /work`, `RUN /work/scripts/setup.sh`
> to the `Containerfile`. The image becomes multi-GB and the build takes the full
> 30-90 min, but then no setup step is needed at run time. The default keeps the
> image small and does the heavy build once on the mounted volume.

---

## Alternative paths (no container)

**B. You already have the Python 2.7 / SCons / g++ toolchain locally:**

```bash
scripts/setup.sh        # clone + overlay + build into ./_work
source scripts/env.sh
scripts/run_all.sh
```

**C. Transfer a pre-built `_work/` from another machine** (skips the 30-90 min
build entirely — useful when moving to a laptop):

```bash
# On the machine that already ran scripts/setup.sh:
scp -r /path/to/repo/_work  user@laptop:/path/to/cloned/repo/

# On the laptop (after clone + scp):
source scripts/env.sh
scripts/run_all.sh        # works immediately, no build needed
```

The `_work/` directory is git-ignored and never committed; each machine either
runs `scripts/setup.sh` once or copies a pre-built tree.

---

**D. You already have your own full gem5 tree** and just want our changes —
apply the overlay onto it and rebuild:

```bash
cp -r gem5/configs gem5/src /path/to/your/gem5/
cd /path/to/your/gem5
scons EXTRAS=../NVmain build/X86/gem5.opt -j"$(nproc)"
```

Then run gem5sim against it by overriding the paths (see below).

---

## Running simulations — `scripts/gem5sim`

`gem5sim` wraps the long gem5 command line behind named benchmarks and friendly
flags. After `source scripts/env.sh`, just call `gem5sim …`.

```
gem5sim <benchmark> [options] [-- extra gem5 flags]

Benchmarks:   hello | quicksort | multiply
              (quicksort/multiply auto-compile from data/benchmarks/*.c on first use)

Options:
  --l3            enable the L3 cache
  --assoc N       L3 associativity (an integer, or "full" = 256kB/64B = 4096 ways)
  --repl POLICY   L3 replacement policy: LRU (default) or FBR
  --decay N       FBR decay-epoch length, in accesses (default 100000)
  --wt            use write-through (default is write-back)
  --save NAME     save output.log + stats.txt to data/NAME/  (omit = print only)
  -h, --help      full help
```

Per-question commands (this is exactly what `run_all.sh` runs):

```bash
gem5sim hello --save Q1                              # Q1 baseline
gem5sim hello --l3 --save Q2                         # Q2 L3 enabled
gem5sim quicksort --l3 --assoc 2    --save Q3/2way   # Q3 set-associative
gem5sim quicksort --l3 --assoc full --save Q3/fullway# Q3 fully-associative
gem5sim quicksort --l3 --repl LRU --save Q4/LRU      # Q4 baseline
gem5sim quicksort --l3 --repl FBR --save Q4/FBR      # Q4 frequency-based + aging
gem5sim multiply --l3 --assoc 4      --save Q5/writeback    # Q5 write-back
gem5sim multiply --l3 --assoc 4 --wt --save Q5/writethrough # Q5 write-through
gem5sim quicksort --l3 --repl LRU --save Bonus/quicksort/LRU_baseline   # Bonus baseline
gem5sim quicksort --l3 --repl WBA --save Bonus/quicksort/WBA_modified   # Bonus WBA
gem5sim multiply  --l3 --repl LRU --save Bonus/multiply/LRU_baseline    # Bonus baseline
gem5sim multiply  --l3 --repl WBA --save Bonus/multiply/WBA_modified    # Bonus WBA
```

### Path overrides (env vars)

`gem5sim` reads these; `env.sh` sets them for the `./_work` build. Override them
to point at a gem5 tree elsewhere (e.g. path **C** above):

| Variable | Meaning | Default after `source scripts/env.sh` |
|---|---|---|
| `GEM5_DIR` | gem5 source/build root | `./_work/gem5` |
| `NVMAIN_CONFIG` | PCM config file | `./_work/NVmain/Config/PCM_ISSCC_2012_4GB.config` |
| `BENCHMARK_DIR` | where benchmark `.c` / binaries live | `./data/benchmarks` |
| `ANSWER_DIR` | where `--save NAME` writes | `./data` |

```bash
# Example: run against a gem5 you built somewhere else
GEM5_DIR=/opt/gem5 NVMAIN_CONFIG=/opt/NVmain/Config/PCM_ISSCC_2012_4GB.config \
  gem5sim quicksort --l3 --assoc 2
```

---

## Cache hierarchy used by these runs

| Cache | Size | Associativity |
|---|---:|---:|
| L1-I | 32 kB | 2 |
| L1-D | 64 kB | 2 |
| L2 | 128 kB | 8 |
| L3 | 256 kB | 16 |

Sizes increase monotonically and every level is smaller than the working set
(quicksort ≈ 400 kB), so data spills level by level and the L3 sees **both hits
and evictions** — that is what makes the Q3/Q4 experiments meaningful.

---

## Troubleshooting

- **`L3 hits = 0` / 2-way == full-way:** the working set fits inside L2, so L3 is
  never exercised. Check that **L2 < L3 < working set**. (Early on, L2 and L3 were
  both 2 MB and the 100 k quicksort fit in L2 → L3 was dead.)
- **`SyntaxError: Non-ASCII character` while building:** a gem5 `.py` file has a
  non-ASCII (e.g. Chinese) comment. Python 2 forbids that unless the file starts
  with `# -*- coding: utf-8 -*-`. Keep overlay comments ASCII.
- **SCons / build errors about print statements or `python3`:** you're building
  under Python 3. Use Python 2.7 (the container guarantees this).
- **`--save` seems ignored:** it must come **before** any `--`. Everything after
  `--` is passed straight to gem5, so `--save` placed there is treated as a gem5
  flag and never triggers saving.
- **NVMain energy numbers missing from `stats.txt`:** they only appear in the
  terminal log. That's why every run keeps both `output.log` and `stats.txt`.
- **`podman exec` → `error getting current working directory`:** happens when
  your shell's cwd is outside the container's mount points (e.g. you're sitting
  in a directory the container can't see). Fix: `cd` to any valid path first:
  ```bash
  cd ~ && podman exec -it -w /root/work/answer cofinal bash
  ```
- **`source scripts/env.sh` → `No such file or directory`:** same cause — you're
  on the host in a directory outside the container mount. Either enter the
  container first (see above), or `cd` to the repo root on a mounted path before
  sourcing.

---

## Notes

- `data/<Q>/` keeps only `output.log` + `stats.txt`; `gem5sim` deletes gem5's
  `config.ini` / `config.json` from saved runs to keep them clean.
- `_work/` (the cloned + built gem5/NVMain) is git-ignored — it's regenerated by
  `scripts/setup.sh`, not committed.
- Overlay files under `gem5/` keep their original gem5 BSD copyright headers.
- The **Bonus** (a PCM-energy-reducing, writeback-aware LLC policy) is included
  and reproducible — its code is in the overlay and its results in `data/Bonus/`.
