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

#include <projectMM.h>               // FastLED-MM framework
#include <FlowFieldsEngine.h>        // from lib_deps = ewowi/FlowFields
#include "componentEnums.h"          // Emitter / Flow enums

// XY mapping for this panel — replace with your own.
static uint16_t myXY(uint8_t x, uint8_t y) {
    return (uint16_t)y * PANEL_WIDTH + x;   // simple row-major scan
}

class FlowFieldsEffect : public ProducerModule {
public:
    // ── Identity ─────────────────────────────────────────────────────
    const char* name()     const override { return "FlowFields"; }
    const char* category() const override { return "source"; }
    uint8_t preferredCore() const override { return 0; }

    uint16_t pixelWidth()  const override { return PANEL_WIDTH; }
    uint16_t pixelHeight() const override { return PANEL_HEIGHT; }

    // ── Setup ────────────────────────────────────────────────────────
    void setup() override {
        const uint16_t numLeds = PANEL_WIDTH * PANEL_HEIGHT;
        declareBuffer(leds_, numLeds, sizeof(fl::CRGB));

        // 1. Register controls — projectMM owns and updates these floats.
        addControl(emitterIdx_,          "emitter",              "select", 0.0f,   (float)(EMITTER_COUNT - 1), 0.0f);
        addControl(flowIdx_,             "flow",                 "select", 0.0f,   (float)(FLOW_COUNT - 1),    0.0f);
        addControl(globalSpeed_,         "globalSpeed",          "slider", 0.1f,   5.0f,    1.0f);
        addControl(persistence_,         "persistence",          "slider", 0.01f,  2.0f,    0.05f);
        addControl(colorShift_,          "colorShift",           "slider", 0.0f,   1.0f,    0.20f);
        addControl(numDots_,             "numDots",              "slider", 1.0f,   20.0f,   3.0f);
        addControl(dotDiam_,             "dotDiam",              "slider", 0.5f,   5.0f,    1.5f);
        addControl(orbitSpeed_,          "orbitSpeed",           "slider", 0.01f,  25.0f,   2.0f);
        addControl(orbitDiam_,           "orbitDiam",            "slider", 1.0f,   30.0f,   6.6f);
        addControl(swarmSpeed_,          "swarmSpeed",           "slider", 0.01f,  5.0f,    0.5f);
        addControl(swarmSpread_,         "swarmSpread",          "slider", 0.0f,   1.0f,    0.5f);
        addControl(lineSpeed_,           "lineSpeed",            "slider", 0.01f,  5.0f,    0.35f);
        addControl(lineAmp_,             "lineAmp",              "slider", 1.0f,   20.0f,   13.5f);
        addControl(driftSpeed_,          "driftSpeed",           "slider", 0.0f,   2.0f,    0.35f);
        addControl(noiseScale_,          "noiseScale",           "slider", 0.001f, 0.2f,    0.0375f);
        addControl(noiseBand_,           "noiseBand",            "slider", 0.01f,  0.5f,    0.1f);
        addControl(scale_,               "scale",                "slider", 0.1f,   3.0f,    1.0f);
        addControl(rotateSpeedX_,        "rotateSpeedX",         "slider", -5.0f,  5.0f,    0.6f);
        addControl(rotateSpeedY_,        "rotateSpeedY",         "slider", -5.0f,  5.0f,    0.9f);
        addControl(rotateSpeedZ_,        "rotateSpeedZ",         "slider", -5.0f,  5.0f,    0.3f);
        addControl(jetForce_,            "jetForce",             "slider", 0.0f,   2.0f,    0.35f);
        addControl(jetAngle_,            "jetAngle",             "slider", -3.14f, 3.14f,   0.0f);
        addControl(xSpeed_,              "xSpeed",               "slider", -5.0f,  5.0f,    0.15f);
        addControl(ySpeed_,              "ySpeed",               "slider", -5.0f,  5.0f,    0.15f);
        addControl(xShift_,              "xShift",               "slider", 0.0f,   5.0f,    1.5f);
        addControl(yShift_,              "yShift",               "slider", 0.0f,   5.0f,    1.5f);
        addControl(xFreq_,               "xFreq",                "slider", 0.01f,  2.0f,    0.33f);
        addControl(yFreq_,               "yFreq",                "slider", 0.01f,  2.0f,    0.32f);
        addControl(blendFactor_,         "blendFactor",          "slider", 0.0f,   1.0f,    0.45f);
        addControl(innerSwirl_,          "innerSwirl",           "slider", -1.0f,  1.0f,   -0.2f);
        addControl(outerSwirl_,          "outerSwirl",           "slider", -1.0f,  1.0f,    0.2f);
        addControl(midDrift_,            "midDrift",             "slider", 0.0f,   1.0f,    0.3f);
        addControl(angularStep_,         "angularStep",          "slider", 0.0f,   1.0f,    0.28f);
        addControl(viscosity_,           "viscosity",            "slider", 0.0f,   0.01f,   0.0005f);
        addControl(vorticity_,           "vorticity",            "slider", 0.0f,   20.0f,   7.0f);
        addControl(gravity_,             "gravity",              "slider", -2.0f,  2.0f,    0.3f);

        // 2. Initialise the engine for this panel.
        engine_.setup(PANEL_WIDTH, PANEL_HEIGHT, numLeds, myXY);

        // 3. Bind: redirect engine cVars to the floats projectMM updates.
        //    Called once here — run() pays only a pointer dereference per param.
        engine_.bindParam("emitter",       &emitterIdx_);
        engine_.bindParam("flow",          &flowIdx_);
        engine_.bindParam("globalSpeed",   &globalSpeed_);
        engine_.bindParam("persistence",   &persistence_);
        engine_.bindParam("colorShift",    &colorShift_);
        engine_.bindParam("dotDiam",       &dotDiam_);
        engine_.bindParam("orbitSpeed",    &orbitSpeed_);
        engine_.bindParam("orbitDiam",     &orbitDiam_);
        engine_.bindParam("swarmSpeed",    &swarmSpeed_);
        engine_.bindParam("swarmSpread",   &swarmSpread_);
        engine_.bindParam("lineSpeed",     &lineSpeed_);
        engine_.bindParam("lineAmp",       &lineAmp_);
        engine_.bindParam("driftSpeed",    &driftSpeed_);
        engine_.bindParam("noiseScale",    &noiseScale_);
        engine_.bindParam("noiseBand",     &noiseBand_);
        engine_.bindParam("scale",         &scale_);
        engine_.bindParam("rotateSpeedX",  &rotateSpeedX_);
        engine_.bindParam("rotateSpeedY",  &rotateSpeedY_);
        engine_.bindParam("rotateSpeedZ",  &rotateSpeedZ_);
        engine_.bindParam("jetForce",      &jetForce_);
        engine_.bindParam("jetAngle",      &jetAngle_);
        engine_.bindParam("xSpeed",        &xSpeed_);
        engine_.bindParam("ySpeed",        &ySpeed_);
        engine_.bindParam("xShift",        &xShift_);
        engine_.bindParam("yShift",        &yShift_);
        engine_.bindParam("xFreq",         &xFreq_);
        engine_.bindParam("yFreq",         &yFreq_);
        engine_.bindParam("blendFactor",   &blendFactor_);
        engine_.bindParam("innerSwirl",    &innerSwirl_);
        engine_.bindParam("outerSwirl",    &outerSwirl_);
        engine_.bindParam("midDrift",      &midDrift_);
        engine_.bindParam("angularStep",   &angularStep_);
        engine_.bindParam("viscosity",     &viscosity_);
        engine_.bindParam("vorticity",     &vorticity_);
        engine_.bindParam("gravity",       &gravity_);
    }

