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

#include "flowFieldsTypes.h"
#include "modulators.h"

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
    
    static void ringFlowAdvect() {
        float fade = fl::powf(0.5f, dt / persistence);

        const float cx = (WIDTH - 1) * 0.5f;
        const float cy = (HEIGHT - 1) * 0.5f;
        const float max_r = fl::sqrtf(cx * cx + cy * cy);
        const float inv_max_r = (max_r > 1e-6f) ? 1.0f / max_r : 0.0f;
        const float maxClamp = max_r + 1.5f;

        // -----------------------------------------------------------------
        // Soft-boundary ring geometry
        //
        // Instead of 3 Gaussian "peaks", define 2 soft radial boundaries:
        //   b1 = inner <-> middle
        //   b2 = middle <-> outer
        //
        // Keep the overall ring layout stable, and let breathing mostly affect
        // boundary position a little plus transition softness.
        // -----------------------------------------------------------------
        const float b1Base = 0.38f;
        const float b2Base = 0.72f;

        // Small center breathing around the boundaries
        const float b1 = b1Base + 0.045f * (ringBreatheInner - 1.0f);
        const float b2 = b2Base + 0.045f * (ringBreatheOuter - 1.0f);

        // Softness breathes more than position
        const float soft1 = fmaxf(0.035f, 0.070f * ringBreatheMid);
        const float soft2 = fmaxf(0.035f, 0.080f * ringBreatheMid);

        // Small angular carry-through in the middle band
        const float midAngular =
            0.18f * (ringFlow.innerSwirl + ringFlow.outerSwirl);

        // Snapshot live grid to scratch buffer
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                tR[y][x] = gR[y][x];
                tG[y][x] = gG[y][x];
                tB[y][x] = gB[y][x];
            }
        }

        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                const float dx = (float)x - cx;
                const float dy = (float)y - cy;
                const float r = fl::sqrtf(dx * dx + dy * dy);
                const float rn = r * inv_max_r;

                // -------------------------------------------------------------
                // Boundary blends
                //
                // t1 = how far we've transitioned from inner to mid
                // t2 = how far we've transitioned from mid to outer
                // -------------------------------------------------------------
                const float t1 = clampf(0.5f + 0.5f * ((rn - b1) / soft1), 0.0f, 1.0f);
                const float t2 = clampf(0.5f + 0.5f * ((rn - b2) / soft2), 0.0f, 1.0f);

                // Smoothstep for softer perceptual transitions
                const float s1 = t1 * t1 * (3.0f - 2.0f * t1);
                const float s2 = t2 * t2 * (3.0f - 2.0f * t2);

                // Region weights derived from 2 soft boundaries
                const float w_inner = 1.0f - s1;
                const float w_mid   = s1 * (1.0f - s2);
                const float w_outer = s2;

                // -------------------------------------------------------------
                // Transport field
                //
                // Inner: CW swirl
                // Mid: outward drift + a little angular continuity
                // Outer: CCW swirl
                // -------------------------------------------------------------
                const float ang =
                    ringFlow.innerSwirl * w_inner +
                    midAngular          * w_mid +
                    ringFlow.outerSwirl * w_outer;

                const float drift = ringFlow.midDrift * w_mid;

                // Backward sampling for stable advection
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
                sx = clampf(sx, 0.0f, (float)(WIDTH - 1) - 1e-6f);
                sy = clampf(sy, 0.0f, (float)(HEIGHT - 1) - 1e-6f);

                // Bilinear interpolation from scratch buffer
                const int ix0 = (int)fl::floorf(sx);
                const int iy0 = (int)fl::floorf(sy);
                const int ix1 = min(WIDTH - 1, ix0 + 1);
                const int iy1 = min(HEIGHT - 1, iy0 + 1);

                const float fx = sx - ix0;
                const float fy = sy - iy0;

                const float rTop = tR[iy0][ix0] * (1.0f - fx) + tR[iy0][ix1] * fx;
                const float rBot = tR[iy1][ix0] * (1.0f - fx) + tR[iy1][ix1] * fx;
                const float sr   = rTop * (1.0f - fy) + rBot * fy;

                const float gTop = tG[iy0][ix0] * (1.0f - fx) + tG[iy0][ix1] * fx;
                const float gBot = tG[iy1][ix0] * (1.0f - fx) + tG[iy1][ix1] * fx;
                const float sg   = gTop * (1.0f - fy) + gBot * fy;

                const float bTop = tB[iy0][ix0] * (1.0f - fx) + tB[iy0][ix1] * fx;
                const float bBot = tB[iy1][ix0] * (1.0f - fx) + tB[iy1][ix1] * fx;
                const float sb   = bTop * (1.0f - fy) + bBot * fy;

                // Keep full float precision; quantize only at LED output.
                gR[y][x] = sr * fade;
                gG[y][x] = sg * fade;
                gB[y][x] = sb * fade;
            }
        }
    }
        
    
    /*
    static void ringFlowAdvect(float dt) {
        float fade = fl::powf(0.5f, dt / persistence);

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
    }*/

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
