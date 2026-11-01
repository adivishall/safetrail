#pragma once
//
// Scenario runner: reproducible demonstrations driven through the real platform.
//
// A scenario is a script of SIMULATED people (every one is created with
// Source::Scenario and a "SIM" display name, so nothing on screen can be taken
// for a real person) walking scripted paths, plus timed actions -- create a
// group, give consent, go offline, reconnect. Each tick the runner turns every
// actor's ground-truth position into a noisy GPS fix with the simulator's own
// error model (sim::apply_gps_error) and sends it through
// Platform::ingest_fix -- the same entry point the HTTP API uses for a phone.
// Nothing bypasses the pipeline: whatever the operator sees during a demo is
// what the engine actually concluded from those fixes.
//
// Everything is deterministic in the seed: the same scenario produces the same
// event log, byte for byte, which tests/app/scenario_test.cpp asserts.
//
// Control: load, start, pause, resume, reset, step(n ticks), speed (0.25x to
// 60x). The clock is simulated, so a pause really stops time -- nothing
// escalates while the presenter is talking.
//
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "safetrail/app/platform.hpp"
#include "safetrail/sim/mobility.hpp"

namespace safetrail::app {

struct ScenarioInfo {
  std::string id, title, summary;
  int64_t duration_ms = 0;
};

enum class RunState : uint8_t { Idle, Paused, Running, Finished };
const char* to_string(RunState s);

struct NarrationStep {
  int64_t     at_ms = 0;
  std::string text;
  bool        done = false;
};

class ScenarioRunner {
 public:
  // The runner drives whatever platform `world` currently points at; reset()
  // asks `rebuild` for a fresh one (same staff accounts, empty history).
  ScenarioRunner(std::unique_ptr<Platform>* world, std::function<void()> rebuild);
  ~ScenarioRunner();

  static std::vector<ScenarioInfo> catalogue();

  // Resets the world first -- unless `fresh`: the world was just built and is
  // untouched, so it is used as it is (server start-up, where building it twice
  // doubled a cold start on a small host).
  bool load(const std::string& id, std::string* err, bool fresh = false);
  void start();
  void pause();
  void resume();
  void reset();                                          // reload the same scenario
  void set_speed(double x);
  void step(int ticks);                                  // advance n seconds while paused
  // Called by the server loop with wall-clock seconds elapsed; runs the ticks
  // that `speed` says are due. Returns ticks run.
  int advance(double real_seconds);

  RunState state() const { return state_; }
  const std::string& scenario_id() const { return id_; }
  double speed() const { return speed_; }
  int64_t duration_ms() const { return duration_ms_; }
  const std::vector<NarrationStep>& narration() const { return narration_; }

  // The device-side panel: for each simulated phone, what the PHONE knows.
  struct DeviceView {
    UserId user = kNoId;
    std::string name;
    bool offline = false;
    size_t queued = 0;            // events waiting on the device
    int64_t clock_skew_ms = 0;
    bool battery_aware = false;
    uint64_t fixes_sent = 0, fixes_skipped = 0;
  };
  std::vector<DeviceView> devices() const;
  const std::vector<std::string>& sync_reports() const { return sync_reports_; }

  struct Script;   // defined in the .cpp

 private:
  std::unique_ptr<Platform>* world_;
  std::function<void()> rebuild_;
  std::unique_ptr<Script> script_;
  std::string id_;
  RunState state_ = RunState::Idle;
  double speed_ = 1.0;
  double accum_ms_ = 0;
  int64_t duration_ms_ = 0;
  std::vector<NarrationStep> narration_;
  std::vector<std::string> sync_reports_;

  void tick_once();
};

}  // namespace safetrail::app
