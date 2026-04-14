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

#include "flowFieldsTypes.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct SpiralParams {
        float angularStep   = 0.28f;   // rotation per sample (radians)
        float radialStep    = 0.18f;   // radial offset per sample
        float blendFactor   = 0.45f;   // blend: 0 = keep current, 1 = fully transport
        bool  outward       = false;   // false = inward spiral, true = outward spiral
   
        // Shared UI-facing modulation controls.
        // Each ModConfig uses modTimer for X and modTimer + 1 for Y.
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

        timings.ratio[angularStepTimer]  = 0.0004f * angularStepMod.modRate;
        //timings.offset[angularStepTimer] = 0.0f;

        timings.ratio[radialStepTimer]  = 0.00045f * radialStepMod.modRate;
        //timings.offset[radialStepTimer] = 0.0f;

        timings.ratio[blendFactorTimer]  = 0.0005f * blendFactorMod.modRate;
        //timings.offset[blendFactorTimer] = 0.0f;

        calculate_modulators(timings, 3);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: centered bipolar control signals [-1, 1]
        // -----------------------------------------------------------------

        const float angularStepSignal = move.directional_noise[angularStepTimer];
        const float radialStepSignal = move.directional_noise[radialStepTimer];
        const float blendFactorSignal = move.directional_noise[blendFactorTimer];

        // -----------------------------------------------------------------
        // 3) Artistic application
        // -----------------------------------------------------------------

        // Amplitude: centered multiplicative modulation around base value.
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
        float cx = (WIDTH  - 1) * 0.5f;
        float cy = (HEIGHT - 1) * 0.5f;
        float maxRadius = fl::sqrtf(cx * cx + cy * cy);

        // Frame-rate-independent fade
        float fade = fl::powf(0.5f, dt / persistence);

        float aStep = workAngularStep;
        float rStep = workRadialStep;
        float frac  = workBlendFactor;
        float inv   = 1.0f - frac;
        bool  out   = spiral.outward;

        // Copy live grid to scratch buffer
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                tR[y][x] = gR[y][x];
                tG[y][x] = gG[y][x];
                tB[y][x] = gB[y][x];
            }
        }

        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float r  = fl::sqrtf(dx * dx + dy * dy);

                float sr, sg, sb;

                if (r > maxRadius) {
                    // Beyond spiral radius — just fade
                    gR[y][x] = tR[y][x] * fade;
                    gG[y][x] = tG[y][x] * fade;
                    gB[y][x] = tB[y][x] * fade;
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
                sx = clampf(sx, 0.0f, (float)(WIDTH  - 1) - 1e-6f);
                sy = clampf(sy, 0.0f, (float)(HEIGHT - 1) - 1e-6f);

                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = min(WIDTH  - 1, ix0 + 1);
                int   iy1 = min(HEIGHT - 1, iy0 + 1);
                float fx  = sx - ix0;
                float fy  = sy - iy0;

                // Bilinear interpolation from scratch buffer
                float rTop = tR[iy0][ix0] * (1.0f - fx) + tR[iy0][ix1] * fx;
                float rBot = tR[iy1][ix0] * (1.0f - fx) + tR[iy1][ix1] * fx;
                sr = rTop * (1.0f - fy) + rBot * fy;

                float gTop = tG[iy0][ix0] * (1.0f - fx) + tG[iy0][ix1] * fx;
                float gBot = tG[iy1][ix0] * (1.0f - fx) + tG[iy1][ix1] * fx;
                sg = gTop * (1.0f - fy) + gBot * fy;

                float bTop = tB[iy0][ix0] * (1.0f - fx) + tB[iy0][ix1] * fx;
                float bBot = tB[iy1][ix0] * (1.0f - fx) + tB[iy1][ix1] * fx;
                sb = bTop * (1.0f - fy) + bBot * fy;

                // Blend current pixel with sampled pixel, then fade
                gR[y][x] = (tR[y][x] * inv + sr * frac) * fade;
                gG[y][x] = (tG[y][x] * inv + sg * frac) * fade;
                gB[y][x] = (tB[y][x] * inv + sb * frac) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
