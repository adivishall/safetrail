# Point-in-Polygon: The Cases That Break It

Ray casting is about fifteen lines. Getting it right is not. Every case below has
a test in `tests/geo/ray_casting_test.cpp`, and most naive implementations fail at
least three of them.

## The algorithm

Cast a ray from the test point (we use +x, due east) and count how many polygon
edges it crosses. Odd means inside.

## The cases

**1. Point exactly on an edge.**
Undefined by the parity rule. We *define* it: on-boundary counts as inside.
Document the choice, test it, be consistent — a zone and its neighbour sharing an
edge must not both reject a point on that edge.

"On the edge" needs a tolerance, and the tolerance must be a **distance**: a
point is on an edge when its perpendicular distance to the edge is at most
1e-9° (~0.1 mm) and it lies within the edge's extent. The implementation used to
bound the raw cross product instead, which is |edge| × distance — so the
boundary was ~1 mm thick on a 100 m edge and ~11 cm thick on a 1 m edge, and a
fix 10 cm *outside* a small zone counted as inside. Pinned for 1 m, 100 m and
5 km edges in `tests/geo/ray_casting_test.cpp`.

**2. Ray passes exactly through a vertex.**
The classic failure. The vertex belongs to two edges, so naive code counts two
crossings where there is one, flipping parity and inverting the answer. Fix: use a
half-open rule on the y-comparison — count an edge only if
`(y1 > py) != (y2 > py)`. Each vertex then contributes to exactly one of its edges.

**3. Horizontal edge collinear with the ray.**
Infinite intersections. The half-open rule in case 2 handles this for free — a
horizontal edge has `y1 == y2`, so the comparison is never unequal and it never
counts.

**4. Concave polygons.**
Any implementation assuming convexity is wrong. A ray can cross a concave polygon
four times. Test with a star and a C-shape.

**5. Polygons with holes.**
An exempt village inside a restricted forest block. Crossings against hole rings
must flip parity too — count crossings against every ring, outer and holes
together, and apply the odd rule once at the end. Counting holes separately and
subtracting is the common bug: it breaks on nested holes.

Holes also need validating to the same standard as the shell, and for three
failure modes the shell does not have: a hole outside the shell, a hole whose
*edge* crosses the shell (possible with a concave shell even when every hole
vertex is inside), and two holes that overlap or nest. Parity assumes holes are
disjoint sub-regions strictly inside the shell; when they are not, containment is
arbitrary in exactly the way a self-intersecting ring makes it arbitrary. See
`Polygon::Validity` and `tests/geo/polygon_holes_test.cpp`.

**Boundary contact is refused, and that is the opposite of rule 1 on purpose.**
A hole that merely *touches* the shell — flush along an edge, or at a single
vertex — is rejected (`HoleCrossesOuter`), and so are two holes that touch each
other (`HolesOverlap`). Contact pinches the region to zero width at that point,
and "is the pinch point inside?" has no right answer for the two containment
implementations to agree on; the fix on the operator's side is to draw the hole a
millimetre clear, which is not a hardship.

That is deliberately stricter than rule 1, where a point ON a boundary counts as
inside. The two rules answer different questions. Validation asks whether the
GEOMETRY is well defined, and refusing an ambiguous shape is the whole job.
Containment asks where a GPS fix falls in geometry already known to be well
defined, and there the boundary case must resolve one way — "inside" being the
safe direction for a hazard zone. Stating both here because a reader who meets
them in separate files will otherwise read them as an inconsistency.

Metrics are region metrics, not ring metrics: area subtracts holes, the centroid
is the region's — otherwise a ring-shaped zone's label sits in the hole, i.e.
outside itself — and the perimeter includes hole boundaries, because crossing one
takes you out of the zone.

**6. Self-intersecting polygons.**
Genuinely undefined — "inside" has no meaning for a figure-eight. Do not paper
over it. Reject at authoring time (`Polygon::validate()`, Gap 10) and never let one
into `ZoneStore`.

Detection is **Shamos–Hoey**, O(V log V), over a hand-written AVL status structure
(`geo/sweep_line.hpp`) — not Bentley–Ottmann. The distinction is worth keeping
straight: Bentley–Ottmann *enumerates* all k intersection points in
O((V+k) log V); Shamos–Hoey answers *does one exist* and stops at the first, with
no k term at all. A validity gate only ever needs the second question, so paying
for the first would be paying for an answer nobody reads. The O(V²) pairwise scan
is kept beside it as the oracle and is what runs on rings below 80 vertices, where
it is genuinely faster — `make bench` §16 measures the crossover.

**Degeneracies are where a sweep line breaks**, and ring validation is all
degeneracies: adjacent edges share a vertex by definition, so they are exempt
from each other, and exemptions break the textbook argument that a crossing pair
must become adjacent in the sweep's status order. The implementation tested a
new segment only against its *first* non-exempt neighbour on each side, and a
touching segment one step further out was never compared. Fuzzing on lattice
rings — touches, repeated vertices, collinear fold-backs — found 94 rings in
800,000 that the sweep called simple and the pairwise oracle did not. It now
tests every eligible segment among the nearest three on each side (an edge has
at most two exempt partners), which is still O(log V) per event, and events at
the same x are processed in a total order so the result cannot depend on the
standard library's sort. `tests/geo/sweep_line_test.cpp` pins the failing rings
and fuzzes 12,000 more on every run.

