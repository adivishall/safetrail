# SafeTrail build. Plain make, no dependencies beyond a C++17 compiler.
#
#   make test        unit, property, differential and golden tests
#   make bench       every benchmark -> bench/results/*.csv (+ environment.txt),
#                    then regenerates every results table in the docs
#   make demo        run the engine on real OSM zones, print the event stream
#   make dashboard   write dashboard.html (self-contained; open it in a browser)
#   make review      the project review: brute force -> quadtree/R-tree -> same
#                    answer -> transitions -> noise -> history -> benchmark ->
#                    tests -> dashboard, from real runs (PAUSE=1 to step through)
#
#   make validate    everything that gates a merge, in one command (see below)
#   make stress      the randomised tests at 10x their default size
#   make sanitize    whole suite under ASan+UBSan (Linux) or UBSan (macOS)
#   make mutation    inject known bugs; every one must be caught by a test
#   make analyze     Clang Static Analyzer over the library (clang only)
#   make rebuild     clean, then build and test from scratch
#
# Source set: this Makefile and CMakeLists.txt both glob src/**.cpp and
# tests/**_test.cpp, and both read the warning flags from tools/build/*.flags,
# so neither the file list nor the flags can drift between them. CI checks the
# file lists match (`make manifest`) and builds with both.

CXX      ?= c++
BUILD    ?= build
OPT      ?= -O2

# ── Flags ────────────────────────────────────────────────────────────────────
#
# -ffp-contract=off is load-bearing, not a style choice. By default clang and gcc
# may CONTRACT `a*b + c` into a fused multiply-add, which keeps an extra rounding
# step's worth of precision -- and whether they do depends on the compiler and
# the optimisation level. The engine is a long chain of floating-point
# arithmetic feeding threshold comparisons, so one last-bit difference flips an
# inside/outside test and every later event with it. Measured when the flag was
# introduced: macOS clang -O0, macOS clang -O2 and Linux g++ -O2 produced three
# different event streams from one seed; with contraction off, all three agree.
# That is what makes "same seed -> byte-identical output" a property of the
# program rather than of one build of it. Cost: a few FMAs not emitted,
# unmeasurable next to index traversal and branchy geometry.
#
# Warnings: the shared list in tools/build/warnings.flags, plus the conversion
# family on clang. -Wconversion/-Wsign-conversion are clang-only here because
# gcc's versions also diagnose the integer promotions in compound assignments
# on narrow types, which clang does not, and the tree has only been audited
# clean against clang's. WERROR=1 (what CI sets) turns every warning into an
# error.
IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -qi clang && echo 1)
# Fail loudly rather than build silently with no warnings at all.
$(if $(wildcard tools/build/warnings.flags),,$(error tools/build/warnings.flags is missing))
WARN     := $(shell cat tools/build/warnings.flags) \
            $(if $(IS_CLANG),$(shell cat tools/build/warnings-clang.flags))
CXXFLAGS := -std=c++17 -Iinclude $(WARN) -ffp-contract=off $(if $(WERROR),-Werror)

# `sort` for determinism: glob order is filesystem-dependent, and a reproducible
# link order is one less variable when chasing a nondeterministic result.
SRC      := $(sort $(shell find src -name '*.cpp'))
OBJ      := $(patsubst src/%.cpp,$(BUILD)/obj/%.o,$(SRC))
LIB      := $(BUILD)/libsafetrail.a

TEST_SRC := $(sort $(shell find tests -name '*_test.cpp'))
TEST_BIN := $(patsubst tests/%.cpp,$(BUILD)/test/%,$(TEST_SRC))

APPS     := $(BUILD)/safetrail_headless $(BUILD)/safetrail_bench

.PHONY: all demo dashboard review bench bench-variation test stress check validate sanitize asan ubsan \
        mutation analyze cmake-build rebuild clean help manifest determinism

all: $(APPS)

# ── Core library ─────────────────────────────────────────────────────────────
# Compiled once into an archive. Tests link against it instead of recompiling the
# whole core per test file.
$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(OPT) -MMD -MP -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	@ar rcs $@ $(OBJ)

$(BUILD)/%: apps/%.cpp $(LIB)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(OPT) $< $(LIB) -o $@

-include $(OBJ:.o=.d)

# ── Running things ───────────────────────────────────────────────────────────
demo: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 40 --hours 1 --synthetic 5000 --show 12

# The project review, as one deterministic run that tells the story in order and
# ends with the dashboard. tools/review.sh has the steps; PAUSE=1 waits for Enter
# between them. Writes only to build/review/ and dashboard.html.
review: $(APPS)
	@PAUSE=$(PAUSE) tools/review.sh

# A single self-contained HTML file -- no server, no network, no Leaflet.
dashboard: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 60 --hours 2 --synthetic 400 --show 6 --export-html dashboard.html

