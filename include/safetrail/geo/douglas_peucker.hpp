#pragma once
// safetrail :: Polyline simplification
//
// NOT IN THE ENGINE, BY DESIGN. Douglas-Peucker boundary simplification runs once,
// offline, in the data-prep tool (`tools/osm_to_zones.py`) when an OpenStreetMap
// extract is converted into the project's zone set. By the time the engine loads a
// zone the simplification has already happened, so a runtime implementation would
// have no caller.
//
// If simplification ever needs to happen at runtime -- an operator drawing a
// hand-traced boundary, say -- this is where it goes, and the O(n log n) average /
// O(n^2) worst-case analysis belongs with it.

namespace safetrail::geo {

// Intentionally empty. See the note above.

}  // namespace safetrail::geo
