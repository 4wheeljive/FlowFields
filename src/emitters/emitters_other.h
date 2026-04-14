#pragma once

// ═══════════════════════════════════════════════════════════════════
//  OTHER (SHORT) EMITTERS — emitters_other.h
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"
#include "modulators.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    // ═══════════════════════════════════════════════════════════════════
    //  AUDIO DOTS
    // ═══════════════════════════════════════════════════════════════════

    const myAudio::AudioFrame* cFrame = nullptr;

    inline void getAudio(myAudio::binConfig& b) {
        b.busBased = true;
        cFrame = &myAudio::updateAudioFrame(b);
    }

    struct AudioDotsParams {
        uint8_t dotDiam = 1.0f;
        //uint8_t numActiveTimers = 0;
    };

    AudioDotsParams audioDots;

    static void emitAudioDots() {

        if (audioEnabled){
            myAudio::binConfig& b = maxBins ? myAudio::bin32 : myAudio::bin16;
            getAudio(b);
        }

        // On each new beat, spawn a dot at a random grid position
        if (myAudio::busC.newBeat) {
            float cx = random8(0, WIDTH  - 1) + random8() / 255.0f;
            float cy = random8(0, HEIGHT - 1) + random8() / 255.0f;
            // Color from rainbow based on current time
            ColorF c = rainbow(t, colorShift, random8() / 255.0f);
            drawDot(cx, cy, audioDots.dotDiam * 5, c.r, c.g, c.b);
        }
    
        if (myAudio::busB.newBeat) {
            float cx = random8(0, WIDTH  - 1) + random8() / 255.0f;
            float cy = random8(0, HEIGHT - 1) + random8() / 255.0f;
            // Color from rainbow based on current time
            ColorF c = rainbow(t, colorShift, random8() / 255.0f);
            drawDot(cx, cy, audioDots.dotDiam, c.r, c.g, c.b);
        }
    }

    
    // ═══════════════════════════════════════════════════════════════════
    //  RAINBOW BORDER
    // ═══════════════════════════════════════════════════════════════════

    //struct BorderRectParams {};
    //BorderRectParams borderRect;

    static void emitRainbowBorder() {
        const int total = 2 * (WIDTH + HEIGHT) - 4;
        int idx = 0;
        // Top edge: left to right
        for (int x = 0; x < WIDTH; x++) {
            ColorF c = rainbow(t, colorShift, (float)idx / total);
            gR[0][x] = c.r; gG[0][x] = c.g; gB[0][x] = c.b;
            idx++;
        }
        // Right edge: top+1 to bottom
        for (int y = 1; y < HEIGHT; y++) {
            ColorF c = rainbow(t, colorShift, (float)idx / total);
            gR[y][WIDTH-1] = c.r; gG[y][WIDTH-1] = c.g; gB[y][WIDTH-1] = c.b;
            idx++;
        }
        // Bottom edge: right-1 to left
        for (int x = WIDTH - 2; x >= 0; x--) {
            ColorF c = rainbow(t, colorShift, (float)idx / total);
            gR[HEIGHT-1][x] = c.r; gG[HEIGHT-1][x] = c.g; gB[HEIGHT-1][x] = c.b;
            idx++;
        }
        // Left edge: bottom-1 to top+1
        for (int y = HEIGHT - 2; y > 0; y--) {
            ColorF c = rainbow(t, colorShift, (float)idx / total);
            gR[y][0] = c.r; gG[y][0] = c.g; gB[y][0] = c.b;
            idx++;
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
