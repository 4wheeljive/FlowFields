#pragma once

// ═══════════════════════════════════════════════════════════════════
//  FlowFieldsEffect — FastLED-MM / projectMM integration example
// ═══════════════════════════════════════════════════════════════════
//
//  Wraps FlowFieldsEngine in a ProducerModule so all emitters, flows,
//  and parameters are available through the FastLED-MM UI system
//  without any BLE dependency.
//
//  USAGE
//  ─────
//  1. Add to platformio.ini:
//       lib_deps =
//           ewowi/FlowFields
//           <your FastLED-MM dep>
//
//  2. Include and register in your main sketch:
//       #include "FlowFieldsEffect.h"
//       REGISTER_MODULE(FlowFieldsEffect)
//
//  NOTE: This file is illustrative — it targets the projectMM
//  ProducerModule API.  Adapt method names to the actual version
//  of FastLED-MM you are using.
//
//  PARAMETER RELEVANCE (Session 5 context)
//  ─────────────────────────────────────────
//  The engine supports 8 emitters × 6 flows = 48 combinations.
//  Each combination has a different relevant parameter set; most
//  of the 35 registered controls are irrelevant for a given combo.
//  Currently ALL controls are always shown.  See registerControls()
//  pseudocode below and docs/FlowFieldsAsALibrary.md §Session 5.
//
//  Global (all combos): emitter, flow, globalSpeed, persistence, colorShift
//
//  Emitter-specific:
//    orbitaldots  → numDots, dotDiam, orbitSpeed, orbitDiam
//    swarmingdots → numDots, dotDiam, swarmSpeed, swarmSpread
//    audiodots    → (none currently exposed)
//    lissajous    → lineSpeed, lineAmp
//    borderrect   → (none)
//    noisekaleido → driftSpeed, noiseScale, noiseBand
//    cube         → scale, rotateSpeedX/Y/Z
//    fluidjet     → jetForce, jetAngle
//
//  Flow-specific:
//    noise        → xSpeed, ySpeed, xShift, yShift, xFreq, yFreq
//    radial       → blendFactor
//    directional  → blendFactor
//    rings        → innerSwirl, outerSwirl, midDrift
//    spiral       → angularStep, blendFactor
//    fluid        → viscosity, vorticity, gravity

#include <Arduino.h>
#include <FastLED.h>
#include <projectMM.h>

#include "FlowFieldsEngine.h"

constexpr uint8_t PIN = 2;      // data pin to the first LED strip
constexpr uint16_t WIDTH = 44;  // panel width in pixels
constexpr uint16_t HEIGHT = 44; // panel height in pixels
constexpr uint16_t NUM_LEDS = WIDTH * HEIGHT;

// XY mapping for this panel — replace with your own.
static uint32_t XY(uint16_t x, uint16_t y) { return (uint32_t)y * WIDTH + x; }

// Logical pixel array — written by effects in row-major order, read by PreviewModule.
CRGB leds[NUM_LEDS];

class FlowFieldsEffect : public ProducerModule
{
public:
    // ── Identity ─────────────────────────────────────────────────────
    const char *name() const override { return "FlowFields"; }
    const char *category() const override { return "source"; }
    uint8_t preferredCore() const override { return 0; }

    uint16_t pixelWidth() const override { return WIDTH; }
    uint16_t pixelHeight() const override { return HEIGHT; }

