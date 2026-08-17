#pragma once
// Common id and time types.
//
// These live in one place so that fence/, index/, alert/ and track/ can refer to
// each other's entities without circular includes. Strong-ish typedefs only —
// wrapping each in a struct for real type safety is a worthwhile refactor once
// the design settles.
#include <cstdint>

namespace safetrail {

using ZoneId     = uint32_t;
using TouristId  = uint32_t;
using AlertId    = uint32_t;
using IncidentId = uint32_t;
using GroupId    = uint32_t;
using DeviceId   = uint32_t;
using ResponderId= uint32_t;

using Timestamp = int64_t;   // ms since Unix epoch
using Millis    = int64_t;

constexpr Timestamp kForever = INT64_MAX;
constexpr uint32_t  kNoId    = UINT32_MAX;

}  // namespace safetrail
