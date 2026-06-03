#!/usr/bin/env bash
# run_all.sh — reproduce every verified result (Q1-Q5 + Bonus) into data/.
#
# Prereq: run ./setup.sh once, then `source env.sh` (this script also sources
# env.sh for you if you forgot). Each run overwrites data/<NAME>/ with a fresh
# output.log + stats.txt, matching the layout already committed in data/.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Make sure the wrapper can find the built gem5 tree.
[[ -n "${GEM5_DIR:-}" ]] || source "$HERE/env.sh"

GS="$HERE/scripts/gem5sim"
[[ -x "$GS" ]] || chmod +x "$GS" 2>/dev/null || true

run() { echo; echo "### $*"; "$GS" "$@"; }

# Q1 — build-up + hello world (no L3)
run hello --save Q1
# Q2 — enable L3 (hello world with L3)
run hello --l3 --save Q2
# Q3 — quicksort, L3 2-way vs full-way
run quicksort --l3 --assoc 2    --save Q3/2way
run quicksort --l3 --assoc full --save Q3/fullway
# Q4 — quicksort, LRU vs FBR (frequency-based replacement with aging)
run quicksort --l3 --repl LRU --save Q4/LRU
run quicksort --l3 --repl FBR --save Q4/FBR
# Q5 — multiply, write-back vs write-through (4-way L3 on PCM)
run multiply --l3 --assoc 4      --save Q5/writeback
run multiply --l3 --assoc 4 --wt --save Q5/writethrough
# Bonus — writeback-aware (clean-preferring) LLC policy vs LRU, on both benchmarks
run quicksort --l3 --repl LRU --save Bonus/quicksort/LRU_baseline
run quicksort --l3 --repl WBA --save Bonus/quicksort/WBA_modified
run multiply  --l3 --repl LRU --save Bonus/multiply/LRU_baseline
run multiply  --l3 --repl WBA --save Bonus/multiply/WBA_modified

echo
echo "All done. Results are under $HERE/data/"
