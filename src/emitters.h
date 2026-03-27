#pragma once

// ═══════════════════════════════════════════════════════════════════
//  EMITTERS — emitters.h
// ═══════════════════════════════════════════════════════════════════

#include "colorTrailsTypes.h"
#include "modulators.h"

namespace colorTrails {

    // ═══════════════════════════════════════════════════════════════════
    //  ORBITAL DOTS
    // ═══════════════════════════════════════════════════════════════════

    OrbitalDotsParams orbitalDots = {
        .numDots    = 3,
        .orbitSpeed = 2.0f,
        .modOrbitSpeed = {0, 1.0f, 1.0f},       // modTimer, modRate, modLevel                 
        .dotDiam    = 1.5f,
        .orbitDiam  = MIN_DIMENSION * 0.3f, 
        .modOrbitDiam = {1, 1.0f, 1.0f}         // modTimer, modRate, modLevel
    };

    static void emitOrbitalDots(float t) {
        static float orbitAngle = 0.0f;
        static unsigned long lastOrbitMs = 0;
        unsigned long now = fl::millis();
        float dt = (now - lastOrbitMs) * 0.001f;
        lastOrbitMs = now;

        timings.ratio[0] = 0.00006f * orbitalDots.modOrbitSpeed.modRate;
        timings.ratio[1] = 0.0005f * orbitalDots.modOrbitDiam.modRate;

        calculate_modulators(timings);

        float lSpeed = orbitalDots.modOrbitSpeed.modLevel;
        float currentSpeed = orbitalDots.orbitSpeed * ((1.0f - lSpeed) + lSpeed * move.directional_noise[0]);
        orbitAngle += currentSpeed * dt;

        float modDiam = 0.5f + 0.5f * noiseX.noise(move.linear[1]);
        float lDiam = orbitalDots.modOrbitDiam.modLevel;
        float swing = lDiam * 6.0f * (modDiam - 0.5f);

        float fNumDots = static_cast<float>(orbitalDots.numDots);
        float ocx  = WIDTH  * 0.5f - 0.5f;
        float ocy  = HEIGHT * 0.5f - 0.5f;
        float orad = fmaxf(orbitalDots.orbitDiam * (1.0f + swing), orbitalDots.dotDiam);
        for (int i = 0; i < orbitalDots.numDots; i++) {
            float a  = orbitAngle + i * (2.0f * CT_PI / fNumDots);
            float cx = ocx + fl::cosf(a) * orad;
            float cy = ocy + fl::sinf(a) * orad;
            ColorF c = rainbow(t, vizConfig.colorShift, i / fNumDots);
            drawDot(cx, cy, orbitalDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  SWARMING DOTS
    // ═══════════════════════════════════════════════════════════════════

    SwarmingDotsParams swarmingDots = {
        .numDots     = 3,
        .swarmSpeed  = 0.5f,
        .swarmSpread = 0.5f,
        .modSwarmSpread = {10, 1.0f, 1.0f},       // modTimer, modRate, modLevel 
        .dotDiam     = 1.5f,
    };

    // Variable number of dots moving in a loose shifting group.
    // Uses calculate_modulators() with 2 timers per dot (X and Y).
    // swarmSpread controls grouping (0 = clustered, 1 = independent, >1 = wide).
    // Max 5 dots (num_timers=10, 2 timers per dot).
    static void emitSwarmingDots(float t) {

        uint8_t n = swarmingDots.numDots;
        float fNumDots = static_cast<float>(n);

        timings.ratio[10] = 0.00055f * swarmingDots.modSwarmSpread.modRate;
        //timings.ratio[11] = 0.00045f * swarmingDots.modSwarmSpeed.modRate;
        //timings.offset[11] = 500.f;

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
        float modSpread = fl::map_range_clamped<float, float>(noiseX.noise(move.linear[10]), -0.5f, 0.5f, 0.0f, 1.0f);
        float spread = swarmingDots.swarmSpread + (modSpread * swarmingDots.modSwarmSpread.modLevel);

        for (int d = 0; d < n; d++) {
            float sx = cenX + spread * (dotX[d] - cenX);
            float sy = cenY + spread * (dotY[d] - cenY);

            float cx = (WIDTH  - 1) * 0.5f * (1.0f + sx);
            float cy = (HEIGHT - 1) * 0.5f * (1.0f + sy);

            ColorF c = rainbow(t, vizConfig.colorShift, d / fNumDots);
            drawDot(cx, cy, swarmingDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  AUDIO DOTS
    // ═══════════════════════════════════════════════════════════════════

    const myAudio::AudioFrame* cFrame = nullptr;

    inline void getAudio(myAudio::binConfig& b) {
        b.busBased = true;
        cFrame = &myAudio::updateAudioFrame(b);
    }

    AudioDotsParams audioDots = {
        .dotDiam = 3.0f,
    };

    static void emitAudioDots(float t) {

        if (audioEnabled){
            myAudio::binConfig& b = maxBins ? myAudio::bin32 : myAudio::bin16;
            getAudio(b);
        }

        // On each new beat, spawn a dot at a random grid position
        if (myAudio::busC.newBeat) {
            float cx = random8(0, WIDTH  - 1) + random8() / 255.0f;
            float cy = random8(0, HEIGHT - 1) + random8() / 255.0f;
            // Color from rainbow based on current time
            ColorF c = rainbow(t, vizConfig.colorShift, random8() / 255.0f);
            drawDot(cx, cy, audioDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  LISSAJOUS LINE
    // ═══════════════════════════════════════════════════════════════════

    LissajousParams lissajous = {
        .lineSpeed = 0.35f,
        .lineAmp   = (MIN_DIMENSION - 4) * 0.75f,
    };

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

    // ═══════════════════════════════════════════════════════════════════
    //  RAINBOW BORDER
    // ═══════════════════════════════════════════════════════════════════

    BorderRectParams borderRect;

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