# Writes bench/results/*.csv and bench/results/environment.txt (compiler, flags,
# OS, CPU, power, load, and the commit whose CODE produced the numbers), then
# redraws the scaling chart and re-renders the docs tables if Python 3 is
# available. The dirty check looks only at code paths: the redirect below has
# already truncated environment.txt -- itself a tracked file -- by the time it
# runs, so checking the whole tree flagged every run as uncommitted.
bench: $(BUILD)/safetrail_bench
	@mkdir -p bench/results
	@{ echo "date:     $$(date -u +%Y-%m-%dT%H:%MZ)"; \
	   echo "commit:   $$(git rev-parse --short HEAD 2>/dev/null)$$(git diff --quiet HEAD -- src include apps tools/build Makefile CMakeLists.txt 2>/dev/null || echo ' (uncommitted code changes)')"; \
	   echo "compiler: $$($(CXX) --version 2>/dev/null | head -1)"; \
	   echo "flags:    -std=c++17 $(OPT) -ffp-contract=off"; \
	   echo "os:       $$(uname -srm)"; \
	   echo "cpu:      $$(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')"; \
	   echo "cores:    $$(getconf _NPROCESSORS_ONLN 2>/dev/null)"; \
	   echo "power:    $$(pmset -g batt 2>/dev/null | head -1 | sed "s/Now drawing from //; s/'//g" || echo n/a)"; \
	   echo "load:     $$(uptime | sed 's/.*load average[s]*: //')"; } > bench/results/environment.txt
	@./$(BUILD)/safetrail_bench --out bench/results
	@command -v python3 >/dev/null && python3 tools/plot_scaling.py >/dev/null && \
	  python3 tools/render_results.py && echo "  chart and results tables regenerated" || true

# The scaling and end-to-end sections again, three times, so the docs can quote
# each speedup as a range over runs rather than one run's number. Run after
# `make bench`, on the same machine, then re-render.
bench-variation: $(BUILD)/safetrail_bench
	@for i in 1 2 3; do mkdir -p bench/results/variation/run$$i; \
	  ./$(BUILD)/safetrail_bench --only 1,2,3 --out bench/results/variation/run$$i >/dev/null \
	  && echo "  variation run $$i done"; done
	@python3 tools/render_results.py

# ── Tests ────────────────────────────────────────────────────────────────────
$(BUILD)/test/%: tests/%.cpp $(LIB)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -O1 -Itests -MMD -MP $< $(LIB) -o $@

-include $(TEST_BIN:=.d)

test: $(TEST_BIN)
	@fail=0; for t in $(TEST_BIN); do ./$$t || fail=1; done; \
	 echo; [ $$fail -eq 0 ] && echo "ALL TESTS PASS" || { echo "TESTS FAILED"; exit 1; }

# The randomised tests (differential, property, fuzz) scale their seed counts by
# SAFETRAIL_STRESS; 10 is ~10x the default work.
stress: $(TEST_BIN)
	@SAFETRAIL_STRESS=10 $(MAKE) --no-print-directory test

# Byte-identical output across runs of the same seed. Determinism is a claim the
# README makes, so it is a gated test, not a footnote.
determinism: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson \
	   --tourists 30 --hours 1 --seed 4242 --show 40 > $(BUILD)/det_a.txt
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson \
	   --tourists 30 --hours 1 --seed 4242 --show 40 > $(BUILD)/det_b.txt
	@cmp -s $(BUILD)/det_a.txt $(BUILD)/det_b.txt \
	  && echo "determinism: identical output across runs" \
	  || { echo "DETERMINISM FAILURE: runs diverged"; diff $(BUILD)/det_a.txt $(BUILD)/det_b.txt | head; exit 1; }

# ── Sanitizers ───────────────────────────────────────────────────────────────
# The WHOLE suite, not a hand-picked subset. The core is compiled once with the
# sanitizers on, so covering every test costs a per-test compile and run.
#
#   make asan   AddressSanitizer + UndefinedBehaviorSanitizer (what CI runs)
#   make ubsan  UBSan only, into a separate build directory
#   make sanitize   picks asan on Linux, ubsan on macOS
#
# Why macOS gets UBSan only: AddressSanitizer's runtime hangs on macOS 26 with
# Apple clang 17 -- an empty `int main(){}` linked with -fsanitize=address never
# reaches main -- so no ASan binary can run on such a host. That is a platform
# bug, so ASan stays authoritative in Linux CI. Apple's UBSan is also weaker than
# g++'s (a null memcpy source that g++'s UBSan catches passes clean), so a green
# local run is not a promise that CI will be green.
SAN_KIND  ?= address,undefined
SAN_DIR   ?= $(BUILD)/asan
# -fno-sanitize-recover makes UB abort rather than print-and-continue: a
# sanitizer whose findings do not fail the build is a sanitizer nobody reads.
SAN_FLAGS := -O1 -g -fsanitize=$(SAN_KIND) -fno-omit-frame-pointer \
             -fno-sanitize-recover=undefined
