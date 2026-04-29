#pragma once

// ═══════════════════════════════════════════════════════════════════
//  ROUND SPIRAL FLOW FIELD — flow_spiral.h
// ═══════════════════════════════════════════════════════════════════
//
//  Spiral transport: each pixel samples from a point rotated and
//  radially offset, creating inward or outward spiral motion.
//
//  Ported from Python apply_round_spiral_tail() in _8.py (modes 15/16).
//  outward=false: sample from larger radius → pulls content inward
//  outward=true:  sample from smaller radius → pushes content outward

#include "FlowFieldsEngine.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct SpiralParams {
        float angularStep   = 0.28f;   // rotation per sample (radians)
        float radialStep    = 0.18f;   // radial offset per sample
        float blendFactor   = 0.45f;   // blend: 0 = keep current, 1 = fully transport
        bool  outward       = false;   // false = inward spiral, true = outward spiral

        // Shared UI-facing modulation controls.
        ModConfig modAngularStep   = {0, 0.5f, 0.5f}; // modTimer, modRate, modLevel
        ModConfig modRadialStep = {1, 0.5f, 0.5f};
        ModConfig modBlendFactor = {2, 0.5f, 0.5f};
    };

    SpiralParams spiral;

    // Runtime working values prepared each frame by spiralPrepare()
    // and consumed by spiralAdvect().
    static float workAngularStep = 0.0f;
    static float workRadialStep = 0.0f;
    static float workBlendFactor = 0.0f;

    // --- Prepare: nothing to build (geometry is purely radial/angular) ---

    static void spiralPrepare() {

        // -----------------------------------------------------------------
        // 1) Plumbing: assign modulation channels
        // -----------------------------------------------------------------

        const ModConfig& angularStepMod = spiral.modAngularStep;
        const ModConfig& radialStepMod = spiral.modRadialStep;
        const ModConfig& blendFactorMod = spiral.modBlendFactor;

        const uint8_t angularStepTimer = angularStepMod.modTimer;
        const uint8_t radialStepTimer = radialStepMod.modTimer;
        const uint8_t blendFactorTimer = blendFactorMod.modTimer;

        g_engine->timings.ratio[angularStepTimer]  = 0.0004f * angularStepMod.modRate;
        g_engine->timings.ratio[radialStepTimer]  = 0.00045f * radialStepMod.modRate;
        g_engine->timings.ratio[blendFactorTimer]  = 0.0005f * blendFactorMod.modRate;

        g_engine->calculate_modulators(3);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: centered bipolar control signals [-1, 1]
        // -----------------------------------------------------------------

        const float angularStepSignal = g_engine->move.directional_noise[angularStepTimer];
        const float radialStepSignal = g_engine->move.directional_noise[radialStepTimer];
        const float blendFactorSignal = g_engine->move.directional_noise[blendFactorTimer];

        // -----------------------------------------------------------------
        // 3) Artistic application
        // -----------------------------------------------------------------

        const float angularStepDepth = 0.85f;
        workAngularStep = spiral.angularStep * (1.0f + angularStepMod.modLevel * angularStepDepth * angularStepSignal);
        workAngularStep = fmaxf(0.0f, workAngularStep);

        const float radialStepDepth = 0.85f;
        workRadialStep = spiral.radialStep * (1.0f + radialStepMod.modLevel * radialStepDepth * radialStepSignal);
        workRadialStep = fmaxf(0.0f, workRadialStep);

        const float blendFactorDepth = 0.85f;
        workBlendFactor = spiral.blendFactor * (1.0f + blendFactorMod.modLevel * blendFactorDepth * blendFactorSignal);
        workBlendFactor = fmaxf(0.0f, fminf(1.0f, workBlendFactor));

    }

    // --- Advect: spiral transport with bilinear sampling + fade ---

    static void spiralAdvect() {
        float cx = (g_engine->_width  - 1) * 0.5f;
        float cy = (g_engine->_height - 1) * 0.5f;
        float maxRadius = fl::sqrtf(cx * cx + cy * cy);

        // Frame-rate-independent fade
        float fade = fl::powf(0.5f, g_engine->dt / g_engine->persistence);

        float aStep = workAngularStep;
        float rStep = workRadialStep;
        float frac  = workBlendFactor;
        float inv   = 1.0f - frac;
        bool  out   = spiral.outward;

        // Copy live grid to scratch buffer
        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                g_engine->tR[y][x] = g_engine->gR[y][x];
                g_engine->tG[y][x] = g_engine->gG[y][x];
                g_engine->tB[y][x] = g_engine->gB[y][x];
            }
        }

        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float r  = fl::sqrtf(dx * dx + dy * dy);

                float sr, sg, sb;

                if (r > maxRadius) {
                    // Beyond spiral radius — just fade
                    g_engine->gR[y][x] = g_engine->tR[y][x] * fade;
                    g_engine->gG[y][x] = g_engine->tG[y][x] * fade;
                    g_engine->gB[y][x] = g_engine->tB[y][x] * fade;
                    continue;
                }

                float theta = atan2f(dy, dx);

                // Compute sample radius: inward samples from farther out, outward from closer in
                float sampleR;
                if (out) {
                    sampleR = fmaxf(0.0f, r - rStep);
                } else {
                    sampleR = fminf(maxRadius + 1.5f, r + rStep);
                }

                // Rotate sample angle
                float sampleTheta = theta - aStep;

                float sx = cx + cosf(sampleTheta) * sampleR;
                float sy = cy + sinf(sampleTheta) * sampleR;

                // Clamp to grid bounds
                sx = clampf(sx, 0.0f, (float)(g_engine->_width  - 1) - 1e-6f);
                sy = clampf(sy, 0.0f, (float)(g_engine->_height - 1) - 1e-6f);

                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = min(g_engine->_width  - 1, ix0 + 1);
                int   iy1 = min(g_engine->_height - 1, iy0 + 1);
                float fx  = sx - ix0;
                float fy  = sy - iy0;

                // Bilinear interpolation from scratch buffer
                float rTop = g_engine->tR[iy0][ix0] * (1.0f - fx) + g_engine->tR[iy0][ix1] * fx;
                float rBot = g_engine->tR[iy1][ix0] * (1.0f - fx) + g_engine->tR[iy1][ix1] * fx;
                sr = rTop * (1.0f - fy) + rBot * fy;

                float gTop = g_engine->tG[iy0][ix0] * (1.0f - fx) + g_engine->tG[iy0][ix1] * fx;
                float gBot = g_engine->tG[iy1][ix0] * (1.0f - fx) + g_engine->tG[iy1][ix1] * fx;
                sg = gTop * (1.0f - fy) + gBot * fy;

                float bTop = g_engine->tB[iy0][ix0] * (1.0f - fx) + g_engine->tB[iy0][ix1] * fx;
                float bBot = g_engine->tB[iy1][ix0] * (1.0f - fx) + g_engine->tB[iy1][ix1] * fx;
                sb = bTop * (1.0f - fy) + bBot * fy;

                // Blend current pixel with sampled pixel, then fade
                g_engine->gR[y][x] = (g_engine->tR[y][x] * inv + sr * frac) * fade;
                g_engine->gG[y][x] = (g_engine->tG[y][x] * inv + sg * frac) * fade;
                g_engine->gB[y][x] = (g_engine->tB[y][x] * inv + sb * frac) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
