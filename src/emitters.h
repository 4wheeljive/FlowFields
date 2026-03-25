#pragma once

// ═══════════════════════════════════════════════════════════════════
//  EMITTERS — emitters.h
// ═══════════════════════════════════════════════════════════════════

#include "colorTrailsTypes.h"
#include "modulators.h"

namespace colorTrails {

    // Orbiting dots
    static void emitOrbitalDots(float t) {

        // Configure timers for this emitter's modulated params
        Modulators::configureTimer(orbitalDots.modOrbitSpeed);
        Modulators::configureTimer(orbitalDots.modOrbitDiam);
        calculate_modulators(timings);

        // Get effective values (base modified by modulator output)
        float effOrbitSpeed = Modulators::apply(orbitalDots.orbitSpeed, orbitalDots.modOrbitSpeed);
        float effOrbitDiam  = Modulators::apply(orbitalDots.orbitDiam,  orbitalDots.modOrbitDiam);

        float fNumDots = static_cast<float>(orbitalDots.numDots);
        float ocx  = WIDTH  * 0.5f - 0.5f;
        float ocy  = HEIGHT * 0.5f - 0.5f;
        float orad = effOrbitDiam * 0.8f;
        float base = t * effOrbitSpeed;
        for (int i = 0; i < orbitalDots.numDots; i++) {
            float a  = base + i * (2.0f * CT_PI / fNumDots);
            float cx = ocx + fl::cosf(a) * orad;
            float cy = ocy + fl::sinf(a) * orad;
            ColorF c = rainbow(t, vizConfig.colorShift, i / fNumDots);
            drawDot(cx, cy, orbitalDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // Swarming dots — variable number of dots moving in a loose shifting group.
    // Uses calculate_modulators() with 2 timers per dot (X and Y).
    // swarmSpread controls grouping (0 = clustered, 1 = independent, >1 = wide).
    // Max 5 dots (num_timers=10, 2 timers per dot).
    static void emitSwarmingDots(float t) {
        uint8_t n = swarmingDots.numDots;
        float fNumDots = static_cast<float>(n);

        // 2 timers per dot: [d*2]=X, [d*2+1]=Y (up to 10 timers for 5 dots)
        // Similar ratios keep dots moving at comparable speeds;
        // irrational relationships prevent exact repetition.
        static const float baseRatios[10] = {
            0.00173f, 0.00131f,   // dot 0: X, Y
            0.00197f, 0.00149f,   // dot 1: X, Y
            0.00211f, 0.00113f,   // dot 2: X, Y
            0.00157f, 0.00189f,   // dot 3: X, Y
            0.00223f, 0.00167f    // dot 4: X, Y
        };
        // Offsets: each dot's Y is ~quarter-period ahead of its X
        // to create elliptical paths instead of diagonal lines
        static const float baseOffsets[10] = {
               0.0f,  900.0f,    // dot 0
             600.0f, 1700.0f,    // dot 1
            1300.0f, 2400.0f,    // dot 2
            1900.0f, 3100.0f,    // dot 3
            2600.0f, 3800.0f     // dot 4
        };

        uint8_t numTimers = n * 2;
        for (uint8_t i = 0; i < numTimers; i++) {
            timings.ratio[i]  = baseRatios[i] * swarmingDots.swarmSpeed;
            timings.offset[i] = baseOffsets[i];
        }

        calculate_modulators(timings);

        // Each dot: dedicated X (even index) and Y (odd index)
        float dotX[5], dotY[5];
        float cenX = 0.0f, cenY = 0.0f;
        for (int d = 0; d < n; d++) {
            dotX[d] = move.directional_sine[d * 2];
            dotY[d] = move.directional_sine[d * 2 + 1];
            cenX += dotX[d];
            cenY += dotY[d];
        }

        // Group center
        cenX /= fNumDots;
        cenY /= fNumDots;

        // Blend each dot between center and its own position via swarmSpread
        float spread = swarmingDots.swarmSpread;
        for (int d = 0; d < n; d++) {
            float sx = cenX + spread * (dotX[d] - cenX);
            float sy = cenY + spread * (dotY[d] - cenY);

            float cx = (WIDTH  - 1) * 0.5f * (1.0f + sx);
            float cy = (HEIGHT - 1) * 0.5f * (1.0f + sy);

            ColorF c = rainbow(t, vizConfig.colorShift, d / fNumDots);
            drawDot(cx, cy, swarmingDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // Lissajous line
    static void emitLissajousLine(float t) {
        const float cx = (WIDTH  - 1) * 0.5f;
        const float cy = (HEIGHT - 1) * 0.5f;
        float s = lissajous.lineSpeed;
        const float amp = lissajous.lineAmp;

        float lx1 = cx + (amp + 1.5f) * fl::sinf(t * s * 1.13f + 0.20f);
        float ly1 = cy + (amp + 0.5f) * fl::sinf(t * s * 1.71f + 1.30f);
        float lx2 = cx + (amp + 2.0f) * fl::sinf(t * s * 1.89f + 2.20f);
        float ly2 = cy + (amp + 1.0f) * fl::sinf(t * s * 1.37f + 0.70f);

        drawAASubpixelLine(lx1, ly1, lx2, ly2, t, vizConfig.colorShift);
        ColorF ca = rainbow(t, vizConfig.colorShift, 0.0f);
        ColorF cb = rainbow(t, vizConfig.colorShift, 1.0f);
        drawAAEndpointDisc(lx1, ly1, ca.r, ca.g, ca.b, 0.85f);
        drawAAEndpointDisc(lx2, ly2, cb.r, cb.g, cb.b, 0.85f);
    }

    // Rainbow border rectangle — reads from `borderRect`
    static void emitRainbowBorder(float t) {
        const int total = 2 * (WIDTH + HEIGHT) - 4;
        int idx = 0;
        // Top edge: left to right
        for (int x = 0; x < WIDTH; x++) {
            ColorF c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[0][x] = c.r; gG[0][x] = c.g; gB[0][x] = c.b;
            idx++;
        }
        // Right edge: top+1 to bottom
        for (int y = 1; y < HEIGHT; y++) {
            ColorF c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[y][WIDTH-1] = c.r; gG[y][WIDTH-1] = c.g; gB[y][WIDTH-1] = c.b;
            idx++;
        }
        // Bottom edge: right-1 to left
        for (int x = WIDTH - 2; x >= 0; x--) {
            ColorF c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[HEIGHT-1][x] = c.r; gG[HEIGHT-1][x] = c.g; gB[HEIGHT-1][x] = c.b;
            idx++;
        }
        // Left edge: bottom-1 to top+1
        for (int y = HEIGHT - 2; y > 0; y--) {
            ColorF c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[y][0] = c.r; gG[y][0] = c.g; gB[y][0] = c.b;
            idx++;
        }
    }

} // namespace colorTrails
