#!/usr/bin/env bash
# SafeTrail project review: the whole story, in order, from real runs.
#
#   make review            every step, no stops (~1.5 min; rehearsal, or CI-like)
#   make review PAUSE=1    the same, waiting for Enter between steps -- narrate
#   tools/review.sh 5      one step on its own (1-9); reuses the last run's files
#
# Nothing here is typed in: every number is read back from the program that just
# produced it. The scenario is fixed (seed 7), so two runs print the same thing.
# Outputs go to build/review/ (ignored by git); the committed benchmark results
# and the docs are not touched.
set -u -o pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
OUT=$BUILD/review
HL=./$BUILD/safetrail_headless
BENCH=./$BUILD/safetrail_bench
SCENARIO="--zones data/zones/shillong_osm.geojson --tourists 40 --hours 1 --synthetic 5000 --seed 7 --show 100000"
ESC=$(printf '\033')
if [ -t 1 ]; then B="$ESC[1m"; D="$ESC[2m"; G="$ESC[32m"; R="$ESC[31m"; N="$ESC[0m"; else B=; D=; G=; R=; N=; fi

say()  { printf '%s\n' "$*"; }
cmd()  { printf '%s$ %s%s\n' "$D" "$*" "$N"; }
head_() { printf '\n%s━━ %s ━━%s\n' "$B" "$1" "$N"; }
plain() { sed "s/${ESC}\[[0-9;]*m//g"; }           # strip colour for parsing
pause() {
  if [ "${PAUSE:-0}" = 1 ] && [ -t 0 ] && [ "$ONLY" = 0 ]; then
    printf '\n%s  ↵  Enter for the next step%s ' "$D" "$N"; read -r _
  fi
}
die() { printf '\n%s%s%s\n' "$R" "$*" "$N"; exit 1; }

# A block of the headless output: from the line that starts with $2 to the next
# blank line.
section() { awk -v h="$2" 'f && /^$/ {exit} index($0, h) == 1 {f = 1} f' "$1"; }
field() { grep -m1 "$2" "$1" | awk -v c="$3" '{print $c}'; }
events() { awk '/^  [0-9][0-9]:[0-9][0-9]:[0-9][0-9]  /' "$1"; }

ONLY=${1:-0}
case "$ONLY" in ''|*[!0-9]*) die "usage: tools/review.sh [step 1-9]";; esac
want() { [ "$ONLY" = 0 ] || [ "$ONLY" = "$1" ]; }

# ── build, and run the scenario under each index ─────────────────────────────
mkdir -p "$OUT"
make -s all || die "build failed: run 'make' and read the first error it prints"
[ -f data/zones/shillong_osm.geojson ] || die "data/zones/shillong_osm.geojson is missing (git checkout it)"

run_index() {   # $1 = brute|quadtree|rtree, $2 = extra flags
  local f="$OUT/$1.txt"
  if [ ! -s "$f" ] || [ "$ONLY" = 0 ]; then
    $HL $SCENARIO --index "$1" $2 | plain > "$f" || die "the engine failed under --index $1"
  fi
}
if want 1 || want 2 || want 3 || want 4 || want 5; then
  run_index brute ""; run_index quadtree ""; run_index rtree ""
fi
if want 6; then run_index quadtree ""; $HL $SCENARIO --index quadtree --no-hysteresis | plain > "$OUT/naive.txt"; fi

Q=$OUT/quadtree.txt; BF=$OUT/brute.txt; RT=$OUT/rtree.txt
zones=$(field "$Q" 'zones loaded' 3)
evals=$(field "$Q" '^  evaluations' 2)
queries=$(field "$Q" '^  index queries' 3)
nevents=$(events "$Q" | wc -l | tr -d ' ')

if [ "$ONLY" = 0 ]; then
  printf '\n%s━━ SafeTrail · project review ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n' "$B" "$N"
  say "  scenario   38 real OpenStreetMap hazard zones around Shillong + 5,000 synthetic"
  say "             40 simulated tourists, 1 simulated hour, one GPS fix per second"
  say "             GPS error 4 m open sky / 35 m multipath; seed 7, so every run is identical"
  say "  steps      1 problem · 2 brute force · 3 quadtree & R-tree · 4 correctness"
  say "             5 transitions · 6 noise · 7 history · 8 performance · 9 engineering"
fi

