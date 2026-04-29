#pragma once

// ═══════════════════════════════════════════════════════════════════
//  MODULATORS — modulators.h  (type definitions only)
//
//  Instances (timings, move) and calculate_modulators() live in
//  FlowFieldsEngine as class members.  Emitters/flows call:
//      g_engine->calculate_modulators(numActiveTimers);
//  and read/write g_engine->timings / g_engine->move directly.
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"

namespace flowFields {

#define num_timers 20

struct timers {
    float offset[num_timers] = {};  // phase offset per timer (millis)
    float ratio[num_timers]  = {};  // time-sensitivity per timer
};

struct modulators {
    // Base progression
    float linear[num_timers] = {};

    // Sine-family signals
    float radial_phase[num_timers]     = {};  // 0 to 2π
    float normalized_phase[num_timers] = {};  // 0 to 1
    float directional_sine[num_timers] = {};  // −1 to 1
    float normalized_sine[num_timers]  = {};  // 0 to 1

    // Noise-family signals
    float directional_noise[num_timers] = {};  // −1 to 1
    float normalized_noise[num_timers]  = {};  // 0 to 1
    float radial_noise[num_timers]      = {};  // 0 to 2π
};

} // namespace flowFields
