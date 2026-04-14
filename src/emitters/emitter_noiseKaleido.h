#pragma once

// ═══════════════════════════════════════════════════════════════════
//  NOISE KALEIDO - emitter_noiseKaleido.h
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"
#include "modulators.h"

namespace flowFields {

    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct NoiseKaleidoParams {
        float driftSpeed = 0.35f;   // noise field drift speed
        float noiseScale = 0.0375f; // noise zoom level
        float noiseBand = 0.1f;     // noise band width (pattern density)
        float kaleidoGamma = 0.65f; // brightness profile exponent
    };

    NoiseKaleidoParams noiseKaleido;

    // 4-octave fbm with early-exit for band rejection.
    // Octaves 4-5 removed: below Nyquist at scale=0.0375 on a 48x32 grid.
    // Uses ValueNoise2D (no grad dispatch) for speed.
    static constexpr float FBM_NORM_INV = 1.0f / (1.0f + 0.5f + 0.25f + 0.125f); // 1/1.875
    static constexpr float FBM_REMAINING[] = { 0.875f, 0.375f, 0.125f, 0.0f };

    static float fbm2d(float x, float y, float bandLo, float bandHi) {
        static constexpr float amps[] = { 1.0f, 0.5f, 0.25f, 0.125f };
        float total = 0.0f;
        float freq = 1.0f;
        for (int i = 0; i < 4; i++) {
            total += kaleidoNoise.noise(x * freq, y * freq) * amps[i];
            // Early exit: can remaining octaves bring total into band?
            float rem = FBM_REMAINING[i];
            if ((total + rem) * FBM_NORM_INV < bandLo) return -1.0f;
            if ((total - rem) * FBM_NORM_INV > bandHi) return  1.0f;
            freq *= 2.0f;
        }
        return total * FBM_NORM_INV;
    }

    // Circular hue interpolation on [0, 1)
    static float lerpHue(float h0, float h1, float u) {
        float d = fmodPos(h1 - h0 + 0.5f, 1.0f) - 0.5f;
        return fmodPos(h0 + d * u, 1.0f);
    }

    static void emitNoiseKaleido() {
        const float scale = noiseKaleido.noiseScale;
        const float speed = noiseKaleido.driftSpeed;
        const float band = noiseKaleido.noiseBand;
        const float gamma = noiseKaleido.kaleidoGamma;

        const int baseX = (WIDTH / 2) + 1;
        const int baseY = (HEIGHT / 2) + 1;
        const int mirrorLimitX = baseX - 2;
        const int mirrorLimitY = baseY - 2;

        // Two layers with different drifts and color phases
        struct LayerCfg { float offX, offY, phase, span; };
        const LayerCfg layers[2] = {
            {  t * 0.55f * speed, -t * 0.43f * speed,        0.00f,  0.16f },
            { -t * 0.34f * speed + 9.7f, t * 0.61f * speed + 4.1f, 0.48f, -0.18f },
        };

        for (int layer = 0; layer < 2; layer++) {
            const LayerCfg& L = layers[layer];
            float h0 = fmodPos(t * colorShift + L.phase, 1.0f);
            float h1 = fmodPos(h0 + L.span, 1.0f);

            for (int gy = 0; gy < baseY; gy++) {
                for (int gx = 0; gx < baseX; gx++) {
                    float sx = clampf(gx + 0.37f, 0.0f, (float)(baseX - 1) - 1e-6f);
                    float sy = clampf(gy + 0.29f, 0.0f, (float)(baseY - 1) - 1e-6f);

                    float n = fbm2d(sx * scale + L.offX, sy * scale + L.offY, 0.0f, band);

                    // Only draw in narrow noise band [0.0, band] for sparse deposition
                    if (n < 0.0f || n > band) continue;

                    float u = n / band;

                    // Tent profile: peak at center of band, gamma-shaped
                    float tprof = 1.0f - fl::fabsf(u - 0.5f) * 2.0f;
                    tprof = fastpow(tprof, gamma);
                    float gain = clampf(0.80f + 0.20f * tprof, 0.0f, 1.0f);

                    // Color from hue interpolation across noise range
                    float hm = lerpHue(h0, h1, u);
                    ColorF c = useRainbow ? hsvRainbow(hm) : hsvSpectrum(hm);
                    float cr = c.r * gain;
                    float cg = c.g * gain;
                    float cb = c.b * gain;

                    // Bilinear deposition with kaleidoscopic mirroring
                    // floor(sx) == gx since sx = gx + 0.37 (constant sub-pixel offset)
                    float fx = sx - gx;
                    float fy = sy - gy;

                    float weights[4] = {
                        (1.0f - fx) * (1.0f - fy),
                        fx * (1.0f - fy),
                        (1.0f - fx) * fy,
                        fx * fy,
                    };
                    int tapX[4] = { gx, gx + 1, gx, gx + 1 };
                    int tapY[4] = { gy, gy + 1, gy, gy + 1 };

                    for (int tap = 0; tap < 4; tap++) {
                        if (weights[tap] <= 0.0f) continue;
                        int tx = tapX[tap];
                        int ty = tapY[tap];
                        float w = weights[tap];
                        int mx = WIDTH  - 1 - tx;
                        int my = HEIGHT - 1 - ty;

                        // Original quadrant
                        blendPixelWeighted(tx, ty, cr, cg, cb, w);
                        // Horizontal mirror
                        if (tx <= mirrorLimitX)
                            blendPixelWeighted(mx, ty, cr, cg, cb, w);
                        // Vertical mirror
                        if (ty <= mirrorLimitY)
                            blendPixelWeighted(tx, my, cr, cg, cb, w);
                        // Diagonal mirror
                        if (tx <= mirrorLimitX && ty <= mirrorLimitY)
                            blendPixelWeighted(mx, my, cr, cg, cb, w);
                    }
                }
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END
    
}