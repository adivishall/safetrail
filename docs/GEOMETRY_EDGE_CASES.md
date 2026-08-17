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
together, and apply the odd rule once at the end.

**6. Self-intersecting polygons.**
Genuinely undefined — "inside" has no meaning for a figure-eight. Do not paper
over it. Reject at authoring time with Bentley–Ottmann
(`geo/sweep_line.hpp`, Gap 10) and never let one into `ZoneStore`.

**7. Antimeridian crossing (±180° longitude).**
Not relevant for Northeast India, but a bounding box spanning the antimeridian
inverts and silently rejects everything. Assert that zone bboxes do not wrap, and
document the limitation rather than pretending it is handled.

**8. Degenerate rings.**
Fewer than three vertices, zero area, duplicate consecutive points. Reject at
load.

**9. Floating-point boundary noise.**
A point 10⁻¹⁵ from an edge. Use an epsilon comparison, and pick it deliberately:
1e-9 degrees is roughly 0.1 mm, far below GPS accuracy, so it is safe.

**10. Winding direction.**
Clockwise vs counter-clockwise changes the sign of the area but must not change
containment. Ray casting is direction-agnostic; the winding-number implementation
is not, so normalise ring direction on load.

## Why two implementations

`contains()` (ray casting) and `contains_winding()` (winding number) are both
kept permanently. They are cross-validated on randomised input in
`tests/geo/equivalence_test.cpp`, and **they disagree exactly where the hard cases
live**. When a new edge case appears in production data, the disagreement finds it
before a user does.

## Uncertainty changes the question

With `UncertainPoint` (Gap 1), the question stops being "is this point inside" and
becomes "could the true position be inside". That needs distance to the nearest
edge, not a parity test — so `signed_distance_m()` has its own set of edge cases:
nearest point on a segment vs at a vertex, and the sign convention for points
inside holes. Test it separately.
