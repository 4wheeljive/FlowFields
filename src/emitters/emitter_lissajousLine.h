#pragma once

// ═══════════════════════════════════════════════════════════════════
//  LISSAJOUS LINE - emitter_lissajousLine.h
// ═══════════════════════════════════════════════════════════════════

#include "FlowFieldsEngine.h"

namespace flowFields {

    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct LissajousParams {
        float lineSpeed = 0.35f;
        float lineAmp = 13.5f;   // ~(minDim - 4) * 0.75 for a 22-px grid; overridden by syncFromCVars()
        uint8_t lineClamp = 0;  // 0 = free (wrap), 1 = clamp to grid, 2 = tether nearest to center
        ModConfig modLineSpeed = {0, 1.0f, 0.0f}; // modTimer, modRate, modLevel
        ModConfig modLineAmp   = {1, 0.5f, 0.0f};
    };

    LissajousParams lissajous;

    static void emitLissajousLine() {
        const float cx = (g_engine->_width  - 1) * 0.5f;
        const float cy = (g_engine->_height - 1) * 0.5f;
        const float amp = lissajous.lineAmp;

        // Integrate speed to preserve continuity when lineSpeed changes.
        static float phase = 0.0f;

        const ModConfig& speedMod = lissajous.modLineSpeed;
        const ModConfig& ampMod  = lissajous.modLineAmp;

        // -----------------------------------------------------------------
        // 1) Plumbing: configure timer channels
        // -----------------------------------------------------------------

        g_engine->timings.ratio[speedMod.modTimer] = 0.00045f * speedMod.modRate;
        g_engine->timings.ratio[ampMod.modTimer]   = 0.0003f  * ampMod.modRate;
        g_engine->calculate_modulators(2);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: get normalized modulation signals
        //    directional_noise is centered bipolar noise in roughly [-1, 1]
        // -----------------------------------------------------------------

        const float speedSignal = g_engine->move.directional_noise[speedMod.modTimer];
        const float currentSpeed =
            lissajous.lineSpeed * (1.0f + speedMod.modLevel * 0.85f * speedSignal);

        const float ampSignal = g_engine->move.directional_noise[ampMod.modTimer];

        // -----------------------------------------------------------------
        // 3) Artistic application: decide what those signals mean
        // -----------------------------------------------------------------

        phase += currentSpeed * g_engine->dt;

        // Amplitude: exponential modulation for perceptually balanced ×4/÷4 range.
        // fastpow(4, signal) maps: -1→0.25, 0→1.0, +1→4.0
        const float ampFactor = fastpow(4.0f, ampMod.modLevel * ampSignal);
        const float workAmp = amp * ampFactor;

        float lx1 = cx + (workAmp + 1.5f) * fl::sinf(phase * 1.13f + 0.20f);
        float ly1 = cy + (workAmp + 0.5f) * fl::sinf(phase * 1.71f + 1.30f);
        float lx2 = cx + (workAmp + 2.0f) * fl::sinf(phase * 1.89f + 2.20f);
        float ly2 = cy + (workAmp + 1.0f) * fl::sinf(phase * 1.37f + 0.70f);

        // -----------------------------------------------------------------
        // 4) Line clamping
        // -----------------------------------------------------------------

        if (lissajous.lineClamp == 1) {
            // Clamp: pin endpoints to grid edges
            lx1 = clampf(lx1, 0.0f, (float)(g_engine->_width  - 1));
            ly1 = clampf(ly1, 0.0f, (float)(g_engine->_height - 1));
            lx2 = clampf(lx2, 0.0f, (float)(g_engine->_width  - 1));
            ly2 = clampf(ly2, 0.0f, (float)(g_engine->_height - 1));
        } else if (lissajous.lineClamp == 2) {
            // Tether: keep the nearer endpoint within half-grid of center
            float dx1 = lx1 - cx;
            float dx2 = lx2 - cx;
            float dy1 = ly1 - cy;
            float dy2 = ly2 - cy;

            float nearDx = (fabsf(dx1) < fabsf(dx2)) ? dx1 : dx2;
            float nearDy = (fabsf(dy1) < fabsf(dy2)) ? dy1 : dy2;

            float maxDx = (float)g_engine->_width  * 0.5f;
            float maxDy = (float)g_engine->_height * 0.5f;

            float xOver = fmaxf(0.0f, fabsf(nearDx) - maxDx);
            float yOver = fmaxf(0.0f, fabsf(nearDy) - maxDy);

            float xCorrect = (nearDx > 0) ? -xOver : xOver;
            float yCorrect = (nearDy > 0) ? -yOver : yOver;

            lx1 += xCorrect;  lx2 += xCorrect;
            ly1 += yCorrect;  ly2 += yCorrect;
        }

        // -----------------------------------------------------------------
        // 5) Rendering
        // -----------------------------------------------------------------

        // drawAASubpixelLine uses this->t and this->colorShift directly
        g_engine->drawAASubpixelLine(lx1, ly1, lx2, ly2);
        ColorF ca = g_engine->rainbow(g_engine->t, g_engine->colorShift, 0.0f);
        ColorF cb = g_engine->rainbow(g_engine->t, g_engine->colorShift, 1.0f);
        g_engine->drawAAEndpointDisc(lx1, ly1, ca.r, ca.g, ca.b, 0.85f);
        g_engine->drawAAEndpointDisc(lx2, ly2, cb.r, cb.g, cb.b, 0.85f);
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

}
