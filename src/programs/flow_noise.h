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

namespace colorTrails {

    // --- Parameter struct ---

    struct NoiseFlowParams {
        float xSpeed     = -1.73f;   // Noise scroll speed  (column axis)
        float ySpeed     = -1.72f;   // Noise scroll speed  (row axis)
        float xAmplitude =  1.00f;   // Noise amplitude     (column axis)
        float yAmplitude =  1.00f;   // Noise amplitude     (row axis)
        float xFrequency =  0.33f;   // Noise spatial scale (column axis) (aka "xScale")
        float yFrequency =  0.32f;   // Noise spatial scale (row axis) (aka "yScale")
        float xShift     =  1.8f;    // Max horizontal shift per row  (pixels)
        float yShift     =  1.8f;    // Max vertical shift per column (pixels)
        bool  use2DNoise =  true;    // false = 1D Perlin, true = 2D Perlin
    };

    // Live flow field param instance
    NoiseFlowParams noiseFlow;

    // Reference: previous per-emitter noise defaults (before decoupling)
    //                      xSpeed, ySpeed, xAmp, yAmp, xFreq, yFreq, xShft, yShft, 2D noise
    // Orbital:    noiseFlow{-1.73f, -1.72f, 1.0f, 1.0f, 0.33f, 0.32f, 1.8f, 1.8f, true};
    // Lissajous:  noiseFlow{0.1f, 0.1f,   1.0f, 1.0f, 0.33f, 0.32f, 1.8f, 1.8f, true};
    // BorderRect: noiseFlow{-1.73f, -1.72f, 0.75f, 0.75f, 0.33f, 0.32f, 1.8f, 1.8f, true};


    // --- Amplitude modulator ---

    struct AmpModParams {
        float intensity = 4.0f;    // Depth of amplitude modulation (0 = off)
        float speed     = 1.0f;    // Temporal speed of the variation noise
        bool  active    = false;   // on/off
    };

    AmpModParams ampMod;

    //  Slow 1D Perlin noise modulates the flow field's xAmplitude/yAmplitude.
    //  Operates on working copies (does not mutate base noiseFlow params).

    static void applyAmpModulation(float t, float& xAmp, float& yAmp) {
        if (!ampMod.active) return;

        float nVarX = ampVarX.noise(t * 0.16f * ampMod.speed);
        float nVarY = ampVarY.noise(t * 0.13f * ampMod.speed + 17.0f);

        float selfMod = 0.5f + 0.5f * ((nVarX + nVarY) * 0.5f);
        float effVariation = ampMod.intensity * selfMod;

        xAmp = clampf(xAmp + nVarX * 0.45f * effVariation, 0.10f, 1.0f);
        yAmp = clampf(yAmp + nVarY * 0.45f * effVariation, 0.10f, 1.0f);
    }


    // --- Profile builders ---

    static void sampleProfile1D(const Perlin1D &n, float t, float speed,
                                float amp, float scale, int count, float *out) {
        const float freq  = 0.23f;
        const float phase = t * speed;
        for (int i = 0; i < count; i++) {
            float v = n.noise(i * freq * scale + phase);
            out[i]  = clampf(v * amp, -1.0f, 1.0f);
        }
    }

    static void sampleProfile2D(const Perlin2D &n, float t, float speed,
                                float amp, float scale, int count, float *out) {
        const float freq   = 0.23f;
        const float scrollY = t * speed;
        for (int i = 0; i < count; i++) {
            float v = n.noise(i * freq * scale, scrollY);
            out[i]  = clampf(v * amp, -1.0f, 1.0f);
        }
    }


    // --- Prepare: build noise profiles, apply modulator, apply flips ---

    static void noiseFlowPrepare(float t) {
        // Working copies of amplitude (modulator may alter these)
        float workXAmp = noiseFlow.xAmplitude;
        float workYAmp = noiseFlow.yAmplitude;

        if (vizConfig.useAmpMod) {
            applyAmpModulation(t, workXAmp, workYAmp);
        }

        // Build noise profiles
        if (noiseFlow.use2DNoise) {
            sampleProfile2D(noise2X, t, noiseFlow.xSpeed, workXAmp,
                            noiseFlow.xFrequency, WIDTH,  xProf);
            sampleProfile2D(noise2Y, t, noiseFlow.ySpeed, workYAmp,
                            noiseFlow.yFrequency, HEIGHT, yProf);
        } else {
            sampleProfile1D(noiseX, t, noiseFlow.xSpeed, workXAmp,
                            noiseFlow.xFrequency, WIDTH,  xProf);
            sampleProfile1D(noiseY, t, noiseFlow.ySpeed, workYAmp,
                            noiseFlow.yFrequency, HEIGHT, yProf);
        }

        // Apply axis flip toggles
        if (vizConfig.flipVertical) {
            for (int i = 0; i < WIDTH / 2; i++) {
                float tmp = xProf[i];
                xProf[i] = xProf[WIDTH - 1 - i];
                xProf[WIDTH - 1 - i] = tmp;
            }
        }
        if (vizConfig.flipHorizontal) {
            for (int i = 0; i < HEIGHT / 2; i++) {
                float tmp = yProf[i];
                yProf[i] = yProf[HEIGHT - 1 - i];
                yProf[HEIGHT - 1 - i] = tmp;
            }
        }
    }


    // --- Advect: two-pass fractional advection (bilinear interpolation) + fade ---

    static void noiseFlowAdvect(float dt) {
        // The original Python applied fadeRate once per frame at 60 FPS.
        // Scale the exponent by actual dt so decay rate is frame-rate-independent.
        float fadePerSec = fl::powf(vizConfig.fadeRate, 60.0f);
        float fade = fl::powf(fadePerSec, dt);

        // Pass 1 — horizontal row shift  (Y-noise drives X movement)
        for (int y = 0; y < HEIGHT; y++) {
            float sh = yProf[y] * noiseFlow.xShift;
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
            float sh = xProf[x] * noiseFlow.yShift;
            for (int y = 0; y < HEIGHT; y++) {
                float sy  = fmodPos((float)y - sh, (float)HEIGHT);
                int   iy0 = (int)fl::floorf(sy) % HEIGHT;
                int   iy1 = (iy0 + 1) % HEIGHT;
                float f   = sy - fl::floorf(sy);
                float inv = 1.0f - f;
                // truncate to integer — Python's Pygame surface stores uint8,
                // so int(value) kills sub-1.0 residuals every frame.
                gR[y][x] = fl::floorf((tR[iy0][x] * inv + tR[iy1][x] * f) * fade);
                gG[y][x] = fl::floorf((tG[iy0][x] * inv + tG[iy1][x] * f) * fade);
                gB[y][x] = fl::floorf((tB[iy0][x] * inv + tB[iy1][x] * f) * fade);
            }
        }
    }

} // namespace colorTrails
