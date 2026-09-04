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

Metrics are region metrics, not ring metrics: area subtracts holes, the centroid
is the region's — otherwise a ring-shaped zone's label sits in the hole, i.e.
outside itself — and the perimeter includes hole boundaries, because crossing one
takes you out of the zone.

**6. Self-intersecting polygons.**
Genuinely undefined — "inside" has no meaning for a figure-eight. Do not paper
over it. Reject at authoring time with Bentley–Ottmann
(`geo/sweep_line.hpp`, Gap 10) and never let one into `ZoneStore`.

**7. Antimeridian crossing (±180° longitude).**
Not relevant for Northeast India, and worth splitting into what IS handled and
what is not, because they are different things.

*Handled.* `distance_m()` and `bearing_deg()` need no special case: the longitude
delta enters only through periodic functions, so a raw delta of -359.8° evaluates
identically to the true +0.2° crossing. `offset()` and `LocalPlane` do need it,
because they *produce* a longitude rather than consuming one, and both normalise
to (-180, 180]. `tests/geo/wraparound_test.cpp` pins all of it.

*Not handled.* A polygon whose bbox spans the antimeridian still inverts, and the
index would silently reject everything in it. That limitation stands, documented
rather than papered over — the fix is a split-at-the-seam representation, which is
scope this project does not need.

**8. Degenerate rings.**
Fewer than three vertices, zero area, duplicate consecutive points. Reject at
load.

**9. Floating-point boundary noise.**
A point 10⁻¹⁵ from an edge. Use an epsilon comparison, and pick it deliberately:
1e-9 degrees is roughly 0.1 mm, far below GPS accuracy, so it is safe.

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