    // ── Setup ────────────────────────────────────────────────────────
    // Registers ALL controls upfront. Session 5 should replace this
    // with registerControls() called dynamically on emitter/flow change.
    void setup() override
    {
        declareBuffer(leds, NUM_LEDS, sizeof(CRGB));

        // Always-visible selectors
        addControl(emitterIdx_, "emitter", "select", 0.0f, (float)(EMITTER_COUNT - 1));
        for (int i = 0; i < EMITTER_COUNT; i++)
            addSelectOption(emitterIdx_, EMITTER_NAMES[i]);

        addControl(flowIdx_, "flow", "select", 0.0f, (float)(FLOW_COUNT - 1));
        for (int i = 0; i < FLOW_COUNT; i++)
            addSelectOption(flowIdx_, FLOW_NAMES[i]);

        // Global — relevant for ALL emitter/flow combinations
        addControl(globalSpeed_, "globalSpeed", "slider", 0.1f, 5.0f);
        addControl(persistence_, "persistence", "slider", 0.01f, 2.0f);
        addControl(colorShift_,  "colorShift",  "slider", 0.0f,  1.0f);

        // orbitaldots + swarmingdots
        addControl(numDots_, "numDots", "slider", 1.0f, 20.0f);
        addControl(dotDiam_, "dotDiam", "slider", 0.5f,  5.0f);

        // orbitaldots only
        addControl(orbitSpeed_, "orbitSpeed", "slider", 0.01f, 25.0f);
        addControl(orbitDiam_,  "orbitDiam",  "slider", 1.0f,  30.0f);

        // swarmingdots only
        addControl(swarmSpeed_,  "swarmSpeed",  "slider", 0.01f, 5.0f);
        addControl(swarmSpread_, "swarmSpread", "slider", 0.0f,  1.0f);

        // lissajous only
        addControl(lineSpeed_, "lineSpeed", "slider", 0.01f,  5.0f);
        addControl(lineAmp_,   "lineAmp",   "slider", 1.0f,  20.0f);

        // noisekaleido only
        addControl(driftSpeed_, "driftSpeed", "slider", 0.0f,   2.0f);
        addControl(noiseScale_, "noiseScale", "slider", 0.001f, 0.2f);
        addControl(noiseBand_,  "noiseBand",  "slider", 0.01f,  0.5f);

        // cube only
        addControl(scale_,        "scale",        "slider", 0.1f,  3.0f);
        addControl(rotateSpeedX_, "rotateSpeedX", "slider", -5.0f, 5.0f);
        addControl(rotateSpeedY_, "rotateSpeedY", "slider", -5.0f, 5.0f);
        addControl(rotateSpeedZ_, "rotateSpeedZ", "slider", -5.0f, 5.0f);

        // fluidjet only
        addControl(jetForce_, "jetForce", "slider", 0.0f,   2.0f);
        addControl(jetAngle_, "jetAngle", "slider", -3.14f, 3.14f);

        // noise flow only
        addControl(xSpeed_, "xSpeed", "slider", -5.0f, 5.0f);
        addControl(ySpeed_, "ySpeed", "slider", -5.0f, 5.0f);
        addControl(xShift_, "xShift", "slider",  0.0f, 5.0f);
        addControl(yShift_, "yShift", "slider",  0.0f, 5.0f);
        addControl(xFreq_,  "xFreq",  "slider", 0.01f, 2.0f);
        addControl(yFreq_,  "yFreq",  "slider", 0.01f, 2.0f);

        // radial + directional + spiral flows (shared parameter)
        addControl(blendFactor_, "blendFactor", "slider", 0.0f, 1.0f);

        // rings flow only
        addControl(innerSwirl_, "innerSwirl", "slider", -1.0f, 1.0f);
        addControl(outerSwirl_, "outerSwirl", "slider", -1.0f, 1.0f);
        addControl(midDrift_,   "midDrift",   "slider",  0.0f, 1.0f);

        // spiral flow only (angularStep; blendFactor shared above)
        addControl(angularStep_, "angularStep", "slider", 0.0f, 1.0f);

        // fluid flow only
        addControl(viscosity_, "viscosity", "slider",  0.0f, 0.01f);
        addControl(vorticity_, "vorticity", "slider",  0.0f, 20.0f);
        addControl(gravity_,   "gravity",   "slider", -2.0f,  2.0f);

        // audiodots and borderrect currently have no additional exposed params.

        // 2. Initialise the engine for this panel.
        engine_.setup(WIDTH, HEIGHT, NUM_LEDS, XY);

        onSizeChanged(); //( onSizeChanged not implemented yet in projectMM)
    }

    void onSizeChanged()
    { //  override (onSizeChanged not implemented yet in projectMM)
        engine_.teardown();
        engine_.setup(pixelWidth(), pixelHeight(), pixelWidth() * pixelHeight(), XY);
    }

