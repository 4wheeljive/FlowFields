#pragma once

// ═══════════════════════════════════════════════════════════════════
//  NOISE FLOW FIELD — flow_noise.h
// ═══════════════════════════════════════════════════════════════════
//
//  Self-contained noise flow field implementation.
//  Includes colorTrailsTypes.h for shared types and instances.
//  cVar bridge helpers (pushFlowDefaultsToCVars / syncFlowFromCVars)
//  live in colorTrails_detail.hpp since they depend on bleControl.h.

#include "colorTrailsTypes.h"
#include "modulators.h"

namespace colorTrails {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct NoiseFlowParams {
        float xSpeed = 0.15f;   // Noise scroll speed  (column axis)
        float ySpeed = 0.15f;   // Noise scroll speed  (row axis)
        float xAmp = 1.00f;   // Noise amplitude     (column axis)
        float yAmp = 1.00f;   // Noise amplitude     (row axis)
        float xFreq = 0.33f;   // Noise spatial scale (column axis) (aka "xScale")
        float yFreq = 0.32f;   // Noise spatial scale (row axis) (aka "yScale")
        float xShift = 1.8f;    // Max horizontal shift per row  (pixels)
        float yShift = 1.8f;    // Max vertical shift per column (pixels)
        float noiseFreq = 0.23f;  // effectively a multiplier of xFreq and yFreq
        uint8_t numActiveTimers = 2;

        // Shared UI-facing modulation controls.
        // Each ModConfig uses modTimer for X and modTimer + 1 for Y.
        ModConfig modAmp   = {0, 1.0f, 1.0f}; // modTimer, modRate, modLevel  
        ModConfig modSpeed = {2, 0.1f, 0.2f};
        ModConfig modShift = {4, 0.5f, 1.1f};
    };
   
    NoiseFlowParams noiseFlow;

    // Runtime working values prepared each frame by noiseFlowPrepare()
    // and consumed by noiseFlowAdvect().
    static float workXShiftCurrent = 0.0f;
    static float workYShiftCurrent = 0.0f;

    static void sampleProfile2D(const Perlin2D &n, float t, float speed,
                            float amp, float scale, int count, float *out) {
        const float scrollY = t * speed;

        for (int i = 0; i < count; i++) {
            const float v = n.noise(i * noiseFlow.noiseFreq * scale, scrollY);
            out[i] = clampf(v * amp, -1.0f, 1.0f);
        }
    }

