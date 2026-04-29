#pragma once

// ═══════════════════════════════════════════════════════════════════
//  RADIAL FLOW FIELD — flow_radial.h
// ═══════════════════════════════════════════════════════════════════
//
//  Radial transport: each pixel samples from a point closer to or
//  farther from the grid center, causing color to flow outward or inward.
//
//  Ported from Python apply_from_center_tail() in _5.py (mode 15).

#include "FlowFieldsEngine.h"

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
        float cx = (g_engine->_width  - 1) * 0.5f;
        float cy = (g_engine->_height - 1) * 0.5f;

        // Frame-rate-independent fade: half-life = persistence seconds
        float fade = fl::powf(0.5f, g_engine->dt / g_engine->persistence);

        float step = radial.radialStep;
        if (radial.outward) {
            step *= -1.f;
        }
        float frac = radial.blendFactor;
        float inv  = 1.0f - frac;

        // Copy live grid to scratch buffer
        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                g_engine->tR[y][x] = g_engine->gR[y][x];
                g_engine->tG[y][x] = g_engine->gG[y][x];
                g_engine->tB[y][x] = g_engine->gB[y][x];
            }
        }

        // For each pixel, sample from a point closer to center
        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
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
                } else {
                    // At center — no direction to sample from
                    sr = g_engine->tR[y][x];
                    sg = g_engine->tG[y][x];
                    sb = g_engine->tB[y][x];
                }

                // Blend current pixel with sampled pixel, then fade
                // Keep full float precision; quantize only at LED output.
                g_engine->gR[y][x] = (g_engine->tR[y][x] * inv + sr * frac) * fade;
                g_engine->gG[y][x] = (g_engine->tG[y][x] * inv + sg * frac) * fade;
                g_engine->gB[y][x] = (g_engine->tB[y][x] * inv + sb * frac) * fade;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
