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

#include "colorTrailsTypes.h"
#include "modulators.h"

namespace colorTrails {

    struct RingFlowParams {
        float innerSwirl = -0.26f;   // angular step for inner zone (negative = CW)
        float outerSwirl =  0.24f;   // angular step for outer zone (positive = CCW)
        float midDrift   =  0.42f;   // radial outward drift for middle zone

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
    static void ringFlowPrepare(float t) {
        (void)t;
        const ModConfig& breatheMod = ringFlow.modBreathe;

        const uint8_t innerTimer = breatheMod.modTimer;
        const uint8_t midTimer   = breatheMod.modTimer + 1;
        const uint8_t outerTimer = breatheMod.modTimer + 2;

        // Base breathing frequencies (from Python: 0.61, 0.93, 1.27 rad/sec)
        // ratio = freq / 1000 since runtime is in millis
        timings.ratio[innerTimer] = 0.00061f * breatheMod.modRate;
        timings.ratio[midTimer]   = 0.00093f * breatheMod.modRate;
        timings.ratio[outerTimer] = 0.00127f * breatheMod.modRate;

        // Phase offsets: create separation between zones
        // (derived from Python phases 0.20, 1.10, 2.30 rad)
        timings.offset[innerTimer] = 328.0f;
        timings.offset[midTimer]   = 1183.0f;
        timings.offset[outerTimer] = 1811.0f;

        calculate_modulators(timings, 3);

        // Breathing: crossfade from static (1.0) to full pulse [0.1, 1.0]
        const float level = breatheMod.modLevel;
        ringBreatheInner = (1.0f - level) + level * (0.55f + 0.45f * move.directional_sine[innerTimer]);
        ringBreatheMid   = (1.0f - level) + level * (0.55f + 0.45f * move.directional_sine[midTimer]);
        ringBreatheOuter = (1.0f - level) + level * (0.55f + 0.45f * move.directional_sine[outerTimer]);
    }

    // --- Advect: per-pixel polar backward-sampling with zone-weighted transport ---
    static void ringFlowAdvect(float dt) {
        float fade = fl::powf(0.5f, dt / vizConfig.persistence);

        float cx = (WIDTH  - 1) * 0.5f;
        float cy = (HEIGHT - 1) * 0.5f;
        float max_r = fl::sqrtf(cx * cx + cy * cy);
        float inv_max_r = (max_r > 1e-6f) ? 1.0f / max_r : 0.0f;

        // Breathing-modulated zone geometry
        float c_inner = 0.23f * ringBreatheInner;
        float c_mid   = 0.56f * ringBreatheMid;
        float c_outer = 0.86f * ringBreatheOuter;
        float s_inner = fmaxf(0.02f, 0.17f * ringBreatheInner);
        float s_mid   = fmaxf(0.02f, 0.18f * ringBreatheMid);
        float s_outer = fmaxf(0.02f, 0.16f * ringBreatheOuter);

        // Precompute reciprocals for gaussian exponents
        float inv_si2 = 1.0f / (s_inner * s_inner);
        float inv_sm2 = 1.0f / (s_mid   * s_mid);
        float inv_so2 = 1.0f / (s_outer * s_outer);

        float maxClamp = max_r + 1.5f;

        // Snapshot live grid to scratch buffer
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++) {
                tR[y][x] = gR[y][x];
                tG[y][x] = gG[y][x];
                tB[y][x] = gB[y][x];
            }

        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float r  = fl::sqrtf(dx * dx + dy * dy);
                float rn = r * inv_max_r;

                // Gaussian zone weights
                float di  = rn - c_inner;
                float dm  = rn - c_mid;
                float do_ = rn - c_outer;

                float w_inner = expf(-(di * di) * inv_si2);
                float w_mid   = expf(-(dm * dm) * inv_sm2);
                float w_outer = expf(-(do_ * do_) * inv_so2);

                float w_sum = w_inner + w_mid + w_outer + 1e-6f;
                w_inner /= w_sum;
                w_mid   /= w_sum;
                w_outer /= w_sum;

                float ang   = ringFlow.innerSwirl * w_inner + ringFlow.outerSwirl * w_outer;
                float drift = ringFlow.midDrift * w_mid;

                // Backward sampling for stable advection
                float sample_r = clampf(r - drift, 0.0f, maxClamp);

                // Rotation identity avoids atan2:
                // sample at (th - ang) with radius sample_r
                float sx, sy;
                if (r > 1e-6f) {
                    SinCosResult sc = sincos_fast(ang);
                    float scale = sample_r / r;
                    sx = cx + (dx * sc.cos_val + dy * sc.sin_val) * scale;
                    sy = cy + (dy * sc.cos_val - dx * sc.sin_val) * scale;
                } else {
                    sx = cx;
                    sy = cy;
                }

                // Clamp to grid bounds
                sx = clampf(sx, 0.0f, (float)(WIDTH  - 1) - 1e-6f);
                sy = clampf(sy, 0.0f, (float)(HEIGHT - 1) - 1e-6f);

                // Bilinear interpolation from scratch buffer
                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = min(WIDTH  - 1, ix0 + 1);
                int   iy1 = min(HEIGHT - 1, iy0 + 1);
                float fx  = sx - ix0;
                float fy  = sy - iy0;

                float rTop = tR[iy0][ix0] * (1.0f - fx) + tR[iy0][ix1] * fx;
                float rBot = tR[iy1][ix0] * (1.0f - fx) + tR[iy1][ix1] * fx;
                float sr   = rTop * (1.0f - fy) + rBot * fy;

                float gTop = tG[iy0][ix0] * (1.0f - fx) + tG[iy0][ix1] * fx;
                float gBot = tG[iy1][ix0] * (1.0f - fx) + tG[iy1][ix1] * fx;
                float sg   = gTop * (1.0f - fy) + gBot * fy;

                float bTop = tB[iy0][ix0] * (1.0f - fx) + tB[iy0][ix1] * fx;
                float bBot = tB[iy1][ix0] * (1.0f - fx) + tB[iy1][ix1] * fx;
                float sb   = bTop * (1.0f - fy) + bBot * fy;

                gR[y][x] = fl::floorf(sr * fade);
                gG[y][x] = fl::floorf(sg * fade);
                gB[y][x] = fl::floorf(sb * fade);
            }
        }
    }

} // namespace colorTrails
