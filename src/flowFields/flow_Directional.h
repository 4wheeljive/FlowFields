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

#include "colorTrailsTypes.h"

namespace colorTrails {

    ParameterPack windStep;
    ParameterPack blendFactor;
    ParameterPack rotateSpeed;
    ParameterPack waveAmp;
    ParameterPack waveFreq;
    ParameterPack waveSpeed;
    
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

    static void directionalPrepare(float t) {
        float angle = t * (2.0f * CT_PI * directional.rotateSpeed);
        dirCos = fl::cosf(angle);
        dirSin = fl::sinf(angle);
        dirT   = t;
    }

    static void directionalAdvect(float dt) {
        // Frame-rate-independent fade
        float fade = fl::powf(0.5f, dt / vizConfig.persistence);

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

        float maxX = (float)(WIDTH  - 1) - 1e-6f;
        float maxY = (float)(HEIGHT - 1) - 1e-6f;

        // Snapshot live grid to scratch buffer
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++) {
                tR[y][x] = gR[y][x];
                tG[y][x] = gG[y][x];
                tB[y][x] = gB[y][x];
            }

        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
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

                // Blend current pixel with transported sample, then fade
                gR[y][x] = fl::floorf((tR[y][x] * inv + sr * frac) * fade);
                gG[y][x] = fl::floorf((tG[y][x] * inv + sg * frac) * fade);
                gB[y][x] = fl::floorf((tB[y][x] * inv + sb * frac) * fade);
            }
        }
    }

} // namespace colorTrails
