# Viva questions and answers

Answers are short enough to say aloud and point to where to show the
evidence. Earlier review Q&A, mostly about the engine, is in
[../REVIEW_QA.md](../REVIEW_QA.md) and [../presentation/04_TEACHER_QA.md](../presentation/04_TEACHER_QA.md).

## The problem and the product

**1. What problem does SafeTrail solve?**
Tourists in remote areas walk into hazards — cliffs, rivers, closed roads —
and help often arrives late because nobody knew where they were or what was
happening. SafeTrail tracks people who opt in, checks every position against
hazard zones, turns what it finds into alerts and incidents, sends the nearest
responder along the road network, keeps working when phones lose signal, and
keeps a tamper-evident record of what happened.

**2. Why is this a data-structures project and not just an app?**
Because the core cost is a data-structures problem: every GPS fix asks "which
of n zones could contain this point?" Scanning is O(n) per fix; the indexes make
it O(log n + k). Around that sit an AVL interval tree for time, a persistent
quadtree for history, a heap for routing and triage, union-find for groups and
incidents, a hash table, a timer wheel, a ring buffer and a Merkle tree — each
doing a job the product needs.

**3. Is anything real?**
The hazard zones and the 144,090-junction road network are real
OpenStreetMap data around Shillong. The people are simulated, and every
simulated person is labelled SIM. No field trial has been done.

**4. How do you know different users don't interfere?**
Each user owns one `track::Tourist` object holding all their per-zone state,
and the evaluator touches only that object. The tests check it from the
outside anyway: two users enter the same zone independently, one leaves while
the other stays, one stops tracking — each user's events and alerts stay their
own (`tests/app/multi_user_test.cpp`).

## Indexes

**5. Quadtree versus R-tree?**
A quadtree splits *space* into four fixed quadrants; an R-tree groups the
*items* into bounding boxes that may overlap. The quadtree is simpler and its
cells never overlap; the R-tree adapts to where data actually is and, bulk
loaded with STR, is faster here. Show the Lab's spatial experiment: same query,
both trees' nodes, visited and pruned.

**6. Why keep brute force?**
It is the oracle. Every fast index is compared against it on randomised hostile
workloads, and the Lab prints the mismatch count for every query.

**7. Why does the speedup stop growing at large n in one district?**
Because about 100 zones genuinely overlap each query; every index must return
them. O(log n + k) — the k term dominates. At constant density (k ≈ 1) the
speedup keeps growing, to about 1,000× at 100,000 zones.

**8. What is STR bulk loading?**
Sort-Tile-Recursive: sort boxes by longitude, cut into √P vertical slices, sort
each slice by latitude, pack runs of M into leaves, repeat upward. It produces
fewer, fuller, less-overlapping nodes than inserting one at a time: 6.3× faster
queries at 100,000 zones in our measurement.

**9. What are the worst cases?**
O(n) for both trees: a quadtree whose boxes all straddle the centre, an R-tree
whose envelopes all overlap the query. The bounds are expected, not guaranteed.
Removal is O(n) because there is no id → node map.

## Trees

**10. Why an AVL tree for validity windows?**
It guarantees height ≤ 1.44 log₂(n+2), so insert, delete and search are
O(log n) in the worst case. Augmenting each node with the maximum end time
below it lets a "what was in force at t" query skip whole subtrees.

**11. Show me a rotation.**
Lab → AVL → "Ascending inserts": each step shows the tree, balance factors and
the rotations that operation performed. "Zig-zag" shows double rotations.

**12. When does the interval tree lose?**
When the query is not selective: if hundreds of intervals contain every
instant, reporting them costs as much as scanning. The benchmark shows it
slower than a scan at 1,000 dense intervals. That is why the hot path checks
validity in O(1) per candidate and the tree serves history queries only.

**13. What is a persistent data structure?**
One whose old versions stay valid after updates. Path copying copies only the
root-to-leaf path an update changes and shares everything else. Here: 1,001
versions need 14,025 nodes instead of 158,257 (11.3×), and a query against the
past costs the same as one against the present. Lab → Persistent shows the
copied path in amber.

## Graphs

**14. Dijkstra versus A*?**
Dijkstra settles junctions in order of distance from the source. A* orders by
distance so far plus straight-line distance to the target; because a straight
line is never longer than a road, the heuristic is admissible and A* still
finds a shortest path while settling fewer junctions. In the demo assignment:
772 versus 5,557.

