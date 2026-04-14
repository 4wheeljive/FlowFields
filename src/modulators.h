#pragma once

// ═══════════════════════════════════════════════════════════════════
//  MODULATORS — modulators.h
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"

namespace flowFields {

    // ═══════════════════════════════════════════════════════════════════
    //  TIMER / MODULATOR ENGINE
    // ═══════════════════════════════════════════════════════════════════

    #define num_timers 20

    struct timers {
        float offset[num_timers];  // timers can be separated by a time offset
        float ratio[num_timers];   // ratio determines time-sensitivity
    };

    struct modulators {
        // Base progression
        float linear[num_timers];

        // Sine-family signals
        float radial_sine[num_timers];             // angle, 0 to 2PI
        float radial_sine_norm[num_timers];        // normalized angle, 0 to 1
        float directional_sine[num_timers];        // centered sine, -1 to 1
        float directional_sine_norm[num_timers];   // normalized sine, 0 to 1

        // Noise-family signals
        float directional_noise[num_timers];       // centered noise, -1 to 1
        float directional_noise_norm[num_timers];  // normalized noise, 0 to 1
        float radial_noise[num_timers];            // noise mapped to angle, 0 to 2PI
        float radial_noise_norm[num_timers];       // normalized angle-like noise, 0 to 1
    };

    timers timings;     // timer inputs; all time/speed settings in one place
    modulators move;    // timer outputs; all time-based modulators in one place

    void calculate_modulators(timers &timings, uint8_t numActiveTimers) {
        // Virtual millis accumulator — scales with globalSpeed to avoid
        // discontinuities when globalSpeed changes mid-run.
        static unsigned long lastRealMs = 0;
        static float virtualMs = 0.0f;
        const unsigned long realMs = fl::millis();
        if (lastRealMs == 0) lastRealMs = realMs;
        virtualMs += (realMs - lastRealMs) * globalSpeed;
        lastRealMs = realMs;
        const float runtime = virtualMs;

        for (uint8_t i = 0; i < numActiveTimers; i++) {
            // -----------------------------------------------------------------
            // Base time progression
            // -----------------------------------------------------------------
            move.linear[i] = (runtime + timings.offset[i]) * timings.ratio[i];

            // -----------------------------------------------------------------
            // Sine-family signals
            // -----------------------------------------------------------------
            move.radial_sine[i] = fl::fmodf(move.linear[i], CT_2PI);
            move.radial_sine_norm[i] =
                fl::map_range_clamped(move.radial_sine[i], 0.f, CT_2PI, 0.0f, 1.0f);

            move.directional_sine[i] = fl::sinf(move.radial_sine[i]);
            move.directional_sine_norm[i] = 0.5f + 0.5f * move.directional_sine[i];

            // -----------------------------------------------------------------
            // Noise-family signals
            // Perlin1D raw output is roughly [-0.5, 0.5]
            // directional_noise normalizes that to roughly [-1, 1]
            // -----------------------------------------------------------------
            const float rawNoise = noiseX.noise(move.linear[i]);

            move.directional_noise[i] = 2.0f * rawNoise;
            move.directional_noise_norm[i] = 0.5f + 0.5f * move.directional_noise[i];

            move.radial_noise[i] = CT_PI * (1.0f + move.directional_noise[i]);
            move.radial_noise_norm[i] =
                fl::map_range_clamped(move.radial_noise[i], 0.f, CT_2PI, 0.0f, 1.0f);
        }
    }
    
} // namespace flowFields
