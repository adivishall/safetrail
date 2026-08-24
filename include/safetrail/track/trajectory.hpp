#pragma once
// Trajectory analysis over a tourist's recent GPS pings.
//
// The ping history (a circular buffer, ds/circular_buffer.hpp) is a short window
// of recent fixes. These are the summary statistics the anomaly detector and the
// operator UI read off it: how far the person actually walked, how far they got
// (net), how much they wandered, and their recent average speed.
//
// All distances are great-circle metres (geo::distance_m). Pure reads -- nothing
// here mutates the tourist.
#include "safetrail/track/tourist.hpp"

namespace safetrail::track {

// Total path walked: the sum of distances between consecutive pings. O(window).
double path_length_m(const Tourist& t);

// Net displacement: straight-line distance from the oldest ping to the newest.
double displacement_m(const Tourist& t);

// Tortuosity = path / displacement. 1.0 means a straight line; large means a lot
// of wandering for little net progress (a person circling, lost, or milling). If
// there is no displacement but the path is non-zero, returns a large sentinel.
double tortuosity(const Tourist& t);

// Average speed over the last `window_ms` of pings, in m/s. Uses the pings' own
// timestamps, so it is robust to skipped fixes from the adaptive sampler.
double average_speed_mps(const Tourist& t, int64_t window_ms);

}  // namespace safetrail::track
