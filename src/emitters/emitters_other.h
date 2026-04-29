#pragma once

// ═══════════════════════════════════════════════════════════════════
//  OTHER (SHORT) EMITTERS — emitters_other.h
// ═══════════════════════════════════════════════════════════════════

#include "FlowFieldsEngine.h"

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
            float cx = random8(0, g_engine->_width  - 1) + random8() / 255.0f;
            float cy = random8(0, g_engine->_height - 1) + random8() / 255.0f;
            // Color from rainbow based on current time
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, random8() / 255.0f);
            g_engine->drawDot(cx, cy, audioDots.dotDiam * 5, c.r, c.g, c.b);
        }

        if (myAudio::busB.newBeat) {
            float cx = random8(0, g_engine->_width  - 1) + random8() / 255.0f;
            float cy = random8(0, g_engine->_height - 1) + random8() / 255.0f;
            // Color from rainbow based on current time
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, random8() / 255.0f);
            g_engine->drawDot(cx, cy, audioDots.dotDiam, c.r, c.g, c.b);
        }
    }


    // ═══════════════════════════════════════════════════════════════════
    //  RAINBOW BORDER
    // ═══════════════════════════════════════════════════════════════════

    //struct BorderRectParams {};
    //BorderRectParams borderRect;

    static void emitRainbowBorder() {
        const int total = 2 * (g_engine->_width + g_engine->_height) - 4;
        int idx = 0;
        // Top edge: left to right
        for (int x = 0; x < g_engine->_width; x++) {
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, (float)idx / total);
            g_engine->gR[0][x] = c.r; g_engine->gG[0][x] = c.g; g_engine->gB[0][x] = c.b;
            idx++;
        }
        // Right edge: top+1 to bottom
        for (int y = 1; y < g_engine->_height; y++) {
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, (float)idx / total);
            g_engine->gR[y][g_engine->_width-1] = c.r; g_engine->gG[y][g_engine->_width-1] = c.g; g_engine->gB[y][g_engine->_width-1] = c.b;
            idx++;
        }
        // Bottom edge: right-1 to left
        for (int x = g_engine->_width - 2; x >= 0; x--) {
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, (float)idx / total);
            g_engine->gR[g_engine->_height-1][x] = c.r; g_engine->gG[g_engine->_height-1][x] = c.g; g_engine->gB[g_engine->_height-1][x] = c.b;
            idx++;
        }
        // Left edge: bottom-1 to top+1
        for (int y = g_engine->_height - 2; y > 0; y--) {
            ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, (float)idx / total);
            g_engine->gR[y][0] = c.r; g_engine->gG[y][0] = c.g; g_engine->gB[y][0] = c.b;
            idx++;
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
