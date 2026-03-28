#pragma once

// ═══════════════════════════════════════════════════════════════════
//  MODULATORS — modulators.h
// ═══════════════════════════════════════════════════════════════════

#include "colorTrailsTypes.h"

namespace colorTrails {

    // ═══════════════════════════════════════════════════════════════════
    //  TIMER / MODULATOR ENGINE
    // ═══════════════════════════════════════════════════════════════════

    #define num_timers 12

    struct timers {
        float offset[num_timers];  // timers can be separated by a time offset
        float ratio[num_timers];   // ratio determines time-sensitivity
    };

    /*struct modulators {
        float linear[num_timers];                   // returns 0 to FLT_MAX
        float radial_sine[num_timers];              // returns 0 to 2*PI
        float radial_sine_norm[num_timers];         // returns 0 to 1
        float directional_sine[num_timers];         // returns -1 to 1
        float directional_noise[num_timers];        // returns -1 to 1
        float directional_noise_norm[num_timers];   // returns 0 to 1
        float radial_noise[num_timers];             // returns 0 to 2*PI
        float radial_noise_norm[num_timers];        // returns 0 to 1
    };*/

    struct modulators {
        // Base progression
        float linear[num_timers];

        // Sine-family signals
        float radial_sine[num_timers];             // angle, 0 to 2PI
        float radial_sine_norm[num_timers];        // normalized angle, 0 to 1
        float directional_sine[num_timers];        // centered sine, -1 to 1

        // Noise-family signals
        float directional_noise[num_timers];       // centered noise, -1 to 1
        float directional_noise_norm[num_timers];  // normalized noise, 0 to 1
        float radial_noise[num_timers];            // noise mapped to angle, 0 to 2PI
        float radial_noise_norm[num_timers];       // normalized angle-like noise, 0 to 1
    };

    timers timings;     // timer inputs; all time/speed settings in one place
    modulators move;    // timer outputs; all time-based modulators in one place


    void calculate_modulators(timers &timings) {
        const float runtime = fl::millis();

        for (uint8_t i = 0; i < num_timers; i++) {
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
    
    /*
    void calculate_modulators(timers &timings) {

            float runtime = fl::millis();

            for (uint8_t i = 0; i < num_timers; i++) {

                // continuously rising linear mod, returns 0 to max_float
                move.linear[i] =
                    (runtime + timings.offset[i]) * timings.ratio[i];

                // angle mod, continous rotation, returns 0 to 2 * PI
                move.radial_sine[i] =
                    fl::fmodf(move.linear[i], CT_2PI);

                move.radial_sine_norm[i] = fl::map_range_clamped<float, float>(move.radial_sine[i], 0.f, CT_2PI, 0.0f, 1.0f);

                // directional offsets or factors, returns -1 to 1
                move.directional_sine[i] =
                    fl::sinf(move.radial_sine[i]);

                // noise-based directional, offsets or factors, returns -1 to 1
                move.directional_noise[i] =
                    2.f * noiseX.noise(move.linear[i]);

                // noise based angle offset, returns 0 to 2 * PI
                move.radial_noise[i] =
                    CT_PI * (1.f + move.directional_noise[i]); // noiseX.noise(move.linear[i])
                
                move.radial_noise_norm[i] = fl::map_range_clamped<float, float>(move.radial_noise[i], 0.f, CT_2PI, 0.0f, 1.0f);

            }
        }*/

    // ═══════════════════════════════════════════════════════════════════
    //  MODCONFIG APPLICATOR
    // ═══════════════════════════════════════════════════════════════════

    /*class Modulators {
    public:
        // Read the raw modulator output for this config's modType + modTimer (native range)
        static float getModValue(const ModConfig& m) {
            switch (m.modType) {
                case MOD_LINEAR:            return move.linear[m.modTimer];
                case MOD_RADIAL_SINE:       return move.radial_sine[m.modTimer];
                case MOD_RADIAL_SINE_NORM:  return move.radial_sine_norm[m.modTimer];
                case MOD_DIRECTIONAL_SINE:  return move.directional_sine[m.modTimer];
                case MOD_DIRECTIONAL_NOISE: return move.directional_noise[m.modTimer];
                case MOD_RADIAL_NOISE:      return move.radial_noise[m.modTimer];
                case MOD_RADIAL_NOISE_NORM: return move.radial_noise_norm[m.modTimer];
                default:                    return 0.0f;
            }
        }
    };*/

} // namespace colorTrails
