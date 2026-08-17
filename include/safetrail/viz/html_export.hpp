#pragma once
// Self-contained HTML dashboard export.
//
// Deliberately NOT an HTTP server with a WebSocket. One file, opened with
// file://, no network, no tile server, no Leaflet, no build step -- because the
// demo has to work on a projector in a room with no wifi. The map is drawn on a
// canvas from the zone polygons we already have, which also means the diagnostics
// overlay can draw the actual spatial-index node boxes rather than approximating
// them.
#include <string>
#include "safetrail/sim/simulator.hpp"

namespace safetrail::viz {

struct ExportOptions {
  int64_t frame_interval_ms = 10000;   // one animation frame per 10 s of sim
  size_t  max_events = 4000;
  std::string title = "safetrail";
};

// Captures frames while the simulation runs, then writes the dashboard.
class TraceRecorder {
 public:
  explicit TraceRecorder(ExportOptions opt = {}) : opt_(opt) {}

  void capture(const sim::Simulator& s);                  // call each tick
  bool write_html(const sim::Simulator& s, const std::string& path) const;

  size_t frames() const { return frames_.size(); }

 private:
  struct Frame {
    int64_t t_ms;
    std::vector<float> lat, lon;
    std::vector<uint8_t> state;   // 0 outside, 1 uncertain, 2 inside
    std::vector<float> acc;
  };
  ExportOptions opt_;
  std::vector<Frame> frames_;
  int64_t last_capture_ms_ = -1;
};

}  // namespace safetrail::viz
