#pragma once

// ═══════════════════════════════════════════════════════════════════
//  LISSAJOUS LINE - emitter_lissajousLine.h
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"
#include "modulators.h"

namespace flowFields {

    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct LissajousParams {
        float lineSpeed = 0.35f;
        float lineAmp = (MIN_DIMENSION - 4) * 0.75f;
        ModConfig modLineSpeed = {0, 1.0f, 0.0f}; // modTimer, modRate, modLevel
    };

    LissajousParams lissajous;

    static void emitLissajousLine() {
        const float cx = (WIDTH  - 1) * 0.5f;
        const float cy = (HEIGHT - 1) * 0.5f;
        const float amp = lissajous.lineAmp;

        // Integrate speed to preserve continuity when lineSpeed changes.
        static float phase = 0.0f;

        const ModConfig& speedMod = lissajous.modLineSpeed;
        
        // -----------------------------------------------------------------
        // 1) Plumbing: configure timer channels
        // -----------------------------------------------------------------
        
        timings.ratio[speedMod.modTimer]  = 0.00045f * speedMod.modRate;
        calculate_modulators(timings, speedMod.modTimer + 1);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: get normalized modulation signals
        //    directional_noise is centered bipolar noise in roughly [-1, 1]
        // -----------------------------------------------------------------

        const float speedSignal = move.directional_noise[speedMod.modTimer];
        const float currentSpeed =
            lissajous.lineSpeed * (1.0f + speedMod.modLevel * 0.85f * speedSignal);

        // -----------------------------------------------------------------
        // 3) Artistic application: decide what those signals mean
        // -----------------------------------------------------------------

        phase += currentSpeed * dt;

        float lx1 = cx + (amp + 1.5f) * fl::sinf(phase * 1.13f + 0.20f);
        float ly1 = cy + (amp + 0.5f) * fl::sinf(phase * 1.71f + 1.30f);
        float lx2 = cx + (amp + 2.0f) * fl::sinf(phase * 1.89f + 2.20f);
        float ly2 = cy + (amp + 1.0f) * fl::sinf(phase * 1.37f + 0.70f);

        // -----------------------------------------------------------------
        // 4) Rendering
        // -----------------------------------------------------------------

        drawAASubpixelLine(lx1, ly1, lx2, ly2, t, colorShift);
        ColorF ca = rainbow(t, colorShift, 0.0f);
        ColorF cb = rainbow(t, colorShift, 1.0f);
        drawAAEndpointDisc(lx1, ly1, ca.r, ca.g, ca.b, 0.85f);
        drawAAEndpointDisc(lx2, ly2, cb.r, cb.g, cb.b, 0.85f);
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END
    
}