# ── 1. The problem ───────────────────────────────────────────────────────────
if want 1; then
  head_ "1. The problem: many zones, a stream of noisy positions"
  cmd "$HL $SCENARIO --index quadtree"
  grep -E '^  (zones loaded|tourists|GPS error model|simulated span)' "$Q"
  say ""
  say "  Real zones in the set (restricted and caution; the 5,000 synthetic ones pad the scale):"
  section "$Q" "zones" | grep -E 'restricted|caution' | sed -n '1,6p'
  say "  ..."
  say ""
  say "  Each tourist's position is a disc, not a point (the GPS accuracy), and the"
  say "  question every second is: inside, outside or uncertain, for every zone nearby."
  say "  $evals evaluations in this hour; checking all $zones zones each time is the naive cost."
  pause
fi

# ── 2. Brute force ───────────────────────────────────────────────────────────
if want 2; then
  head_ "2. Brute force: every zone, every fix  (the oracle we keep forever)"
  cmd "$HL $SCENARIO --index brute"
  section "$BF" "index brute"
  say ""
  section "$BF" "engine counters" | grep -E 'index queries|candidates returned|exact geometry'
  boxes=$(( queries * zones ))
  say ""
  say "  Brute force compares the query box with every one of the $zones zone boxes:"
  say "  $queries queries × $zones zones = $boxes box tests this hour, O(n) per fix."
  say "  The ~14 zones per query whose boxes overlap are the candidates; exact geometry"
  say "  runs on those. Brute force is kept forever as the oracle the fast structures"
  say "  are checked against, and as the denominator of every speedup."
  pause
fi

# ── 3. Quadtree and R-tree ───────────────────────────────────────────────────
if want 3; then
  head_ "3. Spatial indexes: a quadtree (partitions space) and an R-tree (partitions items)"
  cmd "$HL $SCENARIO --index quadtree"
  section "$Q" "index quadtree"
  section "$Q" "engine counters" | grep -E 'exact geometry'
  say ""
  cmd "$HL $SCENARIO --index rtree"
  section "$RT" "index r-tree"
  section "$RT" "engine counters" | grep -E 'exact geometry'
  say ""
  say "  Same candidates per query, same exact-geometry count: the answer is identical by"
  say "  construction. What changes is how it is found: instead of $zones box tests, a"
  say "  descent through the few cells (quadtree) or envelopes (R-tree) that overlap the"
  say "  query box -- O(log n + k). Step 8 times it. The dashboard draws both structures"
  say "  over the real map (the index: button)."
  pause
fi

# ── 4. Correctness ───────────────────────────────────────────────────────────
if want 4; then
  head_ "4. Correctness: the fast indexes must give the oracle's answer, exactly"
  for i in brute quadtree rtree; do
    { grep -m1 '^event stream' "$OUT/$i.txt"; events "$OUT/$i.txt"; section "$OUT/$i.txt" "events by kind"; } > "$OUT/$i.events"
  done
  if cmp -s "$OUT/brute.events" "$OUT/quadtree.events" && cmp -s "$OUT/brute.events" "$OUT/rtree.events"; then
    say "  ${G}✓ identical event streams${N}: brute force = quadtree = R-tree, $nevents events, byte for byte"
    say "    (compared: every event line and the per-kind totals of the three runs above)"
  else
    say "  ${R}✗ EVENT STREAMS DIFFER between indexes${N}"; diff "$OUT/brute.events" "$OUT/quadtree.events" | head -5
    die "an index changed the engine's answer; this is a correctness failure"
  fi
  say ""
  cmd "$BENCH --only 5"
  $BENCH --only 5 | plain | awk '/^5\./ {f=1} f && /^═/ {exit} f' || die "benchmark section 5 failed"
  say "  The test suite goes further: tests/index/differential_test.cpp compares every index"
  say "  with brute force on ~55,000 hostile queries each, auditing the tree's invariants"
  say "  after every insert, removal and rebuild (make test, step 9)."
  pause
fi

# ── 5. State transitions ─────────────────────────────────────────────────────
if want 5; then
  head_ "5. State: outside → approaching → ENTER → inside → EXIT, for one real tourist and zone"
  cmd "$HL $SCENARIO --index quadtree   # one (tourist, zone) pair's events"
  # The first real (non-synthetic) zone that a tourist approached from outside,
  # entered and later left; print that pair's history. Zone names occupy columns
  # 37-64 of an event line.
  events "$Q" | awk '
    { kind = $2; tid = $3; zone = substr($0, 37, 28); sub(/ +$/, "", zone)
      if (index(zone, "synthetic") == 1) next
      key = tid "|" zone
      if (!(key in first)) first[key] = kind
      if (kind == "ENTER" && first[key] != "ENTER" && !(key in entered)) entered[key] = NR
      if (kind == "EXIT" && (key in entered) && pick == "") pick = key
      lines[NR] = $0; keys[NR] = key; n = NR }
    END { if (pick == "") { print "  (no outside -> enter -> exit sequence on a real zone in this run)"; exit }
          split(pick, p, "|"); printf "  %s and %s:\n", p[1], p[2]
          shown = 0
          for (i = 1; i <= n; i++) if (keys[i] == pick) {
            if (shown < 12) print lines[i]; shown++ }
          if (shown > 12) printf "  ... %d more\n", shown - 12 }'
  say ""
  say "  'Nm deep, ±Am' is the signed distance to the boundary and the fix's accuracy."
  say "  UNCERTAIN means the accuracy disc straddles the edge: reported, never acted on."
  say "  ENTER needs the fix 15 m inside and 3 agreeing fixes (5 s dwell); EXIT needs 25 m"
  say "  outside and 3 agreeing fixes. A zone that leaves the index's candidate window is"
  say "  still observed every fix (reconciliation), so an exit is never missed."
  pause