    // ── PSEUDOCODE: dynamic control registration (Session 5) ─────────
    // If projectMM adds clearControls() / setControlVisible(), replace
    // the flat setup() block above with registerControls() called once
    // at setup and again whenever emitter or flow changes.
    //
    // void registerControls() {
    //     clearControls();  // hypothetical projectMM API
    //
    //     // Always-present selectors and globals
    //     addControl(emitterIdx_, "emitter", "select", 0.0f, (float)(EMITTER_COUNT-1));
    //     for (int i = 0; i < EMITTER_COUNT; i++) addSelectOption(emitterIdx_, EMITTER_NAMES[i]);
    //     addControl(flowIdx_, "flow", "select", 0.0f, (float)(FLOW_COUNT-1));
    //     for (int i = 0; i < FLOW_COUNT; i++) addSelectOption(flowIdx_, FLOW_NAMES[i]);
    //     addControl(globalSpeed_, "globalSpeed", "slider", 0.1f, 5.0f);
    //     addControl(persistence_,  "persistence",  "slider", 0.01f, 2.0f);
    //     addControl(colorShift_,   "colorShift",   "slider", 0.0f,  1.0f);
    //
    //     // Emitter-specific params
    //     switch (emitterIdx_) {
    //     case 0:  // orbitaldots
    //         addControl(numDots_,    "numDots",    "slider", 1.0f,  20.0f);
    //         addControl(dotDiam_,    "dotDiam",    "slider", 0.5f,   5.0f);
    //         addControl(orbitSpeed_, "orbitSpeed", "slider", 0.01f, 25.0f);
    //         addControl(orbitDiam_,  "orbitDiam",  "slider", 1.0f,  30.0f);
    //         break;
    //     case 1:  // swarmingdots
    //         addControl(numDots_,     "numDots",     "slider", 1.0f, 20.0f);
    //         addControl(dotDiam_,     "dotDiam",     "slider", 0.5f,  5.0f);
    //         addControl(swarmSpeed_,  "swarmSpeed",  "slider", 0.01f, 5.0f);
    //         addControl(swarmSpread_, "swarmSpread", "slider", 0.0f,  1.0f);
    //         break;
    //     // case 2 (audiodots): no additional params
    //     case 3:  // lissajous
    //         addControl(lineSpeed_, "lineSpeed", "slider", 0.01f,  5.0f);
    //         addControl(lineAmp_,   "lineAmp",   "slider", 1.0f,  20.0f);
    //         break;
    //     // case 4 (borderrect): no additional params
    //     case 5:  // noisekaleido
    //         addControl(driftSpeed_, "driftSpeed", "slider", 0.0f,   2.0f);
    //         addControl(noiseScale_, "noiseScale", "slider", 0.001f, 0.2f);
    //         addControl(noiseBand_,  "noiseBand",  "slider", 0.01f,  0.5f);
    //         break;
    //     case 6:  // cube
    //         addControl(scale_,        "scale",        "slider", 0.1f,  3.0f);
    //         addControl(rotateSpeedX_, "rotateSpeedX", "slider", -5.0f, 5.0f);
    //         addControl(rotateSpeedY_, "rotateSpeedY", "slider", -5.0f, 5.0f);
    //         addControl(rotateSpeedZ_, "rotateSpeedZ", "slider", -5.0f, 5.0f);
    //         break;
    //     case 7:  // fluidjet
    //         addControl(jetForce_, "jetForce", "slider", 0.0f,   2.0f);
    //         addControl(jetAngle_, "jetAngle", "slider", -3.14f, 3.14f);
    //         break;
    //     }
    //
    //     // Flow-specific params
    //     switch (flowIdx_) {
    //     case 0:  // noise
    //         addControl(xSpeed_, "xSpeed", "slider", -5.0f, 5.0f);
    //         addControl(ySpeed_, "ySpeed", "slider", -5.0f, 5.0f);
    //         addControl(xShift_, "xShift", "slider",  0.0f, 5.0f);
    //         addControl(yShift_, "yShift", "slider",  0.0f, 5.0f);
    //         addControl(xFreq_,  "xFreq",  "slider", 0.01f, 2.0f);
    //         addControl(yFreq_,  "yFreq",  "slider", 0.01f, 2.0f);
    //         break;
    //     case 1:  // radial
    //         addControl(blendFactor_, "blendFactor", "slider", 0.0f, 1.0f);
    //         break;
    //     case 2:  // directional
    //         addControl(blendFactor_, "blendFactor", "slider", 0.0f, 1.0f);
    //         break;
    //     case 3:  // rings
    //         addControl(innerSwirl_, "innerSwirl", "slider", -1.0f, 1.0f);
    //         addControl(outerSwirl_, "outerSwirl", "slider", -1.0f, 1.0f);
    //         addControl(midDrift_,   "midDrift",   "slider",  0.0f, 1.0f);
    //         break;
    //     case 4:  // spiral
    //         addControl(angularStep_,  "angularStep",  "slider", 0.0f, 1.0f);
    //         addControl(blendFactor_,  "blendFactor",  "slider", 0.0f, 1.0f);
    //         break;
    //     case 5:  // fluid
    //         addControl(viscosity_, "viscosity", "slider",  0.0f, 0.01f);
    //         addControl(vorticity_, "vorticity", "slider",  0.0f, 20.0f);
    //         addControl(gravity_,   "gravity",   "slider", -2.0f,  2.0f);
    //         break;
    //     }
    // }
    //
    // Detection in onUpdate():
    //   if (emitterIdx_ != lastEmitterIdx_ || flowIdx_ != lastFlowIdx_) {
    //       registerControls();
    //       lastEmitterIdx_ = emitterIdx_;
    //       lastFlowIdx_    = flowIdx_;
    //   }
    // Add uint8_t lastEmitterIdx_ = 255, lastFlowIdx_ = 255 to private section.

