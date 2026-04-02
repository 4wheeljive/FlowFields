#pragma once

//=====================================================================================
//  NOTE: Flow Fields was initially called Color Trails. 
//  It will take a little time to get everything renamed internally.  
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
#include "flowFieldsTypes.h"
#include "flows/flow_noise.h"
#include "flows/flow_fromCenter.h"
#include "flows/flow_directional.h"
#include "flows/flow_rings.h"
#include "emitters/emitters_other.h"
#include "emitters/emitter_orbitalDots.h"
#include "emitters/emitter_swarmingDots.h"
#include "emitters/emitter_lissajousLine.h"
#include "emitters/emitter_noiseKaleido.h"
#include "emitters/emitter_cube.h"
#include "flows/flow_spiral.h"
#include "modulators.h"

namespace flowFields {

    // ═══════════════════════════════════════════════════════════════════
    //  DISPATCH TABLES
    // ═══════════════════════════════════════════════════════════════════

    const EmitterFn EMITTER_RUN[] = {
        emitOrbitalDots,
        emitSwarmingDots,
        emitAudioDots,
        emitLissajousLine,
        emitRainbowBorder,
        emitNoiseKaleido,
        emitCube,
    };

    const FlowPrepFn FLOW_PREPARE[] = {
        noiseFlowPrepare,
        fromCenterPrepare,
        directionalPrepare,
        ringFlowPrepare,
        spiralPrepare,
    };

    const FlowAdvectFn FLOW_ADVECT[] = {
        noiseFlowAdvect,
        fromCenterAdvect,
        directionalAdvect,
        ringFlowAdvect,
        spiralAdvect,
    };

    constexpr uint8_t FLOW_DISPATCH_COUNT = sizeof(FLOW_PREPARE) / sizeof(FLOW_PREPARE[0]);

    // ═══════════════════════════════════════════════════════════════════
    //  INIT & MAIN LOOP
    // ═══════════════════════════════════════════════════════════════════

    void initFlowFields(uint16_t (*xy_func)(uint8_t, uint8_t)) {
        flowFieldsInstance = true;
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
        kaleidoNoise.init(7331);

        timings = timers();
        move = modulators();

    }

    // ═══════════════════════════════════════════════════════════════════
    //  cVAR BRIDGE
    // ═══════════════════════════════════════════════════════════════════

