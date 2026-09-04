#pragma once
// safetrail :: Great-circle distance
//
// NOT A SEPARATE MODULE. Haversine distance, initial bearing and `offset()` are
// implemented in `geo/point.cpp`, right next to the `LatLon` type they operate on
// and the `Meters` type they are deliberately incompatible with. Splitting three
// short free functions into their own translation unit would buy nothing and put
// the "degrees are not a unit of distance" argument in a different file from the
// types that argument is about.
//
// This header survives only so that the several comments pointing at
// "haversine.hpp" lead somewhere that says where the code actually is, instead of
// to a 404. Include `geo/point.hpp`.

namespace safetrail::geo {

// Intentionally empty. See the note above.

}  // namespace safetrail::geo
