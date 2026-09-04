#pragma once
// safetrail :: HTTP API
//
// NOT IMPLEMENTED, AND NOT PLANNED. This project has no server.
//
// The engine is a C++ program that runs a scenario and writes ONE self-contained
// HTML replay; the "deployment" is that static file (see docs/ARCHITECTURE.md and
// the README's "what this is - and isn't" note). A live HTTP API was on an early
// roadmap and was dropped, because a network service is not what a data-structures
// course project is graded on and shipping a half-built one would misrepresent the
// architecture.
//
// The header survives so that a stale reference fails here, loudly and with an
// explanation, rather than mysteriously somewhere else.

namespace safetrail::server {

// Intentionally empty. See the note above.

}  // namespace safetrail::server
