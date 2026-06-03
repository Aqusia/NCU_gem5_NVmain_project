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
├── README.md           # this file — setup + command tutorial
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

## Complete build and run guide

### Step 1 — Clone the repo

```bash
git clone https://github.com/Aqusia/NCU_gem5_NVmain_project.git
cd NCU_gem5_NVmain_project
```

### Step 2 — Build the container image

```bash
podman build -t cofinal .
```

This installs the Ubuntu 18.04 toolchain (Python 2.7, SCons, g++, etc.).
Takes ~1–2 minutes. Only needs to be done once.

> **Note:** If `apt-get update` fails with 404 errors for `old-releases.ubuntu.com`,
> the Ubuntu 18.04 mirror may be temporarily unavailable. Retry after a few minutes,
> or check that your network can reach `old-releases.ubuntu.com`. If the image was
> already built on this machine (check with `podman images`), skip this step entirely.

### Step 3 — Start the container

```bash
# Linux / macOS
podman run -d --name cofinal \
  -v "$PWD":/root/COFINAL:z \
  cofinal sleep infinity

# Windows (PowerShell) — use full path and root connection
podman --connection podman-machine-default-root run -d --name cofinal \
  -v "D:/path/to/NCU_gem5_NVmain_project:/root/COFINAL:z" \
  localhost/cofinal sleep infinity
```

`-v …:/root/COFINAL:z` mounts the repo into the container so `_work/`
(the built gem5) and `data/` results land back on your host automatically.
The `:z` label is required on SELinux systems (Fedora, RHEL, etc.).

### Step 4 — Enter the container