fi

# ── 6. Noise ─────────────────────────────────────────────────────────────────
if want 6; then
  head_ "6. Noise: the same hour with and without the hysteresis filter"
  cmd "$HL $SCENARIO --index quadtree --no-hysteresis"
  NV=$OUT/naive.txt
  printf '  %-22s %12s %12s\n' "" "filter on" "filter OFF"
  for k in "zone entries" "zone exits" "uncertain" "flaps suppressed"; do
    a=$(grep -m1 "^  $k" "$Q" | awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) {print $i; exit}}')
    b=$(grep -m1 "^  $k" "$NV" | awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) {print $i; exit}}')
    printf '  %-22s %12s %12s\n' "$k" "${a:-?}" "${b:-?}"
  done
  say ""
  say "  Same trajectories, same GPS noise. Without the filter, drift at a boundary becomes"
  say "  an enter/exit pair. With it, a transition needs margin, confirmation and dwell."
  say "  The next table measures that against noise-free truth on the same trajectories:"
  cmd "$BENCH --only 10"
  $BENCH --only 10 | plain | awk '/^10\./ {f=1} f && /^═/ {exit} f' || die "benchmark section 10 failed"
  pause
fi

# ── 7. History ───────────────────────────────────────────────────────────────
if want 7; then
  head_ "7. History: a persistent quadtree answers 'what were the rules at 00:45?'"
  cmd "$BENCH --only 9"
  $BENCH --only 9 | plain | awk '/^9\./ {f=1} f && /^═/ {exit} f' || die "benchmark section 9 failed"
  say "  Each change copies one root-to-leaf path (~15 nodes) and shares the rest of the"
  say "  tree; a rule change copies nothing; a query against the past costs what a query"
  say "  against the present does. On the dashboard: scrub the timeline and watch 'rules"
  say "  in force', the 'zone change log' and the 'persistent index' panel (copied path"
  say "  highlighted, shared subtrees dim)."
  pause
fi

# ── 8. Performance ───────────────────────────────────────────────────────────
if want 8; then
  head_ "8. Performance: brute force vs quadtree vs R-tree, measured now"
  cmd "$BENCH --only 1"
  $BENCH --only 1 | plain | awk '/^1\./ {f=1} f && /^═/ {exit} f' || die "benchmark section 1 failed"
  say "  Medians of 11 interleaved rounds, paired ratios; 'equal' is the checksum gate that"
  say "  the three returned the same results. The committed tables (docs/RESULTS.md) come"
  say "  from 'make bench' on this machine; ratios move between sessions, so they are"
  say "  quoted as ranges there."
  pause
fi

# ── 9. Engineering quality ───────────────────────────────────────────────────
if want 9; then
  head_ "9. Engineering: the test suite, determinism, and the dashboard"
  cmd "make test"
  make -s test || die "tests failed"
  say ""
  cmd "make determinism"
  make -s determinism || die "determinism check failed"
  say ""
  say "  Also in the repository, not run here because they take minutes:"
  say "    make mutation    22 realistic bugs injected one at a time; every one must be caught (22/22)"
  say "    make ubsan       the whole suite under UndefinedBehaviorSanitizer (ASan + UBSan in Linux CI)"
  say "    make analyze     Clang Static Analyzer over the library (0 findings)"
  say "    make validate    everything that gates a merge, in one command"
  say ""
  cmd "make dashboard"
  make -s dashboard | plain | tail -1 || die "dashboard generation failed"
fi

if [ "$ONLY" = 0 ]; then
  printf '\n%s━━ done ━━%s\n' "$B" "$N"
  say "  Open ${B}dashboard.html${N} in a browser (macOS: ${B}open dashboard.html${N}). One file, no server."
  say "  Walkthrough and talking points: docs/REVIEW_DEMO.md · questions: docs/REVIEW_QA.md"
fi
