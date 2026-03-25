#pragma once

// ═══════════════════════════════════════════════════════════════════
//  MODULATORS — modulators.h
// ═══════════════════════════════════════════════════════════════════

#include "colorTrailsTypes.h"

namespace colorTrails {

    // ═══════════════════════════════════════════════════════════════════
    //  TIMER / MODULATOR ENGINE
    // ═══════════════════════════════════════════════════════════════════

    #define num_timers 10

    struct timers {
        float offset[num_timers];  // timers can be separated by a time offset
        float ratio[num_timers];   // ratio determines time-sensitivity
    };

    struct modulators {
        float linear[num_timers];               // returns 0 to FLT_MAX
        float radial[num_timers];               // returns 0 to 2*PI
        float directional_sine[num_timers];     // returns -1 to 1
        float directional_noise[num_timers];    // returns -1 to 1
        float radial_noise[num_timers];          // returns 0 to 2*PI
    };

    timers timings;     // timer inputs; all time/speed settings in one place
    modulators move;    // timer outputs; all time-based modulators in one place


    void calculate_modulators(timers &timings) {

            float runtime = fl::millis();

            for (uint8_t i = 0; i < num_timers; i++) {

                // continuously rising linear mod, returns 0 to max_float
                move.linear[i] =
                    (runtime + timings.offset[i]) * timings.ratio[i];

                // angle mod, continous rotation, returns 0 to 2 * PI
                move.radial[i] =
                    fl::fmodf(move.linear[i], 2 * PI);

                // directional offsets or factors, returns -1 to 1
                move.directional_sine[i] =
                    fl::sinf(move.radial[i]);

                // noise-based directional, offsets or factors, returns -1 to 1
                move.directional_noise[i] =
                    noiseX.noise(move.linear[i]);

                // noise based angle offset, returns 0 to 2 * PI
                move.radial_noise[i] =
                    PI * (1.f + move.directional_noise[i]); // noiseX.noise(move.linear[i])

            }
        }

    // ═══════════════════════════════════════════════════════════════════
    //  MODCONFIG APPLICATOR
    // ═══════════════════════════════════════════════════════════════════

    class Modulators {
    public:
        // Configure a timer from a ModConfig (writes rate + offset into timings)
        static void configureTimer(const ModConfig& m) {
            timings.ratio[m.timer]  = m.rate;
            timings.offset[m.timer] = m.offset;
        }

        // Apply modulation to a base value using the selected waveform and operation
        static float apply(float base, const ModConfig& m) {
            if (m.type == MOD_NONE || m.level == 0.0f) return base;
            float wave = getModValue(m);
            switch (m.op) {
                case OP_SCALE: return base * (1.0f + m.level * wave);
                case OP_ADD:   return base + m.level * wave;
                default:       return base;
            }
        }

    private:
        // Read the raw modulator output for this config's type + timer (native range)
        static float getModValue(const ModConfig& m) {
            switch (m.type) {
                case MOD_LINEAR:            return move.linear[m.timer];
                case MOD_RADIAL:            return move.radial[m.timer];
                case MOD_DIRECTIONAL_SINE:  return move.directional_sine[m.timer];
                case MOD_DIRECTIONAL_NOISE: return move.directional_noise[m.timer];
                case MOD_RADIAL_NOISE:      return move.radial_noise[m.timer];
                default:                    return 0.0f;
            }
        }
    };

} // namespace colorTrails