SAN_OBJ   := $(patsubst src/%.cpp,$(SAN_DIR)/obj/%.o,$(SRC))
SAN_LIB   := $(SAN_DIR)/libsafetrail.a
SAN_BIN   := $(patsubst tests/%.cpp,$(SAN_DIR)/test/%,$(TEST_SRC))

$(SAN_DIR)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(SAN_FLAGS) -c $< -o $@

$(SAN_LIB): $(SAN_OBJ)
	@mkdir -p $(dir $@)
	@ar rcs $@ $(SAN_OBJ)

$(SAN_DIR)/test/%: tests/%.cpp $(SAN_LIB)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(SAN_FLAGS) -Itests $< $(SAN_LIB) -o $@

asan: $(SAN_BIN)
	@fail=0; for t in $(SAN_BIN); do ./$$t || fail=1; done; \
	 echo; [ $$fail -eq 0 ] && echo "SANITIZERS CLEAN ($(SAN_KIND))" \
	                       || { echo "SANITIZER FAILURES"; exit 1; }

ubsan:
	@$(MAKE) --no-print-directory asan SAN_KIND=undefined SAN_DIR=$(BUILD)/ubsan

sanitize:
	@if [ "$$(uname -s)" = Darwin ]; then $(MAKE) --no-print-directory ubsan; \
	 else $(MAKE) --no-print-directory asan; fi

# ── Verification beyond the test suite ───────────────────────────────────────
# Mutation testing: tools/mutation_check.py applies known-bad edits to the core
# (a dropped subtree, a missing refit, a skipped reconciliation, ...) one at a
# time and requires a test to fail for each. A suite that stays green when the
# code is broken is measuring nothing.
mutation:
	@python3 tools/mutation_check.py

analyze:
	@if [ -z "$(IS_CLANG)" ]; then echo "analyze: needs clang (CXX=clang++)"; exit 1; fi
	@mkdir -p $(BUILD)/analyze; fail=0; \
	 for f in $(SRC) apps/*.cpp; do \
	   out=$$($(CXX) -std=c++17 -Iinclude --analyze -Xanalyzer -analyzer-output=text \
	          -o $(BUILD)/analyze/$$(basename $$f).plist $$f 2>&1); \
	   if [ -n "$$out" ]; then echo "$$out"; fail=1; fi; done; \
	 [ $$fail -eq 0 ] && echo "static analysis: no findings" || exit 1

# Every header compiles on its own (no hidden include-order dependencies).
check:
	@fail=0; for h in $$(find include -name '*.hpp' | sort); do \
	  rel=$${h#include/}; \
	  out=$$(printf '#include "%s"\nint main(){}\n' "$$rel" | $(CXX) $(CXXFLAGS) -fsyntax-only -x c++ - 2>&1); \
	  if [ -n "$$out" ]; then echo "  FAIL $$rel"; echo "$$out" | head -5; fail=1; fi; done; \
	[ $$fail -eq 0 ] && echo "all headers compile standalone" || exit 1

# Everything that gates a merge: documentation links, warnings as errors, header
# hygiene, the suite, determinism, the platform's sanitizer run, the CMake
# build + ctest, and the two builds agreeing byte for byte on one seed.
validate:
	@python3 tools/check_docs.py
	@python3 tools/render_results.py --check
	@$(MAKE) --no-print-directory WERROR=1 check all test determinism
	@$(MAKE) --no-print-directory sanitize
	@$(MAKE) --no-print-directory cmake-build
	@ctest --test-dir $(BUILD)-cmake --output-on-failure -j4 >/dev/null \
	  && echo "cmake + ctest: pass" || { echo "cmake + ctest: FAIL"; exit 1; }
	@./$(BUILD)-cmake/safetrail_headless --zones data/zones/shillong_osm.geojson \
	   --tourists 30 --hours 1 --seed 4242 --show 40 > $(BUILD)/det_cmake.txt
	@cmp -s $(BUILD)/det_a.txt $(BUILD)/det_cmake.txt \
	  && echo "determinism across builds: Make (-O2) and CMake (-O3) binaries agree byte for byte" \
	  || { echo "DETERMINISM FAILURE: Make and CMake binaries diverge"; exit 1; }
	@echo; echo "VALIDATION PASSED"

manifest:
	@echo "sources ($(words $(SRC))):"; printf '  %s\n' $(SRC)
	@echo "tests ($(words $(TEST_SRC))):"; printf '  %s\n' $(TEST_SRC)

cmake-build:
	@cmake -B $(BUILD)-cmake -DCMAKE_BUILD_TYPE=Release -DSAFETRAIL_WERROR=ON -S . >/dev/null \
	  && cmake --build $(BUILD)-cmake -j >/dev/null && echo "cmake build: ok"

rebuild: clean
	@$(MAKE) --no-print-directory all test

clean:
	@rm -rf $(BUILD) $(BUILD)-cmake

help:
	@sed -n '3,18p' Makefile | sed 's/^# \{0,1\}//'
