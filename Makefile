# Plain make, no cmake needed. `make cmake-build` uses CMake if you have it.
CXX      ?= clang++
CXXFLAGS := -std=c++17 -Iinclude -Wall -Wextra -Wpedantic
OPT      := -O2
BUILD    := build

# Modules implemented so far. Stubs are excluded until they have a body --
# see docs/ROADMAP.md for which phase each belongs to.
CORE := src/geo/point.cpp src/geo/bbox.cpp src/geo/polygon.cpp src/geo/containment.cpp \
        src/ds/dynamic_connectivity.cpp src/index/brute_force.cpp src/index/quadtree.cpp src/index/rtree.cpp \
        src/index/versioned_index.cpp \
        src/util/json.cpp src/fence/zone.cpp src/fence/hysteresis.cpp src/fence/evaluator.cpp \
        src/track/tourist.cpp src/power/adaptive_sampler.cpp src/alert/alert.cpp \
        src/alert/correlator.cpp src/group/cohesion.cpp src/sim/mobility.cpp src/sim/simulator.cpp \
        src/viz/html_export.cpp \
        src/evidence/sha256.cpp src/evidence/merkle_log.cpp \
        src/graph/road_graph.cpp src/graph/dijkstra.cpp src/graph/astar.cpp \
        src/graph/bipartite_match.cpp src/dispatch/assigner.cpp \
        src/index/geohash.cpp src/geo/sweep_line.cpp \
        src/sync/lamport.cpp src/jurisdiction/hierarchy.cpp \
        src/track/trajectory.cpp src/track/anomaly.cpp

TESTS := tests/geo/ray_casting_test.cpp tests/geo/containment_uncertainty_test.cpp \
         tests/index/equivalence_test.cpp tests/index/versioned_index_test.cpp \
         tests/ds/dynamic_connectivity_test.cpp tests/ds/interval_tree_test.cpp \
         tests/ds/circular_buffer_test.cpp tests/ds/priority_queue_test.cpp \
         tests/ds/hash_table_test.cpp tests/ds/timer_wheel_test.cpp \
         tests/alert/correlator_test.cpp \
         tests/group/cohesion_test.cpp tests/golden/scenario_replay_test.cpp \
         tests/golden/hysteresis_ab_test.cpp \
         tests/evidence/merkle_log_test.cpp \
         tests/graph/shortest_path_test.cpp tests/graph/matching_test.cpp \
         tests/dispatch/assigner_test.cpp \
         tests/index/kd_tree_test.cpp tests/index/geohash_test.cpp \
         tests/geo/sweep_line_test.cpp tests/sync/lamport_test.cpp \
         tests/jurisdiction/hierarchy_test.cpp \
         tests/alert/triage_escalation_test.cpp tests/track/anomaly_test.cpp

.PHONY: all demo bench test check clean asan cmake-build help

all: $(BUILD)/safetrail_headless $(BUILD)/safetrail_bench

$(BUILD)/safetrail_headless: $(CORE) apps/safetrail_headless.cpp
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) $(OPT) $^ -o $@

$(BUILD)/safetrail_bench: $(CORE) apps/safetrail_bench.cpp
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) $(OPT) $^ -o $@

demo: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 40 --hours 1 --synthetic 5000 --show 12

# Writes a single self-contained HTML file -- no server, no network, no Leaflet.
dashboard: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 60 --hours 2 --synthetic 400 --show 6 --export-html dashboard.html

# Render the slide deck to a PDF (one landscape page per slide) via headless Chrome.
CHROME ?= /Applications/Google Chrome.app/Contents/MacOS/Google Chrome
pdf:
	@"$(CHROME)" --headless --disable-gpu --no-pdf-header-footer \
	  --print-to-pdf="$(PWD)/safetrail-slides.pdf" --print-to-pdf-no-header \
	  --virtual-time-budget=4000 "file://$(PWD)/slides.html" 2>/dev/null
	@echo "wrote safetrail-slides.pdf"

bench: $(BUILD)/safetrail_bench
	@mkdir -p bench/results
	@./$(BUILD)/safetrail_bench --out bench/results

test:
	@mkdir -p $(BUILD)/test; fail=0; \
	for t in $(TESTS); do \
	  n=$$(basename $$t .cpp); \
	  $(CXX) $(CXXFLAGS) -O1 $(CORE) $$t -o $(BUILD)/test/$$n 2>/dev/null || { echo "  build FAIL $$n"; fail=1; continue; }; \
	  ./$(BUILD)/test/$$n || fail=1; \
	done; echo; [ $$fail -eq 0 ] && echo "ALL TESTS PASS" || { echo "TESTS FAILED"; exit 1; }

# Sanitizers are opt-in: they make link times painful and timing numbers useless.
# Sanitizer run: compile the core ONCE into an instrumented archive, then link
# each sensitive test against it -- avoids recompiling ~20 files per test.
ASAN_FLAGS := -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_TESTS := tests/index/versioned_index_test.cpp tests/evidence/merkle_log_test.cpp
asan:
	@mkdir -p $(BUILD)/asan
	@echo "building instrumented core (once)..."
	@$(CXX) $(CXXFLAGS) $(ASAN_FLAGS) -c $(CORE) && mv *.o $(BUILD)/asan/ 2>/dev/null || true
	@ar rcs $(BUILD)/asan/libcore.a $(BUILD)/asan/*.o
	@fail=0; for t in $(ASAN_TESTS); do \
	  n=$$(basename $$t .cpp); \
	  $(CXX) $(CXXFLAGS) $(ASAN_FLAGS) $$t $(BUILD)/asan/libcore.a -o $(BUILD)/asan/$$n \
	    2>/dev/null || { echo "  build FAIL $$n"; fail=1; continue; }; \
	  ./$(BUILD)/asan/$$n || fail=1; \
	done; echo; [ $$fail -eq 0 ] && echo "ASAN CLEAN" || { echo "ASAN FAILURES"; exit 1; }

check:
	@fail=0; for h in $$(find include -name '*.hpp'); do \
	  rel=$${h#include/}; \
	  out=$$(printf '#include "%s"\nint main(){}\n' "$$rel" | $(CXX) $(CXXFLAGS) -fsyntax-only -x c++ - 2>&1); \
	  if [ -n "$$out" ]; then echo "  FAIL $$rel"; fail=1; fi; done; \
	[ $$fail -eq 0 ] && echo "all headers compile standalone" || exit 1

cmake-build:
	@cmake -B $(BUILD)-cmake -DCMAKE_BUILD_TYPE=Release -S . && cmake --build $(BUILD)-cmake -j

clean:
	@rm -rf $(BUILD) $(BUILD)-cmake build-rel

help:
	@echo "make          build demo + benchmark"
	@echo "make demo     run the simulation, print the event stream and counters"
	@echo "make dashboard  build dashboard.html and open it in a browser"
	@echo "make bench    index scaling, equivalence, hysteresis A/B  -> bench/results/"
	@echo "make test     unit tests"
	@echo "make asan     unit tests under ASan/UBSan"
	@echo "make check    syntax-check every header standalone"
