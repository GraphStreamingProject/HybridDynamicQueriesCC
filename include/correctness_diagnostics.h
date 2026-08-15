#pragma once

#ifdef CORRECTNESS_DIAGNOSTICS

#include "types.h"

// Correctness-only telemetry. This header is intentionally empty in speed and
// profile builds so their hot paths do not gain diagnostic state or branches.
struct TierMaximalityCheck {
    bool insufficient_tiers = false;
    Edge update_induced_on{};
};

#endif  // CORRECTNESS_DIAGNOSTICS
