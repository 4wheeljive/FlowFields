#pragma once

// ═══════════════════════════════════════════════════════════════════
//  FlowFieldsEffect v2 — FastLED-MM / projectMM integration example
// ═══════════════════════════════════════════════════════════════════
//
//  V2 vs V1 key differences:
//
//  1. Includes FlowFieldsModule.h (thin façade) instead of FlowFieldsEngine.h.
//     Consumer never sees engine internals (no struct field names, no cVars).
//
//  2. onUpdate() uses ff_.setParameter("name", value) — one call per changed
//     param via a name→member table.  No knowledge of internal struct layout.
//     V1 had 45 lines of engine_.struct.field = member_ assignments.
//
//  3. onSizeChanged() calls ff_.onSizeChanged(w, h) — the shim reallocates
//     grids and scales pixel content so no black frame occurs.
//     V1 called engine_.teardown() + engine_.setup() (one black frame).
//
//  4. Dynamic parameter visibility (C4): registerControls() uses
//     ff_.getEmitterParams() / ff_.getFlowParams() to register only the
//     controls relevant to the active combination.  Requires projectMM to
//     expose clearControls() — left as pseudocode until that API exists.

#include <Arduino.h>
#include <FastLED.h>
#include <projectMM.h>

#include "FlowFieldsModule.h"   // v2 façade — replaces FlowFieldsEngine.h

constexpr uint16_t WIDTH    = 44;
constexpr uint16_t HEIGHT   = 44;
constexpr uint32_t NUM_LEDS = (uint32_t)WIDTH * HEIGHT;

static uint32_t XY(uint16_t x, uint16_t y) { return (uint32_t)y * WIDTH + x; }

CRGB leds[NUM_LEDS];

// ── Parameter ranges — one row per slider registered in the UI ──────
// Mirrors VISUALIZER_PARAMETER_REGISTRY in index.html.
// Used by registerControls() to add controls by name without hardcoding
// each addControl() call per emitter/flow.
struct ParamRange { const char* name; float lo, hi; };
static const ParamRange PARAM_RANGES[] = {
    { "globalSpeed",  0.1f,   5.0f  },
    { "persistence",  0.01f,  2.0f  },
    { "colorShift",   0.0f,   1.0f  },
    { "numDots",      1.0f,  20.0f  },
    { "dotDiam",      0.5f,   5.0f  },
    { "orbitSpeed",   0.01f, 25.0f  },
    { "orbitDiam",    1.0f,  30.0f  },
    { "swarmSpeed",   0.01f,  5.0f  },
    { "swarmSpread",  0.0f,   1.0f  },
    { "lineSpeed",    0.01f,  5.0f  },
    { "lineAmp",      1.0f,  20.0f  },
    { "driftSpeed",   0.0f,   2.0f  },
    { "noiseScale",   0.001f, 0.2f  },
    { "noiseBand",    0.01f,  0.5f  },
    { "scale",        0.1f,   3.0f  },
    { "rotateSpeedX", -5.0f,  5.0f  },
    { "rotateSpeedY", -5.0f,  5.0f  },
    { "rotateSpeedZ", -5.0f,  5.0f  },
    { "jetForce",     0.0f,   2.0f  },
    { "jetAngle",    -3.14f,  3.14f },
    { "xSpeed",      -5.0f,   5.0f  },
    { "ySpeed",      -5.0f,   5.0f  },
    { "xShift",       0.0f,   5.0f  },
    { "yShift",       0.0f,   5.0f  },
    { "xFreq",        0.01f,  2.0f  },
    { "yFreq",        0.01f,  2.0f  },
    { "blendFactor",  0.0f,   1.0f  },
    { "innerSwirl",  -1.0f,   1.0f  },
    { "outerSwirl",  -1.0f,   1.0f  },
    { "midDrift",     0.0f,   1.0f  },
    { "angularStep",  0.0f,   1.0f  },
    { "viscosity",    0.0f,   0.01f },
    { "vorticity",    0.0f,  20.0f  },
    { "gravity",     -2.0f,   2.0f  },
};

// ── Member-variable lookup for onUpdate dispatch ─────────────────────
// Maps parameter name to the address of the corresponding float member.
// Kept as a pointer so the table can reference members via offsetof.
struct Binding { const char* name; float* ptr; };