    // Push flow field struct defaults into cVars (called on flow field change)
    static void pushFlowDefaultsToCVars() {
        switch (vizConfig.flow) {
            case FLOW_NOISE: {
                noiseFlow = NoiseFlowParams{};
                cXSpeed = noiseFlow.xSpeed;
                cYSpeed = noiseFlow.ySpeed;
                cXAmp = noiseFlow.xAmp;
                cYAmp = noiseFlow.yAmp;
                cXFreq = noiseFlow.xFreq;
                cYFreq = noiseFlow.yFreq;
                cXShift = noiseFlow.xShift;
                cYShift = noiseFlow.yShift;
                cModAmpRate = noiseFlow.modAmp.modRate;
                cModAmpLevel = noiseFlow.modAmp.modLevel;
                cModSpeedRate = noiseFlow.modSpeed.modRate;
                cModSpeedLevel = noiseFlow.modSpeed.modLevel;
                cModShiftRate = noiseFlow.modShift.modRate;
                cModShiftLevel = noiseFlow.modShift.modLevel;
                break;
            }
            case FLOW_FROMCENTER: {
                fromCenter = FromCenterParams{};
                cRadialStep = fromCenter.radialStep;
                cBlendFactor = fromCenter.blendFactor;
                break;
            }
            case FLOW_DIRECTIONAL: {
                directional = DirectionalParams{};
                cWindStep = directional.windStep;
                cBlendFactor = directional.blendFactor;
                cRotateSpeed = directional.rotateSpeed;
                cWaveAmp = directional.waveAmp;
                cWaveFreq = directional.waveFreq;
                cWaveSpeed = directional.waveSpeed;
                break;
            }
            case FLOW_RINGS: {
                ringFlow = RingFlowParams{};
                cInnerSwirl = ringFlow.innerSwirl;
                cOuterSwirl = ringFlow.outerSwirl;
                cMidDrift = ringFlow.midDrift;
                cModBreatheRate = ringFlow.modBreathe.modRate;
                cModBreatheLevel = ringFlow.modBreathe.modLevel;
                break;
            }
            case FLOW_SPIRAL: {
                spiral = SpiralParams{};
                cAngularStep = spiral.angularStep;
                cRadialStep = spiral.radialStep;
                cBlendFactor = spiral.blendFactor;
                cSpiralOutward = spiral.outward;
                break;
            }
            default: break;
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
        noiseFlow.modAmp.modRate = cModAmpRate;
        noiseFlow.modAmp.modLevel = cModAmpLevel;
        noiseFlow.modSpeed.modRate = cModSpeedRate;
        noiseFlow.modSpeed.modLevel = cModSpeedLevel;
        noiseFlow.modShift.modRate = cModShiftRate;
        noiseFlow.modShift.modLevel = cModShiftLevel;
        // From-center flow
        fromCenter.radialStep = cRadialStep;
        fromCenter.blendFactor = cBlendFactor;
        // Directional flow
        directional.windStep = cWindStep;
        directional.blendFactor = cBlendFactor;
        directional.rotateSpeed = cRotateSpeed;
        directional.waveAmp = cWaveAmp;
        directional.waveFreq = cWaveFreq;
        directional.waveSpeed = cWaveSpeed;
        // Ring flow
        ringFlow.innerSwirl = cInnerSwirl;
        ringFlow.outerSwirl = cOuterSwirl;
        ringFlow.midDrift = cMidDrift;
        ringFlow.modBreathe.modRate = cModBreatheRate;
        ringFlow.modBreathe.modLevel = cModBreatheLevel;
        // Spiral flow
        spiral.angularStep = cAngularStep;
        spiral.radialStep = cRadialStep;
        spiral.blendFactor = cBlendFactor;
        spiral.outward = cSpiralOutward;
    }

    // Push emitter + universal defaults into cVars (called on emitter/mode change)
    static void pushDefaultsToCVars() {
        // Universal
        cPersistence = vizConfig.persistence;
        cColorShift = vizConfig.colorShift;
        // Emitter: orbitalDots
        cNumDots = orbitalDots.numDots;
        cOrbitSpeed = orbitalDots.orbitSpeed;
        cDotDiam = orbitalDots.dotDiam;
        cOrbitDiam = orbitalDots.orbitDiam;
        cModOrbitSpeedRate = orbitalDots.modOrbitSpeed.modRate;
        cModOrbitSpeedLevel = orbitalDots.modOrbitSpeed.modLevel;
        cModOrbitDiamRate = orbitalDots.modOrbitDiam.modRate;
        cModOrbitDiamLevel = orbitalDots.modOrbitDiam.modLevel;
        // Emitter: swarmingDots
        cSwarmSpeed = swarmingDots.swarmSpeed;
        cSwarmSpread = swarmingDots.swarmSpread;
        cModSwarmSpeedRate = swarmingDots.modSwarmSpeed.modRate;
        cModSwarmSpeedLevel = swarmingDots.modSwarmSpeed.modLevel;
        cModSwarmSpreadRate = swarmingDots.modSwarmSpread.modRate;
        cModSwarmSpreadLevel = swarmingDots.modSwarmSpread.modLevel;
        // Emitter: lissajous / borderRect
        cLineSpeed = lissajous.lineSpeed;
        cLineAmp = lissajous.lineAmp;
        cModLineSpeedRate = lissajous.modLineSpeed.modRate;
        cModLineSpeedLevel = lissajous.modLineSpeed.modLevel;
        // Emitter: noiseKaleido
        cDriftSpeed = noiseKaleido.driftSpeed;
        cNoiseScale = noiseKaleido.noiseScale;
        cNoiseBand = noiseKaleido.noiseBand;
        cKaleidoGamma = noiseKaleido.kaleidoGamma;
        // Emitter: cube
        cScale = cube.scale;
        cAngleRateX = cube.angleRate[0];
        cAngleRateY = cube.angleRate[1];
        cAngleRateZ = cube.angleRate[2];
        cAngleFreezeX = cube.angleFreeze[0];
        cAngleFreezeY = cube.angleFreeze[1];
        cAngleFreezeZ = cube.angleFreeze[2];
    }

    // Read cVars into component structs (called every frame)
    static void syncFromCVars() {
        vizConfig.persistence = cPersistence;
        vizConfig.colorShift = cColorShift;
        orbitalDots.numDots = cNumDots;
        orbitalDots.orbitSpeed = cOrbitSpeed;
        orbitalDots.dotDiam  = cDotDiam;
        orbitalDots.orbitDiam = cOrbitDiam;
        orbitalDots.modOrbitSpeed.modRate = cModOrbitSpeedRate;
        orbitalDots.modOrbitSpeed.modLevel = cModOrbitSpeedLevel;
        orbitalDots.modOrbitDiam.modRate = cModOrbitDiamRate;
        orbitalDots.modOrbitDiam.modLevel = cModOrbitDiamLevel;
        swarmingDots.numDots = cNumDots;
        swarmingDots.swarmSpeed = cSwarmSpeed;
        swarmingDots.swarmSpread = cSwarmSpread;
        swarmingDots.modSwarmSpeed.modRate = cModSwarmSpeedRate;
        swarmingDots.modSwarmSpeed.modLevel = cModSwarmSpeedLevel;
        swarmingDots.modSwarmSpread.modRate = cModSwarmSpreadRate;
        swarmingDots.modSwarmSpread.modLevel = cModSwarmSpreadLevel;
        swarmingDots.dotDiam = cDotDiam;
        lissajous.lineSpeed = cLineSpeed;
        lissajous.lineAmp = cLineAmp;
        lissajous.modLineSpeed.modRate = cModLineSpeedRate;
        lissajous.modLineSpeed.modLevel = cModLineSpeedLevel;
        noiseKaleido.driftSpeed = cDriftSpeed;
        noiseKaleido.noiseScale = cNoiseScale;
        noiseKaleido.noiseBand = cNoiseBand;
        noiseKaleido.kaleidoGamma = cKaleidoGamma;
        cube.scale = cScale;
        cube.angleRate[0] = cAngleRateX;
        cube.angleRate[1] = cAngleRateY;
        cube.angleRate[2] = cAngleRateZ;
        cube.angleFreeze[0] = cAngleFreezeX;
        cube.angleFreeze[1] = cAngleFreezeY;
        cube.angleFreeze[2] = cAngleFreezeZ;
        // Flow field + modulator
        syncFlowFromCVars();
    }

    void runFlowFields() {
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

        // 2. Emitter: inject color onto grid
        EMITTER_RUN[vizConfig.emitter](t);

        // 3. Flow field: advect + fade
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

} // namespace flowFields