> **Important:** Always `cd ~` before `podman exec` to avoid the
> `error getting current working directory` error (happens when your shell's
> cwd is a path the container can't see).

```bash
# Linux / macOS
cd ~ && podman exec -it -w /root/COFINAL cofinal bash

# Windows (PowerShell)
podman --connection podman-machine-default-root exec -it -w /root/COFINAL cofinal bash
```

### Step 5 — Fix PATH (one-time, inside container)

```bash
echo 'export PATH=/root/COFINAL/scripts:$PATH' >> ~/.bashrc
source ~/.bashrc
```

This makes `gem5sim` resolve to the repo's `scripts/gem5sim` and saves all
results to `data/` inside the repo.

### Step 6 — Build gem5 + NVMain (one-time, ~30–90 min)

```bash
scripts/setup.sh
```

This will:
1. Clone gem5 at pinned commit `525ce650` → `_work/gem5/`
2. Clone NVMain → `_work/NVmain/`
3. Apply the overlay from `gem5/` onto the full gem5 tree
4. Build `_work/gem5/build/X86/gem5.opt` with SCons
5. Write `scripts/env.sh` with the correct paths

### Step 7 — Run simulations

```bash
# Set environment (or skip if PATH was fixed in step 5)
source scripts/env.sh

# Run a single question
gem5sim hello --save Q1

# Or reproduce everything at once
scripts/run_all.sh
```

---

## Re-entering the container later

The container keeps running in the background. To come back:

```bash
# Linux / macOS
cd ~ && podman exec -it -w /root/COFINAL cofinal bash

# Windows (PowerShell)
podman --connection podman-machine-default-root exec -it -w /root/COFINAL cofinal bash
```

PATH is already set from `~/.bashrc` — `gem5sim` works immediately.

---

## Running simulations — `scripts/gem5sim`

`gem5sim` wraps the long gem5 command line behind named benchmarks and friendly
flags. After step 5, just call `gem5sim …` from anywhere inside the container.

```
gem5sim <benchmark> [options] [-- extra gem5 flags]

Benchmarks:   hello | quicksort | multiply
              (quicksort/multiply auto-compile from data/benchmarks/*.c on first use)

Options:
  --l3            enable the L3 cache
  --assoc N       L3 associativity (an integer, or "full" = 256kB/64B = 4096 ways)
  --repl POLICY   L3 replacement policy: LRU (default), FBR, or WBA
  --decay N       FBR decay-epoch length, in accesses (default 100000)
  --wt            use write-through (default is write-back)
  --save NAME     save output.log + stats.txt to data/NAME/  (omit = print only)
  -h, --help      full help
```

Per-question commands (exactly what `scripts/run_all.sh` runs):

```bash
gem5sim hello --save Q1                                        # Q1 baseline
gem5sim hello --l3 --save Q2                                   # Q2 L3 enabled
gem5sim quicksort --l3 --assoc 2    --save Q3/2way             # Q3 set-associative
gem5sim quicksort --l3 --assoc full --save Q3/fullway          # Q3 fully-associative
gem5sim quicksort --l3 --repl LRU   --save Q4/LRU              # Q4 baseline
gem5sim quicksort --l3 --repl FBR   --save Q4/FBR              # Q4 FBR w/ aging
gem5sim multiply  --l3 --assoc 4         --save Q5/writeback   # Q5 write-back
gem5sim multiply  --l3 --assoc 4 --wt    --save Q5/writethrough# Q5 write-through
gem5sim quicksort --l3 --repl LRU --save Bonus/quicksort/LRU_baseline
gem5sim quicksort --l3 --repl WBA --save Bonus/quicksort/WBA_modified
gem5sim multiply  --l3 --repl LRU --save Bonus/multiply/LRU_baseline
gem5sim multiply  --l3 --repl WBA --save Bonus/multiply/WBA_modified
```

---

## Alternative: transfer pre-built `_work/` (skip the build)

If you already have a machine that ran `scripts/setup.sh`, copy `_work/` to
skip the 30–90 min build on a new machine:

```bash
# On the source machine:
scp -r /path/to/repo/_work  user@newmachine:/path/to/cloned/repo/

# On the new machine (after clone + scp):
# Start container (step 3), enter (step 4), fix PATH (step 5), then:
gem5sim hello --save Q1   # works immediately, no build needed
```

---

## Path overrides (env vars)

`gem5sim` reads these; `scripts/env.sh` sets them for the `_work/` build.

| Variable | Meaning | Default after `source scripts/env.sh` |
|---|---|---|
| `GEM5_DIR` | gem5 source/build root | `./_work/gem5` |
| `NVMAIN_CONFIG` | PCM config file | `./_work/NVmain/Config/PCM_ISSCC_2012_4GB.config` |
| `BENCHMARK_DIR` | where benchmark `.c` / binaries live | `./data/benchmarks` |
| `ANSWER_DIR` | where `--save NAME` writes | `./data` |

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

- **Scripts fail with `$'\r': command not found` (Windows clone):** the repo
  was cloned on Windows, which adds CRLF line endings that Linux rejects.
  Fix inside the container before running anything:
  ```bash
  sed -i 's/\r//' scripts/setup.sh scripts/run_all.sh scripts/gem5sim scripts/env.sh
  ```

- **`podman exec` → `error getting current working directory`:** your shell's
  cwd is outside the container mounts. Fix:
  ```bash
  cd ~ && podman exec -it -w /root/COFINAL cofinal bash
  ```

- **`gem5sim` saves to wrong directory:** PATH is pointing at old scripts.
  Run step 5 (fix PATH) again:
  ```bash
  echo 'export PATH=/root/COFINAL/scripts:$PATH' >> ~/.bashrc && source ~/.bashrc
  which gem5sim   # should show /root/COFINAL/scripts/gem5sim
  ```

- **`L3 hits = 0` / 2-way == full-way:** the working set fits inside L2, so L3
  is never exercised. Check that **L2 < L3 < working set**.

- **`SyntaxError: Non-ASCII character` while building:** a gem5 `.py` file has
  a non-ASCII comment. Python 2 forbids that unless the file starts with
  `# -*- coding: utf-8 -*-`. Keep overlay comments ASCII.

- **SCons / build errors about `python3`:** you're building under Python 3.
  Use the container (Python 2.7 guaranteed).

- **`--save` seems ignored:** it must come **before** `--`. Everything after
  `--` is passed straight to gem5.

- **NVMain energy numbers missing from `stats.txt`:** they only appear in the
  terminal log. That's why every run keeps both `output.log` and `stats.txt`.

---

## Notes

- `data/<Q>/` keeps only `output.log` + `stats.txt`; `gem5sim` deletes gem5's
  `config.ini` / `config.json` from saved runs to keep them clean.
- `_work/` (the cloned + built gem5/NVMain) is git-ignored — regenerated by
  `scripts/setup.sh`, not committed.
- Overlay files under `gem5/` keep their original gem5 BSD copyright headers.
- The **Bonus** (writeback-aware LLC policy) is included and reproducible —
  code in the overlay, results in `data/Bonus/`.
