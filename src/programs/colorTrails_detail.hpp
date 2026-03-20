#pragma once

//=====================================================================================
//
//  colortrails began with a FastLED Reddit post by u/StefanPetrick:
//  https://www.reddit.com/r/FastLED/comments/1rny5j3/i_used_codex_for_the_first_time/
//
//  I had Claude help me port it to a C++ Arduino/FastLED-friendly sketch and then
//  (2) implement that as a new "colorTrails" program in my AuroraPortal playground:
//  https://github.com/4wheeljive/AuroraPortal
//
//  As Stefan has shared subsequent ideas, I've been implementing them in colorTrails.
//
//  It quickly became clear that we were going to want to do things with colorTrails
//  that would be difficult if it was structured as just one of AuroraPortal's dozen
//  or so visualizer programs. So I cloned my AuroraPortal repo to this project,
//  stripped away all of the other programs, and have started redoing the architecture,
//  from the C++ core out to the web UI, to best keep up with Stefan's fount of ideas.
//
//=====================================================================================

#include "bleControl.h"
#include "colorTrailsTypes.h"
#include "flow_noise.h"
#include "flow_fromCenter.h"
#include "flow_directional.h"

namespace colorTrails {

    // ═══════════════════════════════════════════════════════════════════
    //  EMITTERS
    // ═══════════════════════════════════════════════════════════════════

