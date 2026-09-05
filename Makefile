# Plain make, no cmake needed. `make cmake-build` uses CMake if you have it.
#
# Source set: both this Makefile and CMakeLists.txt discover sources by globbing
# src/**.cpp and tests/**_test.cpp. Neither maintains a hand-written list, so the
# two build systems cannot drift apart -- which they did, historically, when the
# Makefile carried an explicit CORE list. `make manifest` prints what is found.

CXX      ?= c++
CXXFLAGS := -std=c++17 -Iinclude -Wall -Wextra -Wpedantic
OPT      ?= -O2
BUILD    ?= build

# `sort` for determinism: glob order is filesystem-dependent, and a reproducible
# link order is one less variable when chasing a nondeterministic result.
SRC      := $(sort $(shell find src -name '*.cpp'))
OBJ      := $(patsubst src/%.cpp,$(BUILD)/obj/%.o,$(SRC))
LIB      := $(BUILD)/libsafetrail.a

TEST_SRC := $(sort $(shell find tests -name '*_test.cpp'))
TEST_BIN := $(patsubst tests/%.cpp,$(BUILD)/test/%,$(TEST_SRC))

APPS     := $(BUILD)/safetrail_headless $(BUILD)/safetrail_bench

.PHONY: all demo dashboard bench test check asan ubsan cmake-build clean help manifest determinism

all: $(APPS)

# ── core library ─────────────────────────────────────────────────────────────
# Compiled once into an archive. Tests link against it instead of recompiling the
# whole core per test file, which took the suite from ~4 min to well under one.
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

# ── running things ───────────────────────────────────────────────────────────
demo: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 40 --hours 1 --synthetic 5000 --show 12

# Writes a single self-contained HTML file -- no server, no network, no Leaflet.
dashboard: $(BUILD)/safetrail_headless
	@./$(BUILD)/safetrail_headless --zones data/zones/shillong_osm.geojson --tourists 60 --hours 2 --synthetic 400 --show 6 --export-html dashboard.html

bench: $(BUILD)/safetrail_bench
	@mkdir -p bench/results
	@./$(BUILD)/safetrail_bench --out bench/results

# Render the slide deck to a PDF (one landscape page per slide) via headless
# Chrome. CHROME is overridable because the binary's name and path differ per
# platform; the default probes the usual macOS and Linux locations.
CHROME ?= $(firstword $(wildcard \
            /Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome \
            /usr/bin/google-chrome /usr/bin/chromium /usr/bin/chromium-browser) \
          google-chrome)
pdf:
	@"$(CHROME)" --headless --disable-gpu --no-pdf-header-footer \
	  --print-to-pdf="$(PWD)/safetrail-slides.pdf" --print-to-pdf-no-header \
	  --virtual-time-budget=4000 "file://$(PWD)/slides.html" 2>/dev/null
	@echo "wrote safetrail-slides.pdf"

# ── tests ────────────────────────────────────────────────────────────────────
$(BUILD)/test/%: tests/%.cpp $(LIB)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -O1 -Itests -MMD -MP $< $(LIB) -o $@

-include $(TEST_BIN:=.d)

test: $(TEST_BIN)
	@fail=0; for t in $(TEST_BIN); do ./$$t || fail=1; done; \
	 echo; [ $$fail -eq 0 ] && echo "ALL TESTS PASS" || { echo "TESTS FAILED"; exit 1; }

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

# ── sanitizers ───────────────────────────────────────────────────────────────
# The WHOLE suite, not a hand-picked pair. The core is compiled once with the
# sanitizers on and archived, so the marginal cost of covering every test is a
# per-test compile and run -- cheap enough that narrowing coverage would only be
# hiding failures.
#
# `make asan`   AddressSanitizer + UndefinedBehaviorSanitizer. What CI runs.
# `make ubsan`  UBSan only, into a separate build directory.
#
# Why the second target exists, since one sanitizer set would be tidier:
# AddressSanitizer's runtime is currently broken on macOS 26 with Apple clang 17 --
# an empty `int main(){}` linked with -fsanitize=address hangs in dyld's
# __malloc_init before reaching main, so no ASan binary of any kind can run on
# such a host. That is a platform bug, not a project one, and the right response
# is to keep ASan authoritative in CI (Linux/g++, where it works) while leaving
# macOS developers a sanitizer they can actually run locally. Silently dropping
# sanitizer coverage on one platform would be worse than saying this out loud.
#
# And `make ubsan` is genuinely weaker than the CI job, not merely narrower, so
# a green local run is not a promise that CI will be green. Measured, not assumed:
# `memcpy(dst, (const char*)nullptr + 0, 0)` -- undefined because memcpy's source
# is declared nonnull and null-pointer arithmetic is UB at any offset -- passes
# clean under Apple clang's -fsanitize=undefined (with or without ,nullability)
# and is caught by g++'s. A real instance of exactly that lived in
# src/evidence/sha256.cpp, reached by the RFC 6962 empty-log root, until CI found
# it. So: run `make ubsan` before pushing, and expect CI to be the one that has
# the last word.
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

# ── hygiene ──────────────────────────────────────────────────────────────────
check:
	@fail=0; for h in $$(find include -name '*.hpp' | sort); do \
	  rel=$${h#include/}; \
	  out=$$(printf '#include "%s"\nint main(){}\n' "$$rel" | $(CXX) $(CXXFLAGS) -fsyntax-only -x c++ - 2>&1); \
	  if [ -n "$$out" ]; then echo "  FAIL $$rel"; echo "$$out" | head -5; fail=1; fi; done; \
	[ $$fail -eq 0 ] && echo "all headers compile standalone" || exit 1

manifest:
	@echo "sources ($(words $(SRC))):"; printf '  %s\n' $(SRC)
	@echo "tests ($(words $(TEST_SRC))):"; printf '  %s\n' $(TEST_SRC)

cmake-build:
	@cmake -B $(BUILD)-cmake -DCMAKE_BUILD_TYPE=Release -S . && cmake --build $(BUILD)-cmake -j

clean:
	@rm -rf $(BUILD) $(BUILD)-cmake build-rel

help:
	@echo "make            build the headless engine + benchmark"
	@echo "make demo       run the simulation, print the event stream and counters"
	@echo "make dashboard  build dashboard.html (self-contained, just open it)"
	@echo "make bench      index scaling, equivalence, hysteresis A/B -> bench/results/"
	@echo "make test       unit + golden + integration tests"
	@echo "make asan       the whole test suite under ASan + UBSan"
	@echo "make ubsan      the whole suite under UBSan only (macOS ASan fallback)"
	@echo "make determinism  same seed twice, assert byte-identical output"
	@echo "make check      syntax-check every header standalone"
	@echo "make manifest   print the source/test set both build systems see"
