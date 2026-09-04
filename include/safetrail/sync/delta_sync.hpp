#pragma once
// safetrail :: Delta synchronisation
//
// SUPERSEDED. The offline story (GAP 6) is `sync/lamport.hpp`: an append-only
// device queue, Lamport-ordered reconciliation on the server, and idempotent
// merge. A delta-sync layer on top -- shipping only what changed since a
// watermark -- was on an early roadmap and was dropped: the payloads here are
// events, which are already deltas, so it would have been a compression scheme
// with nothing to compress.
//
// This header survives so that references to "delta_sync.hpp" point somewhere
// honest. Include `sync/lamport.hpp`.

namespace safetrail::sync {

// Intentionally empty. See the note above.

}  // namespace safetrail::sync