    // Orbiting dots — reads from `orbital`
    static void emitOrbitalDots(float t) {
        float ocx  = WIDTH  * 0.5f - 0.5f;
        float ocy  = HEIGHT * 0.5f - 0.5f;
        float orad = orbitalDots.orbitDiam * 0.8f;
        float base = t * orbitalDots.orbitSpeed;
        for (int i = 0; i < 3; i++) {
            float a  = base + i * (2.0f * CT_PI / 3.0f);
            float cx = ocx + fl::cosf(a) * orad;
            float cy = ocy + fl::sinf(a) * orad;
            CRGB c = rainbow(t, vizConfig.colorShift, i / 3.0f);
            drawDot(cx, cy, orbitalDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // Swarming dots — 3 dots that move in a loose shifting group.
    // Uses calculate_modulators() with 6 timers: each dot gets a dedicated
    // X modulator (even indices) and Y modulator (odd indices).
    // swarmSpread controls grouping (0 = clustered, 1 = independent, >1 = wide).
    static void emitSwarmingDots(float t) {
        float spd = swarmingDots.swarmSpeed;

        // 6 timers: [0]=dot0.X, [1]=dot0.Y, [2]=dot1.X, [3]=dot1.Y, [4]=dot2.X, [5]=dot2.Y
        // Similar ratios keep dots moving at comparable speeds;
        // irrational relationships prevent exact repetition.
        static const float baseRatios[6] = {
            0.00173f, 0.00131f,   // dot 0: X, Y
            0.00197f, 0.00149f,   // dot 1: X, Y
            0.00211f, 0.00113f    // dot 2: X, Y
        };
        // Offsets: each dot's Y is ~quarter-period ahead of its X
        // to create elliptical paths instead of diagonal lines
        static const float baseOffsets[6] = {
               0.0f,  900.0f,    // dot 0
             600.0f, 1700.0f,    // dot 1
            1300.0f, 2400.0f     // dot 2
        };

        for (int i = 0; i < 6; i++) {
            timings.ratio[i]  = baseRatios[i] * spd;
            timings.offset[i] = baseOffsets[i];
        }

        calculate_modulators(timings);

        // Each dot: dedicated X (even index) and Y (odd index)
        float dotX[3], dotY[3];
        for (int d = 0; d < 3; d++) {
            dotX[d] = move.directional[d * 2];
            dotY[d] = move.directional[d * 2 + 1];
        }

        // Group center
        float cenX = (dotX[0] + dotX[1] + dotX[2]) / 3.0f;
        float cenY = (dotY[0] + dotY[1] + dotY[2]) / 3.0f;

        // Blend each dot between center and its own position via swarmSpread
        float spread = swarmingDots.swarmSpread;
        for (int d = 0; d < 3; d++) {
            float sx = cenX + spread * (dotX[d] - cenX);
            float sy = cenY + spread * (dotY[d] - cenY);

            float cx = (WIDTH  - 1) * 0.5f * (1.0f + sx);
            float cy = (HEIGHT - 1) * 0.5f * (1.0f + sy);

            CRGB c = rainbow(t, vizConfig.colorShift, d / 3.0f);
            drawDot(cx, cy, swarmingDots.dotDiam, c.r, c.g, c.b);
        }
    }

    // Lissajous line — reads from `lissajous`
    static void emitLissajousLine(float t) {
        const float c = (MIN_DIMENSION - 1) * 0.5f;
        float s = lissajous.lineSpeed;
        const float amp = lissajous.lineAmp;

        float lx1 = c + (amp + 1.5f) * fl::sinf(t * s * 1.13f + 0.20f);
        float ly1 = c + (amp + 0.5f) * fl::sinf(t * s * 1.71f + 1.30f);
        float lx2 = c + (amp + 2.0f) * fl::sinf(t * s * 1.89f + 2.20f);
        float ly2 = c + (amp + 1.0f) * fl::sinf(t * s * 1.37f + 0.70f);

        drawAASubpixelLine(lx1, ly1, lx2, ly2, t, vizConfig.colorShift);
        CRGB ca = rainbow(t, vizConfig.colorShift, 0.0f);
        CRGB cb = rainbow(t, vizConfig.colorShift, 1.0f);
        drawAAEndpointDisc(lx1, ly1, ca.r, ca.g, ca.b, 0.85f);
        drawAAEndpointDisc(lx2, ly2, cb.r, cb.g, cb.b, 0.85f);
    }

    // Rainbow border rectangle — reads from `borderRect`
    static void emitRainbowBorder(float t) {
        const int total = 2 * (WIDTH + HEIGHT) - 4;
        int idx = 0;
        // Top edge: left to right
        for (int x = 0; x < WIDTH; x++) {
            CRGB c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[0][x] = c.r; gG[0][x] = c.g; gB[0][x] = c.b;
            idx++;
        }
        // Right edge: top+1 to bottom
        for (int y = 1; y < HEIGHT; y++) {
            CRGB c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[y][WIDTH-1] = c.r; gG[y][WIDTH-1] = c.g; gB[y][WIDTH-1] = c.b;
            idx++;
        }
        // Bottom edge: right-1 to left
        for (int x = WIDTH - 2; x >= 0; x--) {
            CRGB c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[HEIGHT-1][x] = c.r; gG[HEIGHT-1][x] = c.g; gB[HEIGHT-1][x] = c.b;
            idx++;
        }
        // Left edge: bottom-1 to top+1
        for (int y = HEIGHT - 2; y > 0; y--) {
            CRGB c = rainbow(t, vizConfig.colorShift, (float)idx / total);
            gR[y][0] = c.r; gG[y][0] = c.g; gB[y][0] = c.b;
            idx++;
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  DISPATCH TABLES
    // ═══════════════════════════════════════════════════════════════════

    const EmitterFn EMITTER_RUN[] = {
        emitOrbitalDots,
        emitSwarmingDots,
        emitLissajousLine,
        emitRainbowBorder,
    };

    const FlowPrepFn FLOW_PREPARE[] = {
        noiseFlowPrepare,
        fromCenterPrepare,
        directionalPrepare,  
    };

    const FlowAdvectFn FLOW_ADVECT[] = {
        noiseFlowAdvect,  
        fromCenterAdvect,
        directionalAdvect,
    };

    constexpr uint8_t FLOW_DISPATCH_COUNT = sizeof(FLOW_PREPARE) / sizeof(FLOW_PREPARE[0]);

    // ═══════════════════════════════════════════════════════════════════
    //  INIT & MAIN LOOP
    // ═══════════════════════════════════════════════════════════════════

    void initColorTrails(uint16_t (*xy_func)(uint8_t, uint8_t)) {
        colorTrailsInstance = true;
        xyFunc = xy_func;

        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                gR[y][x] = gG[y][x] = gB[y][x] = 0.0f;

        t0 = fl::millis();
        lastFrameMs = t0;   
        lastEmitter = 255;
        lastFlow = 255;

        noiseX.init(42);
        noiseY.init(1337);
        noise2X.init(42);
        noise2Y.init(1337);
        ampVarX.init(101);
        ampVarY.init(202);

        timings = timers();
        move = modulators();

    }

    // ═══════════════════════════════════════════════════════════════════
    //  cVAR BRIDGE
    // ═══════════════════════════════════════════════════════════════════

    // Push flow field struct defaults into cVars (called on flow field change)
    static void pushFlowDefaultsToCVars() {
        if (vizConfig.flow == FLOW_NOISE) {
            noiseFlow = NoiseFlowParams{};
            cXSpeed = noiseFlow.xSpeed;
            cYSpeed = noiseFlow.ySpeed;
            cXAmp = noiseFlow.xAmp;
            cYAmp = noiseFlow.yAmp;
            cXFreq = noiseFlow.xFreq;
            cYFreq = noiseFlow.yFreq;
            cXShift = noiseFlow.xShift;
            cYShift = noiseFlow.yShift;
            ampMod = AmpModParams{};
            cVariationIntensity = ampMod.intensity;
            cVariationSpeed = ampMod.speed;
            cModulateAmp = ampMod.active ? 1 : 0;
        } else if (vizConfig.flow == FLOW_FROMCENTER) {
            fromCenter = FromCenterParams{};
            cRadialStep = fromCenter.radialStep;
            cBlendFactor = fromCenter.blendFactor;
        }
    }

    // Copy cVars into flow field + modulator structs (called every frame)
    static void syncFlowFromCVars() {
        // Noise flow
        noiseFlow.xSpeed = cXSpeed;
        noiseFlow.ySpeed  = cYSpeed;
        noiseFlow.xAmp = cXAmp;
        noiseFlow.yAmp = cYAmp;
        noiseFlow.xFreq = cXFreq;
        noiseFlow.yFreq = cYFreq;
        noiseFlow.xShift = cXShift;
        noiseFlow.yShift = cYShift;
        ampMod.intensity = cVariationIntensity;
        ampMod.speed = cVariationSpeed;
        ampMod.active = (cModulateAmp > 0);
        vizConfig.useAmpMod = ampMod.active;
        // From-center flow
        fromCenter.radialStep = cRadialStep;
        fromCenter.blendFactor = cBlendFactor;
    }

    // Push emitter + universal defaults into cVars (called on emitter/mode change)
    static void pushDefaultsToCVars() {
        // Universal
        cPersistence = vizConfig.persistence;
        cColorShift = vizConfig.colorShift;
        cFlipY = vizConfig.flipY;
        cFlipX = vizConfig.flipX;
        // Emitter: orbitalDots
        cOrbitSpeed = orbitalDots.orbitSpeed;
        cDotDiam = orbitalDots.dotDiam;
        cOrbitDiam = orbitalDots.orbitDiam;
        // Emitter: swarmingDots
        cSwarmSpeed = swarmingDots.swarmSpeed;
        cSwarmSpread = swarmingDots.swarmSpread;
        // Emitter: lissajous / borderRect
        cLineSpeed = lissajous.lineSpeed;
        cLineAmp = lissajous.lineAmp;
    }

    // Read cVars into component structs (called every frame)
    static void syncFromCVars() {
        vizConfig.persistence = cPersistence;
        vizConfig.colorShift = cColorShift;
        vizConfig.flipY = cFlipY;
        vizConfig.flipX = cFlipX;
        orbitalDots.orbitSpeed = cOrbitSpeed;
        orbitalDots.dotDiam  = cDotDiam;
        orbitalDots.orbitDiam = cOrbitDiam;
        swarmingDots.swarmSpeed = cSwarmSpeed;
        swarmingDots.swarmSpread = cSwarmSpread;
        swarmingDots.dotDiam = cDotDiam;
        lissajous.lineSpeed = cLineSpeed;
        lissajous.lineAmp = cLineAmp;
        // Flow field + modulator
        syncFlowFromCVars();
    }

    void runColorTrails() {
        unsigned long now = fl::millis();
        float dt = (now - lastFrameMs) * 0.001f;
        lastFrameMs = now;
        float t = (now - t0) * 0.001f;

        // Update emitter if changed
        if (EMITTER < EMITTER_COUNT && EMITTER != lastEmitter) {
            vizConfig.emitter = (Emitter)EMITTER;
            lastEmitter = EMITTER;
            pushDefaultsToCVars();
            sendEmitterState();
        }

        // Update flow field if changed
        if (FLOW < FLOW_COUNT && FLOW != lastFlow) {
            vizConfig.flow = (Flow)FLOW;
            lastFlow = FLOW;
            pushFlowDefaultsToCVars();
            sendFlowState();
        }

        // Sync UI-controlled values into component structs
        syncFromCVars();

        // 1. Flow field: prepare (build X and Y profiles)
        FLOW_PREPARE[vizConfig.flow](t);

        // 2. Apply axis flips
        if (vizConfig.flipX) flipAxisX();
        if (vizConfig.flipY) flipAxisY();
        
        // 3. Emitter: inject color onto grid
        EMITTER_RUN[vizConfig.emitter](t);

        // 4. Flow field: advect + fade
        FLOW_ADVECT[vizConfig.flow](dt);

        // 4. Copy float grid to LED array
        for (uint8_t y = 0; y < HEIGHT; y++) {
            for (uint8_t x = 0; x < WIDTH; x++) {
                uint16_t idx = xyFunc(x, y);
                leds[idx].r = f2u8(gR[y][x]);
                leds[idx].g = f2u8(gG[y][x]);
                leds[idx].b = f2u8(gB[y][x]);
            }
        }
    }

} // namespace colorTrails
