#pragma once

// ═══════════════════════════════════════════════════════════════════
//  RADIAL FLOW FIELD — flow_radial.h
// ═══════════════════════════════════════════════════════════════════
//
//  Radial transport: each pixel samples from a point closer to or
//  farther from the grid center, causing color to flow outward or inward.
//
//  Ported from Python apply_from_center_tail() in _5.py (mode 15).

#include "flowFieldsTypes.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct RadialParams {
        float radialStep        = 0.18f;   // how far to sample (controls radial speed)
        float blendFactor       = 0.45f;   // blend factor (0 = keep current, 1 = fully transport)
        bool  outward           = false;   // false = radial in, true = radial out
    };

    RadialParams    radial;

    // --- Prepare: nothing to build (geometry is purely radial) ---

    static void radialPrepare() {
    }


    // --- Advect: radial transport with clamped bilinear sampling + fade ---

    static void radialAdvect() {
        float cx = (WIDTH  - 1) * 0.5f;
        float cy = (HEIGHT - 1) * 0.5f;

        // Frame-rate-independent fade: half-life = persistence seconds
        float fade = fl::powf(0.5f, dt / persistence);

        float step = radial.radialStep;
        if (radial.outward) {
            step *= -1.f;
        }
        float frac = radial.blendFactor;
        float inv  = 1.0f - frac;

        // Copy live grid to scratch buffer
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                tR[y][x] = gR[y][x];
                tG[y][x] = gG[y][x];
                tB[y][x] = gB[y][x];
            }
        }

        // For each pixel, sample from a point closer to center
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float r  = fl::sqrtf(dx * dx + dy * dy);

                float sr, sg, sb;

                if (r > 1e-6f) {
                    float ux = dx / r;
                    float uy = dy / r;
                    float sx = (float)x - ux * step;
                    float sy = (float)y - uy * step;

                    // Clamp to grid bounds (no wrapping — content fades at edges)
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
                } else {
                    // At center — no direction to sample from
                    sr = tR[y][x];
                    sg = tG[y][x];
                    sb = tB[y][x];
                }

                // Blend current pixel with sampled pixel, then fade
                // Keep full float precision; quantize only at LED output.
                gR[y][x] = (tR[y][x] * inv + sr * frac) * fade;
                gG[y][x] = (tG[y][x] * inv + sg * frac) * fade;
                gB[y][x] = (tB[y][x] * inv + sb * frac) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
