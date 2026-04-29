#pragma once

// ═══════════════════════════════════════════════════════════════════
//  NOISE FLOW FIELD — flow_noise.h
// ═══════════════════════════════════════════════════════════════════

#include "FlowFieldsEngine.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct NoiseFlowParams {
        float xSpeed = 0.15f;   // Noise scroll speed  (column axis)
        float ySpeed = 0.15f;   // Noise scroll speed  (row axis)
        float xAmp = 1.00f;   // Noise amplitude     (column axis)
        float yAmp = 1.00f;   // Noise amplitude     (row axis)
        float xFreq = 0.33f;   // Noise spatial scale (column axis) (aka "xScale")
        float yFreq = 0.32f;   // Noise spatial scale (row axis) (aka "yScale")
        float xShift = 1.5f;    // Max horizontal shift per row  (pixels)
        float yShift = 1.5f;    // Max vertical shift per column (pixels)
        float noiseFreq = 0.2f;  // effectively a multiplier of xFreq and yFreq
        uint8_t numActiveTimers = 2;

        // Shared UI-facing modulation controls.
        // Each ModConfig uses modTimer for X and modTimer + 1 for Y.
        ModConfig modAmp   = {0, 0.5f, 0.5f}; // modTimer, modRate, modLevel
        ModConfig modSpeed = {2, 0.1f, 0.1f};
        ModConfig modShift = {4, 0.5f, 0.5f};
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
    static void noiseFlowPrepare() {
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
        g_engine->timings.ratio[xAmpTimer]  = 0.00043f * ampMod.modRate;
        g_engine->timings.offset[xAmpTimer] = 0.0f;
        g_engine->timings.ratio[yAmpTimer]  = 0.00049f * ampMod.modRate;
        g_engine->timings.offset[yAmpTimer] = 1700.0f;

        // Speed: slightly slower, allows directional reversal around base
        g_engine->timings.ratio[xSpeedTimer]  = 0.00027f * speedMod.modRate;
        g_engine->timings.offset[xSpeedTimer] = 0.0f;
        g_engine->timings.ratio[ySpeedTimer]  = 0.00031f * speedMod.modRate;
        g_engine->timings.offset[ySpeedTimer] = 2100.0f;

        // Shift: slower structural breathing
        g_engine->timings.ratio[xShiftTimer]  = 0.00018f * shiftMod.modRate;
        g_engine->timings.offset[xShiftTimer] = 0.0f;
        g_engine->timings.ratio[yShiftTimer]  = 0.00022f * shiftMod.modRate;
        g_engine->timings.offset[yShiftTimer] = 3200.0f;

        g_engine->calculate_modulators(6);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: centered bipolar control signals [-1, 1]
        // -----------------------------------------------------------------
        const float xAmpSignal   = g_engine->move.directional_noise[xAmpTimer];
        const float yAmpSignal   = g_engine->move.directional_noise[yAmpTimer];

        const float xSpeedSignal = g_engine->move.directional_noise[xSpeedTimer];
        const float ySpeedSignal = g_engine->move.directional_noise[ySpeedTimer];

        const float xShiftSignal = g_engine->move.directional_noise[xShiftTimer];
        const float yShiftSignal = g_engine->move.directional_noise[yShiftTimer];

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
        const float shiftDepth = 0.75f;
        float workXShift = noiseFlow.xShift * (1.0f + shiftMod.modLevel * shiftDepth * xShiftSignal);
        float workYShift = noiseFlow.yShift * (1.0f + shiftMod.modLevel * shiftDepth * yShiftSignal);
        // Ensure neither axis fully drops to zero (prevents pure cardinal motion)
        const float minShift = 0.3f;
        workXShift = fmaxf(minShift, workXShift);
        workYShift = fmaxf(minShift, workYShift);

        // Publish working shift values for the advection pass
        workXShiftCurrent = workXShift;
        workYShiftCurrent = workYShift;

        sampleProfile2D(g_engine->noise2X, g_engine->t, workXSpeed, workXAmp,
                        noiseFlow.xFreq, g_engine->_width, g_engine->xProf);

        sampleProfile2D(g_engine->noise2Y, g_engine->t, workYSpeed, workYAmp,
                        noiseFlow.yFreq, g_engine->_height, g_engine->yProf);

    }

    // --- Advect: two-pass fractional advection (bilinear interpolation) + fade ---

    static void noiseFlowAdvect() {
        // Frame-rate-independent fade: half-life = persistence seconds
        float fade = fl::powf(0.5f, g_engine->dt / g_engine->persistence);

        // Pass 1 — horizontal row shift  (Y-noise drives X movement)
        for (int y = 0; y < g_engine->_height; y++) {
            float sh = g_engine->yProf[y] * workXShiftCurrent;
            for (int x = 0; x < g_engine->_width; x++) {
                float sx  = fmodPos((float)x - sh, (float)g_engine->_width);
                int   ix0 = (int)fl::floorf(sx) % g_engine->_width;
                int   ix1 = (ix0 + 1) % g_engine->_width;
                float f   = sx - fl::floorf(sx);
                float inv = 1.0f - f;
                g_engine->tR[y][x] = g_engine->gR[y][ix0] * inv + g_engine->gR[y][ix1] * f;
                g_engine->tG[y][x] = g_engine->gG[y][ix0] * inv + g_engine->gG[y][ix1] * f;
                g_engine->tB[y][x] = g_engine->gB[y][ix0] * inv + g_engine->gB[y][ix1] * f;
            }
        }

        // Pass 2 — vertical column shift  (X-noise drives Y movement) + dim
        for (int x = 0; x < g_engine->_width; x++) {
            float sh = g_engine->xProf[x] * workYShiftCurrent;
            for (int y = 0; y < g_engine->_height; y++) {
                float sy  = fmodPos((float)y - sh, (float)g_engine->_height);
                int   iy0 = (int)fl::floorf(sy) % g_engine->_height;
                int   iy1 = (iy0 + 1) % g_engine->_height;
                float f   = sy - fl::floorf(sy);
                float inv = 1.0f - f;
                // Keep full float precision; quantize only at LED output.
                g_engine->gR[y][x] = (g_engine->tR[iy0][x] * inv + g_engine->tR[iy1][x] * f) * fade;
                g_engine->gG[y][x] = (g_engine->tG[iy0][x] * inv + g_engine->tG[iy1][x] * f) * fade;
                g_engine->gB[y][x] = (g_engine->tB[iy0][x] * inv + g_engine->tB[iy1][x] * f) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
