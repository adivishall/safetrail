#pragma once
// safetrail :: Replay recording
//
// SUPERSEDED. Trace recording is `viz::TraceRecorder` in `viz/html_export.cpp`,
// which captures per-tick state and writes the self-contained HTML replay. That is
// the whole of the recorder's intended job, and putting it next to the exporter
// that consumes it kept one format instead of two.
//
// This header survives so that references to "sim/recorder.hpp" point somewhere
// honest. Include `viz/html_export.hpp`.

namespace safetrail::sim {

// Intentionally empty. See the note above.

}  // namespace safetrail::sim