    // Called by projectMM whenever any control value changes — out of the render hot path.
    void onUpdate(const char* /*name*/) override {
        engine_._emitter = emitterIdx_;
        engine_._flow    = flowIdx_;
        engine_.globalSpeed = globalSpeed_;
        engine_.persistence = persistence_;
        engine_.colorShift  = colorShift_;

        engine_.orbitalDots.numDots  = numDots_;
        engine_.swarmingDots.numDots = numDots_;
        engine_.orbitalDots.dotDiam  = dotDiam_;
        engine_.swarmingDots.dotDiam = dotDiam_;

        engine_.orbitalDots.orbitSpeed   = orbitSpeed_;
        engine_.orbitalDots.orbitDiam    = orbitDiam_;
        engine_.swarmingDots.swarmSpeed  = swarmSpeed_;
        engine_.swarmingDots.swarmSpread = swarmSpread_;
        engine_.lissajous.lineSpeed      = lineSpeed_;
        engine_.lissajous.lineAmp        = lineAmp_;
        engine_.noiseKaleido.driftSpeed  = driftSpeed_;
        engine_.noiseKaleido.noiseScale  = noiseScale_;
        engine_.noiseKaleido.noiseBand   = noiseBand_;
        engine_.cube.scale               = scale_;
        engine_.cube.rotateSpeed[0]      = rotateSpeedX_;
        engine_.cube.rotateSpeed[1]      = rotateSpeedY_;
        engine_.cube.rotateSpeed[2]      = rotateSpeedZ_;
        engine_.fluidJet.jetForce        = jetForce_;
        engine_.fluidJet.jetAngle        = jetAngle_;

        engine_.noiseFlow.xSpeed = xSpeed_;
        engine_.noiseFlow.ySpeed = ySpeed_;
        engine_.noiseFlow.xShift = xShift_;
        engine_.noiseFlow.yShift = yShift_;
        engine_.noiseFlow.xFreq  = xFreq_;
        engine_.noiseFlow.yFreq  = yFreq_;

        engine_.radial.blendFactor      = blendFactor_;
        engine_.directional.blendFactor = blendFactor_;
        engine_.spiral.blendFactor      = blendFactor_;

        engine_.ringFlow.innerSwirl = innerSwirl_;
        engine_.ringFlow.outerSwirl = outerSwirl_;
        engine_.ringFlow.midDrift   = midDrift_;
        engine_.spiral.angularStep  = angularStep_;
        engine_.fluid.viscosity     = viscosity_;
        engine_.fluid.vorticity     = vorticity_;
        engine_.fluid.gravity       = gravity_;
    }

    // ── Hot path — parameter state already current via onUpdate ──────
    void loop() override { engine_.run(leds); }

    void teardown() override { engine_.teardown(); }
    size_t classSize() const override { return sizeof(*this); }

private:
    flowFields::FlowFieldsEngine engine_;

    // ── projectMM-owned values — one per registered control ──────────
    uint8_t emitterIdx_ = 0;
    uint8_t flowIdx_    = 0;
    float globalSpeed_ = 1.0f;
    float persistence_ = 0.05f;
    float colorShift_  = 0.20f;
    uint8_t numDots_ = 3;
    float dotDiam_ = 1.5f;
    float orbitSpeed_ = 2.0f;
    float orbitDiam_ = 6.6f;
    float swarmSpeed_ = 0.5f;
    float swarmSpread_ = 0.5f;
    float lineSpeed_ = 0.35f;
    float lineAmp_ = 13.5f;
    float driftSpeed_ = 0.35f;
    float noiseScale_ = 0.0375f;
    float noiseBand_ = 0.1f;
    float scale_ = 1.0f;
    float rotateSpeedX_ = 0.6f;
    float rotateSpeedY_ = 0.9f;
    float rotateSpeedZ_ = 0.3f;
    float jetForce_ = 0.35f;
    float jetAngle_ = 0.0f;
    float xSpeed_ = 0.15f;
    float ySpeed_ = 0.15f;
    float xShift_ = 1.5f;
    float yShift_ = 1.5f;
    float xFreq_ = 0.33f;
    float yFreq_ = 0.32f;
    float blendFactor_ = 0.45f;
    float innerSwirl_ = -0.2f;
    float outerSwirl_ = 0.2f;
    float midDrift_ = 0.3f;
    float angularStep_ = 0.28f;
    float viscosity_ = 0.0005f;
    float vorticity_ = 7.0f;
    float gravity_ = 0.3f;
};

REGISTER_MODULE(FlowFieldsEffect)
