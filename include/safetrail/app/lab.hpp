#pragma once
//
// The Data Structures Lab: each experiment builds the REAL structure from this
// repository, runs the operations the caller asked for, and returns what the
// structure actually did -- its nodes, the nodes a query visited, the rotations
// an insert caused, the slots a probe sequence walked, the heap as stored.
//
// Nothing here re-implements a structure for display. Where an experiment
// needs a second answer to compare against (brute force for the indexes, a
// linear scan for the k-d tree, the published root for the Merkle tree drawn
// from leaf hashes), the comparison is part of the response, so a viewer can
// see the two agree -- or see a mismatch if one ever appeared.
//
// Inputs are bounded (point counts, grid sizes, operation counts) so a lab
// request can never be used to make the server do unbounded work.
//
#include <string>
#include <vector>
#include "safetrail/util/json.hpp"

namespace safetrail::app {

std::vector<std::string> lab_experiments();

// Runs experiment `name` with parameters `req`. Returns a JSON document; on bad
// input sets *status to 400 (404 for an unknown experiment) and returns
// {"error": "..."}.
std::string run_lab(const std::string& name, const util::Json& req, int* status);

}  // namespace safetrail::app
