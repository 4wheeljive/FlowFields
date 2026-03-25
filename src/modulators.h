#pragma once

// ═══════════════════════════════════════════════════════════════════
//  EMITTERS — emitters.h
// ═══════════════════════════════════════════════════════════════════

#include "colorTrailsTypes.h"

namespace colorTrails {


    // ═══════════════════════════════════════════════════════════════════
    //  MODULATORS
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


    class Modulators {


        float applyMod(float base, uint8_t op, uint8_t type, float rate, float level, uint8_t index);



    
    
    
    };  // Modulators

} // namespace colorTrails