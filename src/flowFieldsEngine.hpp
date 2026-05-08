#pragma once

#include "parameterSchema.h"
#include "flowFieldsTypes.h"
#include "flows/flow_noise.h"
#include "flows/flow_radial.h"
#include "flows/flow_directional.h"
#include "flows/flow_rings.h"
#include "flows/flow_spiral.h"
#include "flows/flow_fluid.h"
#include "emitters/emitters_other.h"
#include "emitters/emitter_orbitalDots.h"
#include "emitters/emitter_swarmingDots.h"
#include "emitters/emitter_lissajousLine.h"
#include "emitters/emitter_noiseKaleido.h"
#include "emitters/emitter_cube.h"
#include "emitters/emitter_fluidJet.h"
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
        emitFluidJet,
    };

    const FlowPrepFn FLOW_PREPARE[] = {
        noiseFlowPrepare,
        radialPrepare,
        directionalPrepare,
        ringFlowPrepare,
        spiralPrepare,
        fluidPrepare,
    };

    const FlowAdvectFn FLOW_ADVECT[] = {
        noiseFlowAdvect,
        radialAdvect,
        directionalAdvect,
        ringFlowAdvect,
        spiralAdvect,
        fluidAdvect,
    };

    constexpr uint8_t FLOW_DISPATCH_COUNT = sizeof(FLOW_PREPARE) / sizeof(FLOW_PREPARE[0]);

    // ═══════════════════════════════════════════════════════════════════
    //  INIT & MAIN LOOP
    // ═══════════════════════════════════════════════════════════════════

    void initFlowFields(uint32_t (*xy_func)(uint16_t, uint16_t)) {
        allocGrids(WIDTH, HEIGHT);
        allocFluidGrids(WIDTH, HEIGHT);

        flowFieldsInstance = true;
        xyFunc = xy_func;

        lastFrameMs = fl::millis();
        lastEmitter = 255;
        lastFlow = 255;

        noiseX.init(42);
        noiseY.init(1337);
        noise2X.init(42);
        noise2Y.init(1337);
        kaleidoNoise.init(7331);

        timings = timers();
        move = modulators();
        startingPalette();

    void teardownFlowFields() {
        freeGrids();
        freeFluidGrids();
        flowFieldsInstance = false;
    }

    // Nearest-neighbour scale pixel content to newW×newH, reallocate all grids.
    // Caller is responsible for updating WIDTH / HEIGHT / MIN_DIMENSION afterward.
    void resizeFlowFields(int newW, int newH) {
        if (!gR) return;
        float** nR = allocGrid(newW, newH);
        float** nG = allocGrid(newW, newH);
        float** nB = allocGrid(newW, newH);
        if (!nR || !nG || !nB) { freeGrid(nR); freeGrid(nG); freeGrid(nB); return; }
        float scaleX = (float)WIDTH  / newW;
        float scaleY = (float)HEIGHT / newH;
        for (int y = 0; y < newH; y++) {
            int sy = (int)(y * scaleY);
            if (sy >= HEIGHT) sy = HEIGHT - 1;
            for (int x = 0; x < newW; x++) {
                int sx = (int)(x * scaleX);
                if (sx >= WIDTH) sx = WIDTH - 1;
                nR[y][x] = gR[sy][sx];
                nG[y][x] = gG[sy][sx];
                nB[y][x] = gB[sy][sx];
            }
        }
        freeGrids();
        freeFluidGrids();
        gR = nR; gG = nG; gB = nB;
        tR = allocGrid(newW, newH);
        tG = allocGrid(newW, newH);
        tB = allocGrid(newW, newH);
        xProf = (float*)malloc((size_t)newW * sizeof(float));
        yProf = (float*)malloc((size_t)newH * sizeof(float));
        allocFluidGrids(newW, newH);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  cVAR BRIDGE
    // ═══════════════════════════════════════════════════════════════════

    // Push flow field struct defaults into cVars (called on flow field change)
    static void pushFlowDefaultsToCVars() {
        switch (activeFlow) {
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
            case FLOW_RADIAL: {
                radial = RadialParams{};
                cRadialStep = radial.radialStep;
                cBlendFactor = radial.blendFactor;
                cOutward = radial.outward;
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
                cOutward = spiral.outward;
                cModAngularStepRate = spiral.modAngularStep.modRate;
                cModAngularStepLevel = spiral.modAngularStep.modLevel;
                cModRadialStepRate = spiral.modRadialStep.modRate;
                cModRadialStepLevel = spiral.modRadialStep.modLevel;
                cModBlendFactorRate = spiral.modBlendFactor.modRate;
                cModBlendFactorLevel = spiral.modBlendFactor.modLevel;
                break;
            }
            case FLOW_FLUID: {
                fluid = FluidParams{};
                cViscosity = fluid.viscosity;
                cDiffusion = fluid.diffusion;
                cVelocityDissipation = fluid.velocityDissipation;
                cDyeDissipation = fluid.dyeDissipation;
                cVorticity = fluid.vorticity;
                cGravity = fluid.gravity;
                cSolverIterations = (float)fluid.solverIterations;
                cModVelDissipRate = fluid.modVelDissip.modRate;
                cModVelDissipLevel = fluid.modVelDissip.modLevel;
                cModDyeDissipRate = fluid.modDyeDissip.modRate;
                cModDyeDissipLevel = fluid.modDyeDissip.modLevel;
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
        // Radial flow
        radial.radialStep = cRadialStep;
        radial.blendFactor = cBlendFactor;
        radial.outward = cOutward;
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
        spiral.outward = cOutward;
        spiral.modAngularStep.modRate = cModAngularStepRate;
        spiral.modAngularStep.modLevel = cModAngularStepLevel;
        spiral.modRadialStep.modRate = cModRadialStepRate;
        spiral.modRadialStep.modLevel = cModRadialStepLevel;
        spiral.modBlendFactor.modRate = cModBlendFactorRate;
        spiral.modBlendFactor.modLevel = cModBlendFactorLevel;
        // Fluid flow
        fluid.viscosity = cViscosity;
        fluid.diffusion = cDiffusion;
        fluid.velocityDissipation = cVelocityDissipation;
        fluid.dyeDissipation = cDyeDissipation;
        fluid.vorticity = cVorticity;
        fluid.gravity = cGravity;
        fluid.solverIterations = (uint8_t)cSolverIterations;
        fluid.modVelDissip.modRate = cModVelDissipRate;
        fluid.modVelDissip.modLevel = cModVelDissipLevel;
        fluid.modDyeDissip.modRate = cModDyeDissipRate;
        fluid.modDyeDissip.modLevel = cModDyeDissipLevel;
    }

    // Push emitter + universal defaults into cVars (called on emitter/mode change)
    static void pushDefaultsToCVars() {
        // Universal
        cGlobalSpeed = globalSpeed;
        cPersistence = floorf(persistence);
        cPersistFine = persistence - cPersistence;
        cColorShift = colorShift;
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
        cLineClamp = lissajous.lineClamp;
        cModLineSpeedRate = lissajous.modLineSpeed.modRate;
        cModLineSpeedLevel = lissajous.modLineSpeed.modLevel;
        cModLineAmpRate = lissajous.modLineAmp.modRate;
        cModLineAmpLevel = lissajous.modLineAmp.modLevel;
        // Emitter: noiseKaleido
        cDriftSpeed = noiseKaleido.driftSpeed;
        cNoiseScale = noiseKaleido.noiseScale;
        cNoiseBand = noiseKaleido.noiseBand;
        cKaleidoGamma = noiseKaleido.kaleidoGamma;
        // Emitter: cube
        cScale = cube.scale;
        cRotateSpeedX = cube.rotateSpeed[0];
        cRotateSpeedY = cube.rotateSpeed[1];
        cRotateSpeedZ = cube.rotateSpeed[2];
        cAxisFreezeX = cube.axisFreeze[0];
        cAxisFreezeY = cube.axisFreeze[1];
        cAxisFreezeZ = cube.axisFreeze[2];
        cModScaleRate = cube.modScale.modRate;
        cModScaleLevel = cube.modScale.modLevel;
        cModRotateSpeedXRate = cube.modRotateSpeedX.modRate;
        cModRotateSpeedXLevel = cube.modRotateSpeedX.modLevel;
        cModRotateSpeedYRate = cube.modRotateSpeedY.modRate;
        cModRotateSpeedYLevel = cube.modRotateSpeedY.modLevel;
        cModRotateSpeedZRate = cube.modRotateSpeedZ.modRate;
        cModRotateSpeedZLevel = cube.modRotateSpeedZ.modLevel;
        // Emitter: fluidJet
        cJetDensity = fluidJet.jetDensity;
        cJetForce = fluidJet.jetForce;
        cJetRadius = fluidJet.jetRadius;
        cJetSpread = fluidJet.jetSpread;
        cJetAngle = fluidJet.jetAngle;
        cJetHueSpeed = fluidJet.jetHueSpeed;
        cModJetForceRate = fluidJet.modJetForce.modRate;
        cModJetForceLevel = fluidJet.modJetForce.modLevel;
        cModAngleRate = fluidJet.modAngle.modRate;
        cModAngleLevel = fluidJet.modAngle.modLevel;
    }

    // Read cVars into component structs (called every frame)
    static void syncFromCVars() {
        globalSpeed = cGlobalSpeed;
        persistence = cPersistence + cPersistFine;
        colorShift = cColorShift;
        useRainbow = cUseRainbow;
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
        lissajous.lineClamp = (uint8_t)cLineClamp;
        lissajous.modLineSpeed.modRate = cModLineSpeedRate;
        lissajous.modLineSpeed.modLevel = cModLineSpeedLevel;
        lissajous.modLineAmp.modRate = cModLineAmpRate;
        lissajous.modLineAmp.modLevel = cModLineAmpLevel;
        noiseKaleido.driftSpeed = cDriftSpeed;
        noiseKaleido.noiseScale = cNoiseScale;
        noiseKaleido.noiseBand = cNoiseBand;
        noiseKaleido.kaleidoGamma = cKaleidoGamma;
        cube.scale = cScale;
        cube.rotateSpeed[0] = cRotateSpeedX;
        cube.rotateSpeed[1] = cRotateSpeedY;
        cube.rotateSpeed[2] = cRotateSpeedZ;
        cube.axisFreeze[0] = cAxisFreezeX;
        cube.axisFreeze[1] = cAxisFreezeY;
        cube.axisFreeze[2] = cAxisFreezeZ;
        cube.modScale.modRate = cModScaleRate;
        cube.modScale.modLevel = cModScaleLevel;
        cube.modRotateSpeedX.modRate = cModRotateSpeedXRate;
        cube.modRotateSpeedX.modLevel = cModRotateSpeedXLevel;
        cube.modRotateSpeedY.modRate = cModRotateSpeedYRate;
        cube.modRotateSpeedY.modLevel = cModRotateSpeedYLevel;
        cube.modRotateSpeedZ.modRate = cModRotateSpeedZRate;
        cube.modRotateSpeedZ.modLevel = cModRotateSpeedZLevel;
        // fluidJet
        fluidJet.jetDensity = cJetDensity;
        fluidJet.jetForce = cJetForce;
        fluidJet.jetRadius = cJetRadius;
        fluidJet.jetSpread = cJetSpread;
        fluidJet.jetAngle = cJetAngle;
        fluidJet.jetHueSpeed = cJetHueSpeed;
        fluidJet.modJetForce.modRate = cModJetForceRate;
        fluidJet.modJetForce.modLevel = cModJetForceLevel;
        fluidJet.modAngle.modRate = cModAngleRate;
        fluidJet.modAngle.modLevel = cModAngleLevel;
        // Flow field + modulator
        syncFlowFromCVars();
    }

    static void updatePaletteState() {
        if (gGradientPaletteCount == 0) {
            return;
        }

        if (cPaletteMode && cRotatePalette) {
            EVERY_N_SECONDS(15) {
                gCurrentPaletteNumber = gTargetPaletteNumber;
                gTargetPaletteNumber = addmod8(gTargetPaletteNumber, 1, gGradientPaletteCount);
                gTargetPalette = gGradientPalettes[gTargetPaletteNumber];
            }
        }

        int maxChanges = (int)(cPaletteBlendRate + 0.5f);
        if (maxChanges < 1) {
            maxChanges = 1;
        } else if (maxChanges > 255) {
            maxChanges = 255;
        }

        EVERY_N_MILLISECONDS(40) {
            if (gCurrentPalette != gTargetPalette) {
                nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, (uint8_t)maxChanges);
            } else {
                gCurrentPaletteNumber = gTargetPaletteNumber;
            }
        }
    }

    void runFlowFields() {
        unsigned long now = fl::millis();
        float rawDt = (now - lastFrameMs) * 0.001f;
        lastFrameMs = now;
        dt = rawDt * globalSpeed;
        t += dt;

        // Update emitter if changed
        if (EMITTER < EMITTER_COUNT && EMITTER != lastEmitter) {
            activeEmitter = (Emitter)EMITTER;
            lastEmitter = EMITTER;
            pushDefaultsToCVars();
            sendEmitterState();
        }

        // Update flow field if changed
        if (FLOW < FLOW_COUNT && FLOW != lastFlow) {
            activeFlow = (Flow)FLOW;
            lastFlow = FLOW;
            pushFlowDefaultsToCVars();
            sendFlowState();
        }

        // Sync UI-controlled values into component structs
        syncFromCVars();
        updatePaletteState();

        // 1. Flow field: prepare (build X and Y profiles)
        FLOW_PREPARE[activeFlow]();

        // 2. Emitter: inject color onto grid
        EMITTER_RUN[activeEmitter]();

        // 3. Flow field: advect + fade
        FLOW_ADVECT[activeFlow]();

        // 4. Copy float grid to LED array
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                uint32_t idx = xyFunc((uint16_t)x, (uint16_t)y);
                if (idx >= (uint32_t)NUM_LEDS) continue;
                leds[idx].r = f2u8d(gR[y][x], x, y);
                leds[idx].g = f2u8d(gG[y][x], x, y);
                leds[idx].b = f2u8d(gB[y][x], x, y);
            }
        }
    }

} // namespace flowFields