class FlowFieldsEffect : public ProducerModule
{
public:
    const char* name()     const override { return "FlowFields"; }
    const char* category() const override { return "source"; }
    uint8_t preferredCore() const override { return 0; }

    uint16_t pixelWidth()  const override { return WIDTH; }
    uint16_t pixelHeight() const override { return HEIGHT; }

    // ── Setup ────────────────────────────────────────────────────────
    void setup() override {
        declareBuffer(leds, NUM_LEDS, sizeof(CRGB));

        // Emitter / flow selectors — always visible.
        addControl(emitterIdx_, "emitter", "select", 0.0f, (float)(ff_.emitterCount() - 1));
        for (uint8_t i = 0; i < ff_.emitterCount(); i++)
            addSelectOption(emitterIdx_, ff_.emitterName(i));

        addControl(flowIdx_, "flow", "select", 0.0f, (float)(ff_.flowCount() - 1));
        for (uint8_t i = 0; i < ff_.flowCount(); i++)
            addSelectOption(flowIdx_, ff_.flowName(i));

        // Register all other controls.  When projectMM exposes clearControls()
        // this flat registration is replaced by registerControls() below.
        registerAllControls();

        ff_.setup(WIDTH, HEIGHT, XY, leds, NUM_LEDS);
    }

    // ── Panel resize — no black frame ────────────────────────────────
    void onSizeChanged() override {
        // Shim allocates new grids, scales pixel content, frees old grids.
        // Timing state (t, dt, noise) preserved — next loop() renders immediately.
        ff_.onSizeChanged((uint16_t)pixelWidth(), (uint16_t)pixelHeight());
    }

    // ── Parameter update — called per slider event, not per frame ────
    // V2 difference: no engine struct field names here.
    // ff_.setParameter routes through PARAMETER_TABLE + syncFromCVars().
    void onUpdate(const char* name) override {
        if (strcmp(name, "emitter") == 0) { ff_.setEmitter((uint8_t)emitterIdx_); return; }
        if (strcmp(name, "flow")    == 0) { ff_.setFlow   ((uint8_t)flowIdx_);    return; }

        // Generic dispatch: find member by name, forward to engine.
        for (const auto& b : bindings_) {
            if (strcasecmp(name, b.name) == 0) {
                ff_.setParameter(name, *b.ptr);
                return;
            }
        }
    }

    // ── Hot path ─────────────────────────────────────────────────────
    void loop()     override { ff_.loop(); }
    void teardown() override { ff_.teardown(); }
    size_t classSize() const override { return sizeof(*this); }

private:
    FlowFieldsModule ff_;

    // ── projectMM-owned values — one per registered control ──────────
    float emitterIdx_  = 0.0f;
    float flowIdx_     = 0.0f;
    float globalSpeed_ = 1.0f;
    float persistence_ = 0.05f;
    float colorShift_  = 0.20f;
    float numDots_     = 3.0f;
    float dotDiam_     = 1.5f;
    float orbitSpeed_  = 2.0f;
    float orbitDiam_   = 6.6f;
    float swarmSpeed_  = 0.5f;
    float swarmSpread_ = 0.5f;
    float lineSpeed_   = 0.35f;
    float lineAmp_     = 13.5f;
    float driftSpeed_  = 0.35f;
    float noiseScale_  = 0.0375f;
    float noiseBand_   = 0.1f;
    float scale_       = 1.0f;
    float rotateSpeedX_ = 0.6f;
    float rotateSpeedY_ = 0.9f;
    float rotateSpeedZ_ = 0.3f;
    float jetForce_    = 0.35f;
    float jetAngle_    = 0.0f;
    float xSpeed_      = 0.15f;
    float ySpeed_      = 0.15f;
    float xShift_      = 1.5f;
    float yShift_      = 1.5f;
    float xFreq_       = 0.33f;
    float yFreq_       = 0.32f;
    float blendFactor_ = 0.45f;
    float innerSwirl_  = -0.2f;
    float outerSwirl_  = 0.2f;
    float midDrift_    = 0.3f;
    float angularStep_ = 0.28f;
    float viscosity_   = 0.0005f;
    float vorticity_   = 7.0f;
    float gravity_     = 0.3f;

