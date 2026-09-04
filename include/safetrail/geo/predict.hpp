#pragma once
// safetrail :: Trajectory prediction
//
// NOT A SEPARATE MODULE. Predictive crossing (GAP 2) is ~25 lines living where it
// is used: `Tourist::project()` in `track/tourist.cpp` extrapolates position from
// the smoothed speed and heading, and step 8 of `fence::Evaluator::evaluate()`
// projects forward and re-tests containment.
//
// It stayed there deliberately. The prediction is only meaningful in the context
// of the candidate zone, the prediction horizon and the hysteresis state that
// surround it; lifting it into a standalone "predictor" would separate a
// four-line calculation from every input that makes it correct, and would invite
// the calculation to be used without them.
//
// This header survives so that references to "predict.hpp" point somewhere honest.

namespace safetrail::geo {

// Intentionally empty. See the note above.

}  // namespace safetrail::geo
