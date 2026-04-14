#pragma once

// ═══════════════════════════════════════════════════════════════════
//  SWARMING DOTS - emitter_swarmingDots.h
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"
#include "modulators.h"

namespace flowFields {

    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct SwarmingDotsParams {
        uint8_t numDots = 3;
        float swarmSpeed = 0.5f;
        float swarmSpread = 0.5f;
        ModConfig modSwarmSpread = {10, 1.0f, 1.0f};       // modTimer, modRate, modLevel 
        ModConfig modSwarmSpeed  = {11, 1.0f, 0.0f};       // modTimer, modRate, modLevel
        float dotDiam = 1.5f;
        uint8_t numActiveTimers = 12;
    };

    SwarmingDotsParams swarmingDots;

    // Variable number of dots moving in a loose shifting group.
    // Uses an integrated time base for dot motion to preserve continuity
    // when speed modulation changes.
    // swarmSpread controls grouping (0 = clustered, 1 = independent, >1 = wide).
    // Max 5 dots (num_timers=10, 2 timers per dot).
    static void emitSwarmingDots() {
        const uint8_t n = swarmingDots.numDots;
        const float fNumDots = static_cast<float>(n);

        const ModConfig& spreadMod = swarmingDots.modSwarmSpread;
        const ModConfig& speedMod  = swarmingDots.modSwarmSpeed;

        // -----------------------------------------------------------------
        // 1) Plumbing: configure timer channels
        // -----------------------------------------------------------------

        // Parameter-owned modulation timer
        timings.ratio[spreadMod.modTimer] = 0.00055f * spreadMod.modRate;
        timings.ratio[speedMod.modTimer]  = 0.00033f * speedMod.modRate;
        timings.offset[speedMod.modTimer] = 0.0f;

        // Structural per-dot motion timers:
        // 2 timers per dot: [d*2] = X, [d*2+1] = Y
        // Similar ratios keep dots moving at comparable speeds;
        // irrational relationships prevent exact repetition.
        static const float baseRatios[10] = {
            0.00173f, 0.00131f,   // dot 0: X, Y
            0.00197f, 0.00149f,   // dot 1: X, Y
            0.00211f, 0.00113f,   // dot 2: X, Y
            0.00157f, 0.00189f,   // dot 3: X, Y
            0.00223f, 0.00167f    // dot 4: X, Y
        };

        // Offsets: each dot's Y is phase-shifted from its X
        // to create elliptical paths instead of diagonal lines
        static const float baseOffsets[10] = {
            0.0f,  900.0f,    // dot 0
            600.0f, 1700.0f,    // dot 1
            1300.0f, 2400.0f,    // dot 2
            1900.0f, 3100.0f,    // dot 3
            2600.0f, 3800.0f     // dot 4
        };

        calculate_modulators(timings, speedMod.modTimer + 1);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: sample structural motion signals
        // -----------------------------------------------------------------
        
        // Speed modulation signal (centered around 1.0f, no reversals)
        const float speedSignal = move.directional_noise_norm[speedMod.modTimer]; // 0..1
       
        float modSpread = move.directional_noise_norm[spreadMod.modTimer];
       
        // -----------------------------------------------------------------
        // 3) Artistic application: speed and spread modulation; position dots
        // -----------------------------------------------------------------

        // Speed modulation
        // Saturating depth so extreme modLevel doesn't flip direction.
        float depth = speedMod.modLevel / (1.0f + speedMod.modLevel); // 0..1 asymptote
        depth *= 0.9f;  // max ±90% around base speed
        float speedScale = (1.0f - depth) + (2.0f * depth * speedSignal); // [1-depth, 1+depth]
        const float currentSpeed = swarmingDots.swarmSpeed * speedScale;

        // Integrated time base to preserve phase continuity under speed changes
        static float swarmTimeMs = 0.0f;
        const float dtMs = dt * 1000.0f;  // shared dt is already scaled by globalSpeed
        swarmTimeMs += dtMs * currentSpeed;
  
        // Spread modulation adds above the base value
        const float spread =
            swarmingDots.swarmSpread +
            (modSpread * spreadMod.modLevel);

        // Calculate dot position
        float dotX[5], dotY[5];
        float cenX = 0.0f;
        float cenY = 0.0f;

        for (uint8_t d = 0; d < n; d++) {
            const uint8_t xTimer = d * 2;
            const uint8_t yTimer = d * 2 + 1;

            const float phaseX = (swarmTimeMs + baseOffsets[xTimer]) * baseRatios[xTimer];
            const float phaseY = (swarmTimeMs + baseOffsets[yTimer]) * baseRatios[yTimer];
            dotX[d] = fl::sinf(phaseX);
            dotY[d] = fl::sinf(phaseY);

            cenX += dotX[d];
            cenY += dotY[d];
        }

        // Group center
        cenX /= fNumDots;
        cenY /= fNumDots;

        // -----------------------------------------------------------------
        // 4) Rendering
        // -----------------------------------------------------------------

        for (uint8_t d = 0; d < n; d++) {
            const float sx = cenX + spread * (dotX[d] - cenX);
            const float sy = cenY + spread * (dotY[d] - cenY);

            const float cx = (WIDTH  - 1) * 0.5f * (1.0f + sx);
            const float cy = (HEIGHT - 1) * 0.5f * (1.0f + sy);

            const ColorF c = rainbow(t, colorShift, d / fNumDots);
            drawDot(cx, cy, swarmingDots.dotDiam, c.r, c.g, c.b);
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

}