    // Name → member pointer table for onUpdate dispatch.
    // Order matches PARAM_RANGES[] — both must stay in sync.
    const Binding bindings_[34] = {
        { "globalSpeed",  &globalSpeed_ },
        { "persistence",  &persistence_ },
        { "colorShift",   &colorShift_  },
        { "numDots",      &numDots_     },
        { "dotDiam",      &dotDiam_     },
        { "orbitSpeed",   &orbitSpeed_  },
        { "orbitDiam",    &orbitDiam_   },
        { "swarmSpeed",   &swarmSpeed_  },
        { "swarmSpread",  &swarmSpread_ },
        { "lineSpeed",    &lineSpeed_   },
        { "lineAmp",      &lineAmp_     },
        { "driftSpeed",   &driftSpeed_  },
        { "noiseScale",   &noiseScale_  },
        { "noiseBand",    &noiseBand_   },
        { "scale",        &scale_       },
        { "rotateSpeedX", &rotateSpeedX_ },
        { "rotateSpeedY", &rotateSpeedY_ },
        { "rotateSpeedZ", &rotateSpeedZ_ },
        { "jetForce",     &jetForce_    },
        { "jetAngle",     &jetAngle_    },
        { "xSpeed",       &xSpeed_      },
        { "ySpeed",       &ySpeed_      },
        { "xShift",       &xShift_      },
        { "yShift",       &yShift_      },
        { "xFreq",        &xFreq_       },
        { "yFreq",        &yFreq_       },
        { "blendFactor",  &blendFactor_ },
        { "innerSwirl",   &innerSwirl_  },
        { "outerSwirl",   &outerSwirl_  },
        { "midDrift",     &midDrift_    },
        { "angularStep",  &angularStep_ },
        { "viscosity",    &viscosity_   },
        { "vorticity",    &vorticity_   },
        { "gravity",      &gravity_     },
    };

    // Register all float params from PARAM_RANGES[].
    // Flat list — used until projectMM exposes clearControls().
    void registerAllControls() {
        for (const auto& r : PARAM_RANGES) {
            for (const auto& b : bindings_) {
                if (strcmp(r.name, b.name) == 0) {
                    addControl(*b.ptr, r.name, "slider", r.lo, r.hi);
                    break;
                }
            }
        }
    }

    // ── PSEUDOCODE: dynamic control registration using C4 param-list API ─
    //
    // Once projectMM exposes clearControls(), replace registerAllControls()
    // with this.  ff_.getEmitterParams() / ff_.getFlowParams() return exactly
    // the same name lists that drive the BLE/web-UI's per-emitter slider set.
    //
    // void registerControls() {
    //     clearControls();   // hypothetical projectMM API
    //
    //     // Always-present selectors
    //     addControl(emitterIdx_, "emitter", "select", ...); // + addSelectOption loop
    //     addControl(flowIdx_,    "flow",    "select", ...); // + addSelectOption loop
    //
    //     // Global params
    //     auto globals = ff_.getGlobalParams();
    //     for (uint8_t i = 0; i < globals.count; i++)
    //         addControlByName(globals.names[i]);   // looks up range from PARAM_RANGES
    //
    //     // Emitter-specific params
    //     auto ep = ff_.getEmitterParams((uint8_t)emitterIdx_);
    //     for (uint8_t i = 0; i < ep.count; i++)
    //         addControlByName(ep.names[i]);
    //
    //     // Flow-specific params
    //     auto fp = ff_.getFlowParams((uint8_t)flowIdx_);
    //     for (uint8_t i = 0; i < fp.count; i++)
    //         addControlByName(fp.names[i]);
    // }
    //
    // // Helper: addControl for a named param using PARAM_RANGES lookup.
    // void addControlByName(const char* name) {
    //     for (const auto& r : PARAM_RANGES)
    //         if (strcmp(name, r.name) == 0) {
    //             for (const auto& b : bindings_)
    //                 if (strcmp(name, b.name) == 0) {
    //                     addControl(*b.ptr, name, "slider", r.lo, r.hi);
    //                     return;
    //                 }
    //         }
    // }
    //
    // // Trigger re-registration on emitter/flow change in onUpdate():
    // // if (strcmp(name,"emitter")==0 || strcmp(name,"flow")==0) registerControls();
};

REGISTER_MODULE(FlowFieldsEffect)
