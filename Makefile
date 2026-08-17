# Two build paths. `make` uses plain clang++ and needs nothing but a compiler.
# `make cmake-build` uses CMake if you have it (brew install cmake).

CXX      ?= clang++
CXXFLAGS := -std=c++17 -Iinclude -Wall -Wextra -Wpedantic -Wshadow -Wconversion
DEBUG    := -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
RELEASE  := -O2 -DNDEBUG

BUILD    := build
SRC      := $(shell find src -name '*.cpp' 2>/dev/null)
OBJ      := $(SRC:src/%.cpp=$(BUILD)/obj/%.o)
TESTS    := $(shell find tests -name '*_test.cpp' 2>/dev/null)
TESTBINS := $(TESTS:tests/%.cpp=$(BUILD)/test/%)
APPS     := $(wildcard apps/*.cpp)
APPBINS  := $(APPS:apps/%.cpp=$(BUILD)/%)

.PHONY: all check test bench run clean fmt cmake-build help

all: $(BUILD)/libsafetrail.a $(APPBINS)

# ── Syntax-check every header on its own. Catches missing includes early, which
#    matters in a header-heavy project like this one.
check:
	@fail=0; for h in $$(find include -name '*.hpp'); do \
	  rel=$${h#include/}; \
	  out=$$(printf '#include "%s"\nint main(){}\n' "$$rel" | $(CXX) $(CXXFLAGS) -fsyntax-only -x c++ - 2>&1); \
	  if [ -n "$$out" ]; then printf '  FAIL %s\n' "$$rel"; echo "$$out" | grep error: | head -3 | sed 's/^/       /'; fail=1; \
	  else printf '  ok   %s\n' "$$rel"; fi; done; exit $$fail

$(BUILD)/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(DEBUG) -c $< -o $@

$(BUILD)/libsafetrail.a: $(OBJ)
	@mkdir -p $(BUILD)
	@ar rcs $@ $(OBJ)

$(BUILD)/%: apps/%.cpp $(BUILD)/libsafetrail.a
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) $(DEBUG) $< -L$(BUILD) -lsafetrail -o $@

$(BUILD)/test/%: tests/%.cpp $(BUILD)/libsafetrail.a
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(DEBUG) $< -L$(BUILD) -lsafetrail -o $@

test: $(TESTBINS)
	@pass=0; fail=0; for t in $(TESTBINS); do \
	  if ./$$t >/dev/null 2>&1; then printf '  PASS %s\n' "$$(basename $$t)"; pass=$$((pass+1)); \
	  else printf '  FAIL %s\n' "$$(basename $$t)"; fail=$$((fail+1)); fi; done; \
	printf '\n  %d passed, %d failed\n' $$pass $$fail; [ $$fail -eq 0 ]

# Release build for benchmarks — sanitizers make timing numbers meaningless.
bench:
	@mkdir -p $(BUILD)-rel bench/results
	@$(CXX) $(CXXFLAGS) $(RELEASE) $(SRC) apps/safetrail_bench.cpp -o $(BUILD)-rel/safetrail_bench
	@./$(BUILD)-rel/safetrail_bench --out bench/results

run: all
	@./$(BUILD)/safetrail_server --scenario data/scenarios/quiet_day.json

cmake-build:
	@cmake -B $(BUILD)-cmake -DCMAKE_BUILD_TYPE=Debug -S . && cmake --build $(BUILD)-cmake -j

clean:
	@rm -rf $(BUILD) $(BUILD)-rel $(BUILD)-cmake

fmt:
	@find include src apps tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

help:
	@echo "make check   syntax-check every header standalone"
	@echo "make         build library + apps"
	@echo "make test    build and run all tests"
	@echo "make bench   release build, run benchmarks -> bench/results/"
	@echo "make run     start the server on the default scenario"