    void onSizeChanged() override {
        engine_.teardown();
        engine_.setup(pixelWidth(), pixelHeight(),
                      pixelWidth() * pixelHeight(), myXY);
        // Bindings survive teardown/setup — they point into this object's
        // own member floats, not into the (now-freed) grid buffers.
    }

    // ── Hot path ─────────────────────────────────────────────────────
    void loop() override {
        engine_.run(leds_);
    }

    void teardown() override { engine_.teardown(); }
    size_t classSize() const override { return sizeof(*this); }

private:
    static constexpr uint8_t  PANEL_WIDTH  = 22;
    static constexpr uint8_t  PANEL_HEIGHT = 22;

    fl::CRGB leds_[PANEL_WIDTH * PANEL_HEIGHT];
    flowFields::FlowFieldsEngine engine_;

    // ── projectMM-owned floats — one per bound parameter ─────────────
    float emitterIdx_   = 0.0f;
    float flowIdx_      = 0.0f;
    float globalSpeed_  = 1.0f;
    float persistence_  = 0.05f;
    float colorShift_   = 0.20f;
    float numDots_      = 3.0f;
    float dotDiam_      = 1.5f;
    float orbitSpeed_   = 2.0f;
    float orbitDiam_    = 6.6f;
    float swarmSpeed_   = 0.5f;
    float swarmSpread_  = 0.5f;
    float lineSpeed_    = 0.35f;
    float lineAmp_      = 13.5f;
    float driftSpeed_   = 0.35f;
    float noiseScale_   = 0.0375f;
    float noiseBand_    = 0.1f;
    float scale_        = 1.0f;
    float rotateSpeedX_ = 0.6f;
    float rotateSpeedY_ = 0.9f;
    float rotateSpeedZ_ = 0.3f;
    float jetForce_     = 0.35f;
    float jetAngle_     = 0.0f;
    float xSpeed_       = 0.15f;
    float ySpeed_       = 0.15f;
    float xShift_       = 1.5f;
    float yShift_       = 1.5f;
    float xFreq_        = 0.33f;
    float yFreq_        = 0.32f;
    float blendFactor_  = 0.45f;
    float innerSwirl_   = -0.2f;
    float outerSwirl_   =  0.2f;
    float midDrift_     = 0.3f;
    float angularStep_  = 0.28f;
    float viscosity_    = 0.0005f;
    float vorticity_    = 7.0f;
    float gravity_      = 0.3f;
};

REGISTER_MODULE(FlowFieldsEffect)