**7. Antimeridian crossing (±180° longitude).**
Not relevant for Northeast India, and worth splitting into what IS handled and
what is not, because they are different things.

*Handled.* `distance_m()` and `bearing_deg()` need no special case: the longitude
delta enters only through periodic functions, so a raw delta of -359.8° evaluates
identically to the true +0.2° crossing. `offset()` and `LocalPlane` do need it,
because they *produce* a longitude rather than consuming one, and both normalise
to (-180, 180]. `tests/geo/wraparound_test.cpp` pins all of it.

*Not handled — and refused.* A polygon spanning the antimeridian would need a
split-at-the-seam representation, which is scope this project does not need. So
the zone loader rejects any coordinate that is non-finite or off the
lat/lon domain (a ring stored as lon 179.5 → 180.5 is refused with a reason)
rather than loading a zone that half-works. The spatial indexes themselves are
correct on out-of-domain boxes — the quadtree and the persistent index widen
their roots, the geohash clamps monotonically — which
`tests/index/differential_test.cpp` checks, but correct pruning is not the same
as meaningful geography.

The same boundary applies to the query box. `Bbox::around(c, r)` is the exact
bounding box of the spherical cap of radius r on the sphere `distance_m` uses:
half-width r/R in latitude, `asin(sin(r/R) / cos(lat))` in longitude (a great
circle bulges poleward), every longitude when the cap reaches a pole. It used to
be built from WGS84 metres-per-degree constants and came out 0.11% too narrow
east-west against the haversine metric — enough for the index to drop a zone the
exact test would have called Uncertain. The filter must be a superset of what the
refinement can accept; `tests/geo/bbox_around_test.cpp` samples 160,000 points to
hold it to that.

**8. Degenerate rings.**
Fewer than three vertices, zero area, repeated vertices. Rejected at load: a
repeated vertex makes two non-adjacent edges share a point, which validation
counts as self-intersection.

**9. Floating-point boundary noise.**
A point 10⁻¹⁵ from an edge. Use an epsilon comparison, and pick it deliberately:
1e-9 degrees is roughly 0.1 mm, far below GPS accuracy, so it is safe. These are
tolerance-based predicates, not exact (adaptive-precision) ones: orientation
treats a cross product within 1e-14 deg² of zero as collinear. Every caller —
containment, validation, the sweep, jurisdiction nesting — shares the one set of
predicates in `src/geo/segment.cpp`, so the layers cannot disagree with each
other about where an edge is, but a configuration that close to collinear is
decided by the tolerance rather than exactly.

**10. Winding direction.**
Clockwise vs counter-clockwise changes the sign of the area but must not change
containment. Ray casting is direction-agnostic — a crossing is a crossing whichever
way the edge runs. The winding-number implementation is *not*, and this is the
case that actually bit: the textbook rule requires holes wound opposite to the
shell, so a counter-clockwise hole inside a counter-clockwise shell gives
w = 1 + 1 = 2 at the hole's centre — "non-zero, therefore inside" — while ray
casting correctly says outside. Most GeoJSON producers get hole orientation wrong,
so this is the common case, not the exotic one.

Rather than requiring callers to normalise on load, `contains_winding()` normalises
hole orientation itself (comparing each hole's signed area against the shell's), so
the two implementations agree however a file was authored. The disagreement was
found by the hole test — which is exactly why the second implementation is kept.

## Why two implementations

`contains()` (ray casting) and `contains_winding()` (winding number) are both
kept permanently. They are cross-validated on randomised input in
`tests/geo/ray_casting_test.cpp` and `tests/geo/polygon_holes_test.cpp`, and
**they disagree exactly where the hard cases live**. That is not a hypothetical:
case 10 above was found precisely this way, by the two implementations returning
opposite answers for a hole's interior.

## Uncertainty changes the question

With `UncertainPoint` (Gap 1), the question stops being "is this point inside" and
becomes "could the true position be inside". That needs distance to the nearest
edge, not a parity test — so `signed_distance_m()` has its own set of edge cases:
nearest point on a segment vs at a vertex, and the sign convention for points
inside holes. Test it separately.

One more, easy to miss: the projection of a point onto a segment needs a parameter
`t`, and computing `t` in *degree* space treats a degree of longitude as equal to a
degree of latitude. At Shillong the true ratio is cos(25.57°) = 0.902, an 11% skew
on the east-west axis, which lands `t` in the wrong place along any slanted edge.
`geo/projection.hpp` converts to a local east-north plane in metres first; its
error budget is measured, not assumed, and printed by
`tests/geo/projection_test.cpp` on every run.