**15. How do you find the nearest responder?**
One Dijkstra from the incident's junction gives road distance to every
responder at once. That is valid because the OSM road graph is undirected; a
directed graph would need the search on the reversed graph. Then A* routes the
chosen responder.

**16. Why a binary heap?**
Dijkstra needs "next nearest" repeatedly: push and pop in O(log n). Lazy
deletion: instead of decreasing a key, push a new entry and skip stale ones on
pop.

**17. Greedy versus Hungarian?**
Greedy takes the cheapest pair first and can strand a later incident. Hungarian
minimises the total in O(n³). Measured: 6.5–15.7 % less total travel, never worse.

**18. Where is union-find used, and why no path compression?**
Group cohesion (members within 150 m are joined; more than one component means
the group split) and alert correlation (alerts within 200 m and 10 minutes form
one incident). Union by rank only, because the structure supports rollback and
path compression would change pointers that cannot be undone.

## Hashing, queues, time

**19. How does your hash table handle collisions and deletions?**
Linear probing; deletion leaves a tombstone so later keys in the probe chain
stay reachable; at 70 % occupancy including tombstones it rebuilds — doubling
only if live entries need it. Lab → Hash table shows the probe path.

**20. What is the timing wheel for?**
Escalation: an unacknowledged operator alert escalates after 2 minutes. Each
deadline sits in a slot; advancing one tick looks at one slot instead of every
alert. Acknowledging cancels the timer.

**21. What is the circular buffer for?**
The last 64 fixes per person, for speed, heading and the stationary and
signal-lost detectors. O(1) push, no allocation.

## History, evidence, offline

**22. How can an examiner see the past?**
Investigation → pick a time. The trail is binary-searched; zones in force come
from the persistent index's version at that time; alert and incident states
from their recorded histories. It shows what the system *knew* at that time —
events synced later from an offline phone are excluded until they arrived.

**23. How do you prove the log was not edited?**
Every log entry is a leaf of a Merkle tree (RFC 6962). An inclusion proof is
O(log n) sibling hashes; anyone with the root can recompute it. The browser
verifies proofs itself with WebCrypto; editing an entry changes the root.

**24. Why not a blockchain?**
The requirement is tamper evidence from one authority, verifiable offline. A
Merkle log gives that with no network, no consensus and about 200 lines.

**25. What happens when a phone loses signal?**
The phone keeps evaluating zones locally from an index downloaded as bytes, and
queues events stamped with a Lamport clock. The server, which has heard
nothing, raises "signal lost" after 2 minutes and does not pretend to know
where the person is. On reconnect the queue is merged once (a retried upload is
rejected event for event), ordered by Lamport stamp, the clock skew is
corrected, and late alerts appear marked LATE.

**26. Why Lamport clocks and not timestamps?**
Phone clocks are wrong — the demo phone is 7 minutes fast. Lamport stamps order
events consistently with cause and effect without trusting any clock.

## Privacy and security

**27. Isn't this surveillance?**
It is designed not to be: tracking only after the person consents and starts a
session; nobody else can switch it on; they can stop any time; history expires
and can be erased; every look at someone's location by someone else is audited
in a tamper-evident log; responders see a person only while assigned to them.

**28. How are passwords stored?**
PBKDF2-HMAC-SHA256 with a random salt and 120,000 iterations, verified against
the RFC test vectors. Sessions are random 256-bit tokens in HttpOnly,
SameSite=Strict cookies; the server stores only their hash.

## Engineering

**29. How do you know your tests are any good?**
Mutation testing: 28 realistic bugs are injected one at a time and a test must
fail for each. All 28 are caught.

**30. Can it handle many users?**
On one core it ingests about 300,000 fixes per second, and the cost per fix is
flat from 10 to 5,000 users. That benchmark also caught a real bug: alert
bookkeeping was quadratic until alerts were indexed per user.

**31. What would you do differently or next?**
Persistence (the server is in memory), real GPS traces, a phone app running the
same offline evaluator, TLS and Argon2, and O(log n) index removal. See
[LIMITATIONS_AND_FUTURE_SCOPE.md](LIMITATIONS_AND_FUTURE_SCOPE.md).

**32. What was the hardest bug?**
The evaluator only observed zones the index returned, so someone who left a
zone's candidate window in one step (a GPS gap) never got an exit and later
re-entries were silent. Fixed by reconciling every zone with open state every
tick — and pinned by a test and a mutant ([../DEFECT_LOG.md](../DEFECT_LOG.md)).
