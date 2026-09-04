#pragma once
// The segment predicates, in one place.
//
// Orientation and segment-intersection are needed by four separate modules --
// polygon validation, the Bentley-Ottmann sweep, ray-casting's on-boundary rule,
// and jurisdiction nesting. They were written three times with three slightly
// different epsilons, which is exactly how two layers of the same system end up
// disagreeing about whether a point is inside a zone. One definition now; every
// caller shares its tolerances, so "do these cross?" has a single answer
// everywhere in the engine.
//
// Coordinates are treated as planar (x = lon, y = lat). That is correct for these
// predicates: orientation and crossing are affine-invariant, and the lon/lat
// anisotropy (geo/projection.hpp) is a uniform scaling of one axis, which cannot
// change the SIGN of a cross product or whether two segments meet. Distances are
// a different matter and never computed here.
#include "safetrail/geo/point.hpp"

namespace safetrail::geo {

// Sign of the cross product (b-a) x (c-a): +1 counter-clockwise, -1 clockwise,
// 0 collinear within tolerance.
int orientation(const LatLon& a, const LatLon& b, const LatLon& c);

// Is p on the closed segment [a, b]? Collinear AND within the segment's extent.
// Used by ray casting, where "on the boundary" is DEFINED as inside.
bool point_on_segment(const LatLon& a, const LatLon& b, const LatLon& p);

// Do segments [a,b] and [c,d] intersect at all -- proper crossing or touching?
// Touching counts: for polygon validation a ring edge that merely grazes a
// non-adjacent edge is still malformed geometry.
bool segments_intersect(const LatLon& a, const LatLon& b,
                        const LatLon& c, const LatLon& d);

// Do segments [a,b] and [c,d] cross TRANSVERSALLY -- each passing strictly from
// one side of the other to the other side? Touching, sharing an endpoint, and
// collinear overlap all return false.
//
// The distinction matters and is not pedantry. Polygon validation wants
// segments_intersect: a ring edge that merely grazes a non-adjacent edge is
// malformed. Jurisdiction nesting wants this one: administrative boundaries
// routinely SHARE edges -- a block whose northern limit is the district's
// northern limit is normal, correctly nested data, and rejecting it would break
// the containment forest on exactly the real-world input it is built for.
bool segments_properly_cross(const LatLon& a, const LatLon& b,
                             const LatLon& c, const LatLon& d);

}  // namespace safetrail::geo
