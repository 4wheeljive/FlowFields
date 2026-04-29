#pragma once

// ═══════════════════════════════════════════════════════════════════
//  DIRECTIONAL FLOW FIELD — flow_directional.h
// ═══════════════════════════════════════════════════════════════════
//
//  Rotating wind advection with optional perpendicular wave wobble.
//  Ported from Python _5.py mode_idx 7 (rotating wind) and 8 (wind wave).
//
//  rotateSpeed = 0  -> fixed wind direction (pure directional push)
//  rotateSpeed > 0  -> wind vector rotates over time
//  waveAmp     > 0  -> adds sinusoidal wobble perpendicular to wind

#include "FlowFieldsEngine.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct DirectionalParams {
        float windStep    = 0.95f;   // backward sample distance (pixels)
        float blendFactor = 0.86f;   // transport blend (0 = keep current, 1 = full transport)
        float rotateSpeed = 0.25f;   // wind rotation (rev/sec; 0 = fixed direction)
        float waveAmp     = 0.0f;    // perpendicular wave amplitude (0 = off)
        float waveFreq    = 0.20f;   // wave spatial frequency
        float waveSpeed   = 1.20f;   // wave temporal speed
    };

    DirectionalParams   directional;

    // Computed in prepare, consumed in advect
    static float dirCos, dirSin;   // unit wind direction
    static float dirT;             // time snapshot for wave phase

    static void directionalPrepare() {
        float angle = g_engine->t * (2.0f * CT_PI * directional.rotateSpeed);
        dirCos = fl::cosf(angle);
        dirSin = fl::sinf(angle);
        dirT   = g_engine->t;
    }

    static void directionalAdvect() {
        // Frame-rate-independent fade
        float fade = fl::powf(0.5f, g_engine->dt / g_engine->persistence);

        float step = directional.windStep;
        float frac = directional.blendFactor;
        float inv  = 1.0f - frac;

        // Wind vector (scaled by step) and perpendicular unit vector
        float wx =  dirCos * step;
        float wy =  dirSin * step;
        float px = -dirSin;
        float py =  dirCos;

        float wAmp  = directional.waveAmp;
        float wFreq = directional.waveFreq;
        float wSpd  = directional.waveSpeed;
        bool  doWave = (wAmp > 0.001f);

        float maxX = (float)(g_engine->_width  - 1) - 1e-6f;
        float maxY = (float)(g_engine->_height - 1) - 1e-6f;

        // Snapshot live grid to scratch buffer
        for (int y = 0; y < g_engine->_height; y++)
            for (int x = 0; x < g_engine->_width; x++) {
                g_engine->tR[y][x] = g_engine->gR[y][x];
                g_engine->tG[y][x] = g_engine->gG[y][x];
                g_engine->tB[y][x] = g_engine->gB[y][x];
            }

        for (int y = 0; y < g_engine->_height; y++) {
            for (int x = 0; x < g_engine->_width; x++) {
                // 1) Backward advection along wind
                float sx = (float)x - wx;
                float sy = (float)y - wy;

                // 2) Optional perpendicular wave wobble
                if (doWave) {
                    float proj = sx * dirCos + sy * dirSin;
                    float wobble = fl::sinf(proj * wFreq + dirT * wSpd) * wAmp;
                    sx += px * wobble;
                    sy += py * wobble;
                }

                // Clamp to grid bounds (color flows off edges)
                sx = clampf(sx, 0.0f, maxX);
                sy = clampf(sy, 0.0f, maxY);

                // Bilinear sample from scratch buffer
                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = min(g_engine->_width  - 1, ix0 + 1);
                int   iy1 = min(g_engine->_height - 1, iy0 + 1);
                float fx  = sx - ix0;
                float fy  = sy - iy0;

                float rTop = g_engine->tR[iy0][ix0] * (1.0f - fx) + g_engine->tR[iy0][ix1] * fx;
                float rBot = g_engine->tR[iy1][ix0] * (1.0f - fx) + g_engine->tR[iy1][ix1] * fx;
                float sr   = rTop * (1.0f - fy) + rBot * fy;

                float gTop = g_engine->tG[iy0][ix0] * (1.0f - fx) + g_engine->tG[iy0][ix1] * fx;
                float gBot = g_engine->tG[iy1][ix0] * (1.0f - fx) + g_engine->tG[iy1][ix1] * fx;
                float sg   = gTop * (1.0f - fy) + gBot * fy;

                float bTop = g_engine->tB[iy0][ix0] * (1.0f - fx) + g_engine->tB[iy0][ix1] * fx;
                float bBot = g_engine->tB[iy1][ix0] * (1.0f - fx) + g_engine->tB[iy1][ix1] * fx;
                float sb   = bTop * (1.0f - fy) + bBot * fy;

                // Blend current pixel with transported sample, then fade
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
