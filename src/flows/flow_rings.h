#pragma once

// ═══════════════════════════════════════════════════════════════════
//  RING FLOW FIELD — flow_rings.h
// ═══════════════════════════════════════════════════════════════════
//
//  Concentric ring transport: inner CW swirl, middle outward drift,
//  outer CCW swirl.  Zone boundaries "breathe" via modulator-driven
//  sinusoidal oscillation.
//
//  Ported from Python perlin_grid_visualization_7.py mode 13 ("Ring Flow 2").

#include "FlowFieldsEngine.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct RingFlowParams {
        float innerSwirl = -0.2f;   // angular step for inner zone (negative = CW)
        float outerSwirl =  0.2f;   // angular step for outer zone (positive = CCW)
        float midDrift   =  0.3f;   // radial outward drift for middle zone

        // Breathing modulation — controls how zone geometry pulses over time.
        // Uses 3 consecutive timers (modTimer, +1, +2) for inner/mid/outer.
        ModConfig modBreathe = {0, 1.0f, 1.0f};   // modTimer, modRate, modLevel
    };

    RingFlowParams ringFlow;

    // Working values computed in prepare, consumed in advect
    static float ringBreatheInner = 1.0f;
    static float ringBreatheMid   = 1.0f;
    static float ringBreatheOuter = 1.0f;

    // --- Prepare: compute breathing factors from modulators ---
    static void ringFlowPrepare() {
        const ModConfig& breatheMod = ringFlow.modBreathe;

        const uint8_t innerTimer = breatheMod.modTimer;
        const uint8_t midTimer   = breatheMod.modTimer + 1;
        const uint8_t outerTimer = breatheMod.modTimer + 2;

        // Base breathing frequencies (from Python: 0.61, 0.93, 1.27 rad/sec)
        // ratio = freq / 1000 since runtime is in millis
        g_engine->timings.ratio[innerTimer] = 0.00061f * breatheMod.modRate;
        g_engine->timings.ratio[midTimer]   = 0.00093f * breatheMod.modRate;
        g_engine->timings.ratio[outerTimer] = 0.00127f * breatheMod.modRate;

        // Phase offsets: create separation between zones
        // (derived from Python phases 0.20, 1.10, 2.30 rad)
        g_engine->timings.offset[innerTimer] = 328.0f;
        g_engine->timings.offset[midTimer]   = 1183.0f;
        g_engine->timings.offset[outerTimer] = 1811.0f;

        g_engine->calculate_modulators(3);

        // Breathing: crossfade from static (1.0) to full pulse [0.1, 1.0]
        const float level = breatheMod.modLevel;
        ringBreatheInner = (1.0f - level) + level * (0.55f + 0.45f * g_engine->move.directional_sine[innerTimer]);
        ringBreatheMid   = (1.0f - level) + level * (0.55f + 0.45f * g_engine->move.directional_sine[midTimer]);
        ringBreatheOuter = (1.0f - level) + level * (0.55f + 0.45f * g_engine->move.directional_sine[outerTimer]);
    }

    // --- Advect: per-pixel polar backward-sampling with zone-weighted transport ---

    static void ringFlowAdvect() {
        float fade = fl::powf(0.5f, g_engine->dt / g_engine->persistence);

        const float cx = (g_engine->_width - 1) * 0.5f;
        const float cy = (g_engine->_height - 1) * 0.5f;
        const float max_r = fl::sqrtf(cx * cx + cy * cy);
        const float inv_max_r = (max_r > 1e-6f) ? 1.0f / max_r : 0.0f;
        const float maxClamp = max_r + 1.5f;

        const float b1Base = 0.38f;
        const float b2Base = 0.72f;

        const float b1 = b1Base + 0.045f * (ringBreatheInner - 1.0f);
        const float b2 = b2Base + 0.045f * (ringBreatheOuter - 1.0f);

        const float soft1 = fmaxf(0.035f, 0.070f * ringBreatheMid);
        const float soft2 = fmaxf(0.035f, 0.080f * ringBreatheMid);

        const float midAngular =
            0.18f * (ringFlow.innerSwirl + ringFlow.outerSwirl);

        // Snapshot live grid to scratch buffer
        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                g_engine->tR[y][x] = g_engine->gR[y][x];
                g_engine->tG[y][x] = g_engine->gG[y][x];
                g_engine->tB[y][x] = g_engine->gB[y][x];
            }
        }

        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                const float dx = (float)x - cx;
                const float dy = (float)y - cy;
                const float r = fl::sqrtf(dx * dx + dy * dy);
                const float rn = r * inv_max_r;

                const float t1 = clampf(0.5f + 0.5f * ((rn - b1) / soft1), 0.0f, 1.0f);
                const float t2 = clampf(0.5f + 0.5f * ((rn - b2) / soft2), 0.0f, 1.0f);

                const float s1 = t1 * t1 * (3.0f - 2.0f * t1);
                const float s2 = t2 * t2 * (3.0f - 2.0f * t2);

                const float w_inner = 1.0f - s1;
                const float w_mid   = s1 * (1.0f - s2);
                const float w_outer = s2;

                const float ang =
                    ringFlow.innerSwirl * w_inner +
                    midAngular          * w_mid +
                    ringFlow.outerSwirl * w_outer;

                const float drift = ringFlow.midDrift * w_mid;

                const float sample_r = clampf(r - drift, 0.0f, maxClamp);

                float sx, sy;
                if (r > 1e-6f) {
                    SinCosResult sc = sincos_fast(ang);
                    const float scale = sample_r / r;

                    sx = cx + (dx * sc.cos_val + dy * sc.sin_val) * scale;
                    sy = cy + (dy * sc.cos_val - dx * sc.sin_val) * scale;
                } else {
                    sx = cx;
                    sy = cy;
                }

                // Clamp to grid bounds
                sx = clampf(sx, 0.0f, (float)(g_engine->_width - 1) - 1e-6f);
                sy = clampf(sy, 0.0f, (float)(g_engine->_height - 1) - 1e-6f);

                // Bilinear interpolation from scratch buffer
                const int ix0 = (int)fl::floorf(sx);
                const int iy0 = (int)fl::floorf(sy);
                const int ix1 = min(g_engine->_width - 1, ix0 + 1);
                const int iy1 = min(g_engine->_height - 1, iy0 + 1);

                const float fx = sx - ix0;
                const float fy = sy - iy0;

                const float rTop = g_engine->tR[iy0][ix0] * (1.0f - fx) + g_engine->tR[iy0][ix1] * fx;
                const float rBot = g_engine->tR[iy1][ix0] * (1.0f - fx) + g_engine->tR[iy1][ix1] * fx;
                const float sr   = rTop * (1.0f - fy) + rBot * fy;

                const float gTop = g_engine->tG[iy0][ix0] * (1.0f - fx) + g_engine->tG[iy0][ix1] * fx;
                const float gBot = g_engine->tG[iy1][ix0] * (1.0f - fx) + g_engine->tG[iy1][ix1] * fx;
                const float sg   = gTop * (1.0f - fy) + gBot * fy;

                const float bTop = g_engine->tB[iy0][ix0] * (1.0f - fx) + g_engine->tB[iy0][ix1] * fx;
                const float bBot = g_engine->tB[iy1][ix0] * (1.0f - fx) + g_engine->tB[iy1][ix1] * fx;
                const float sb   = bTop * (1.0f - fy) + bBot * fy;

                // Keep full float precision; quantize only at LED output.
                g_engine->gR[y][x] = sr * fade;
                g_engine->gG[y][x] = sg * fade;
                g_engine->gB[y][x] = sb * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