    // --- Prepare: build noise profiles, apply modulator(s) ---
    static void noiseFlowPrepare(float t) {
        // -----------------------------------------------------------------
        // 1) Plumbing: assign paired modulation channels
        // -----------------------------------------------------------------
        const ModConfig& ampMod   = noiseFlow.modAmp;
        const ModConfig& speedMod = noiseFlow.modSpeed;
        const ModConfig& shiftMod = noiseFlow.modShift;

        const uint8_t xAmpTimer   = ampMod.modTimer;
        const uint8_t yAmpTimer   = ampMod.modTimer + 1;

        const uint8_t xSpeedTimer = speedMod.modTimer;
        const uint8_t ySpeedTimer = speedMod.modTimer + 1;

        const uint8_t xShiftTimer = shiftMod.modTimer;
        const uint8_t yShiftTimer = shiftMod.modTimer + 1;

        // Amplitude: medium breathing
        timings.ratio[xAmpTimer]  = 0.00043f * ampMod.modRate;
        timings.offset[xAmpTimer] = 0.0f;
        timings.ratio[yAmpTimer]  = 0.00049f * ampMod.modRate;
        timings.offset[yAmpTimer] = 1700.0f;

        // Speed: slightly slower, allows directional reversal around base
        timings.ratio[xSpeedTimer]  = 0.00027f * speedMod.modRate;
        timings.offset[xSpeedTimer] = 0.0f;
        timings.ratio[ySpeedTimer]  = 0.00031f * speedMod.modRate;
        timings.offset[ySpeedTimer] = 2100.0f;

        // Shift: slower structural breathing
        timings.ratio[xShiftTimer]  = 0.00018f * shiftMod.modRate;
        timings.offset[xShiftTimer] = 0.0f;
        timings.ratio[yShiftTimer]  = 0.00022f * shiftMod.modRate;
        timings.offset[yShiftTimer] = 3200.0f;

        calculate_modulators(timings, 6);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: centered bipolar control signals [-1, 1]
        // -----------------------------------------------------------------
        const float xAmpSignal   = move.directional_noise[xAmpTimer];
        const float yAmpSignal   = move.directional_noise[yAmpTimer];

        const float xSpeedSignal = move.directional_noise[xSpeedTimer];
        const float ySpeedSignal = move.directional_noise[ySpeedTimer];

        const float xShiftSignal = move.directional_noise[xShiftTimer];
        const float yShiftSignal = move.directional_noise[yShiftTimer];

        // -----------------------------------------------------------------
        // 3) Artistic application
        // -----------------------------------------------------------------

        // Amplitude: centered multiplicative breathing around base value.
        const float ampDepth = 0.85f;
        float workXAmp = noiseFlow.xAmp * (1.0f + ampMod.modLevel * ampDepth * xAmpSignal);
        float workYAmp = noiseFlow.yAmp * (1.0f + ampMod.modLevel * ampDepth * yAmpSignal);
        workXAmp = fmaxf(0.0f, workXAmp);
        workYAmp = fmaxf(0.0f, workYAmp);

        // Speed: centered multiplicative breathing around base value.
        // Leave unclamped so low base values can reverse direction.
        const float speedDepth = 0.90f;
        float workXSpeed = noiseFlow.xSpeed * (1.0f + speedMod.modLevel * speedDepth * xSpeedSignal);
        float workYSpeed = noiseFlow.ySpeed * (1.0f + speedMod.modLevel * speedDepth * ySpeedSignal);

        // Shift: centered multiplicative breathing around base value.
        // Clamp nonnegative because "max shift" is generally magnitude-like.
        const float shiftDepth = 0.75f;
        float workXShift = noiseFlow.xShift * (1.0f + shiftMod.modLevel * shiftDepth * xShiftSignal);
        float workYShift = noiseFlow.yShift * (1.0f + shiftMod.modLevel * shiftDepth * yShiftSignal);
        workXShift = fmaxf(0.0f, workXShift);
        workYShift = fmaxf(0.0f, workYShift);
        
        // Publish working shift values for the advection pass
        workXShiftCurrent = workXShift;
        workYShiftCurrent = workYShift;

        sampleProfile2D(noise2X, t, workXSpeed, workXAmp,
                        noiseFlow.xFreq, WIDTH, xProf);

        sampleProfile2D(noise2Y, t, workYSpeed, workYAmp,
                        noiseFlow.yFreq, HEIGHT, yProf);

    }

    // --- Advect: two-pass fractional advection (bilinear interpolation) + fade ---

    static void noiseFlowAdvect(float dt) {
        // Frame-rate-independent fade: half-life = persistence seconds
        float fade = fl::powf(0.5f, dt / vizConfig.persistence);

        // Pass 1 — horizontal row shift  (Y-noise drives X movement)
        for (int y = 0; y < HEIGHT; y++) {
            float sh = yProf[y] * workXShiftCurrent;
            for (int x = 0; x < WIDTH; x++) {
                float sx  = fmodPos((float)x - sh, (float)WIDTH);
                int   ix0 = (int)fl::floorf(sx) % WIDTH;
                int   ix1 = (ix0 + 1) % WIDTH;
                float f   = sx - fl::floorf(sx);
                float inv = 1.0f - f;
                tR[y][x] = gR[y][ix0] * inv + gR[y][ix1] * f;
                tG[y][x] = gG[y][ix0] * inv + gG[y][ix1] * f;
                tB[y][x] = gB[y][ix0] * inv + gB[y][ix1] * f;
            }
        }

        // Pass 2 — vertical column shift  (X-noise drives Y movement) + dim
        for (int x = 0; x < WIDTH; x++) {
            float sh = xProf[x] * workYShiftCurrent;
            for (int y = 0; y < HEIGHT; y++) {
                float sy  = fmodPos((float)y - sh, (float)HEIGHT);
                int   iy0 = (int)fl::floorf(sy) % HEIGHT;
                int   iy1 = (iy0 + 1) % HEIGHT;
                float f   = sy - fl::floorf(sy);
                float inv = 1.0f - f;
                // Keep full float precision; quantize only at LED output.
                gR[y][x] = (tR[iy0][x] * inv + tR[iy1][x] * f) * fade;
                gG[y][x] = (tG[iy0][x] * inv + tG[iy1][x] * f) * fade;
                gB[y][x] = (tB[iy0][x] * inv + tB[iy1][x] * f) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace colorTrails
