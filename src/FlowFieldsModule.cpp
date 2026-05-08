// ═══════════════════════════════════════════════════════════════════
//  FlowFieldsModule.cpp — the ONLY translation unit that includes
//  the FlowFields engine headers in library mode.
//
//  Width / Height / leds are runtime values set by setup().
//  boardConfig.h macros are intentionally NOT included here.
// ═══════════════════════════════════════════════════════════════════

#include "FlowFieldsModule.h"

// ── Runtime globals visible to engine headers ────────────────────
// In firmware these come from boardConfig.h macros; here we define
// them as actual int variables set by setup() / onSizeChanged().
static int WIDTH        = 0;
static int HEIGHT       = 0;
static int MIN_DIMENSION= 0;
static uint32_t NUM_LEDS= 0;

// leds pointer — accessed by flowFieldsEngine.hpp's runFlowFields().
static fl::CRGB* leds = nullptr;

// EMITTER / FLOW selection globals
static uint8_t EMITTER   = 0;
static uint8_t FLOW      = 0;
static uint8_t BRIGHTNESS= 35;

// ── No-op stubs for BLE state-sync functions ────────────────────
// These are called from flowFieldsEngine.hpp but are BLE-only in firmware.
void sendEmitterState() {}
void sendFlowState()    {}

// ── Engine headers (single-TU inclusion) ────────────────────────
// Define AUDIO_ENABLED only if the consumer defines it before
// including FlowFieldsModule.h; otherwise audio emitter is a no-op.
#include "parameterSchema.h"
#include "flowFieldsEngine.hpp"

using namespace flowFields;

// ── FlowFieldsModule method implementations ──────────────────────

void FlowFieldsModule::setup(uint16_t w, uint16_t h, XYFunc xy,
                              fl::CRGB* ledsPtr, uint32_t n) {
    WIDTH         = (int)w;
    HEIGHT        = (int)h;
    MIN_DIMENSION = (int)fl::min(w, h);
    NUM_LEDS      = n;
    leds          = ledsPtr;
    initFlowFields(xy);
}

void FlowFieldsModule::onSizeChanged(uint16_t w, uint16_t h) {
    resizeFlowFields((int)w, (int)h);   // scales content, reallocates grids
    WIDTH         = (int)w;             // update dims AFTER resize (uses old dims for scaling)
    HEIGHT        = (int)h;
    MIN_DIMENSION = (int)fl::min(w, h);
}

void FlowFieldsModule::loop()     { runFlowFields(); }
void FlowFieldsModule::teardown() { teardownFlowFields(); }

void FlowFieldsModule::setEmitter(uint8_t idx) { EMITTER = idx; }
void FlowFieldsModule::setFlow   (uint8_t idx) { FLOW    = idx; }

void FlowFieldsModule::setParameter(const char* name, float v) {
    if (strcasecmp(name, "emitter") == 0) { EMITTER = (uint8_t)v; return; }
    if (strcasecmp(name, "flow")    == 0) { FLOW    = (uint8_t)v; return; }
    // Walk PARAMETER_TABLE; write matching cVar.
    #define X(type, Name, def) \
        if (strcasecmp(name, #Name) == 0) { c##Name = (type)v; return; }
    PARAMETER_TABLE
    #undef X
}

float FlowFieldsModule::getParameter(const char* name) const {
    #define X(type, Name, def) \
        if (strcasecmp(name, #Name) == 0) return (float)c##Name;
    PARAMETER_TABLE
    #undef X
    return 0.0f;
}

FlowFieldsModule::ParamList FlowFieldsModule::getGlobalParams() const {
    return { GLOBAL_PARAMS, GLOBAL_PARAM_COUNT };
}

FlowFieldsModule::ParamList FlowFieldsModule::getEmitterParams(uint8_t idx) const {
    const EmitterParamEntry* e = ::getEmitterParams(idx);
    if (!e) return { nullptr, 0 };
    return { e->params, e->count };
}

FlowFieldsModule::ParamList FlowFieldsModule::getFlowParams(uint8_t idx) const {
    const FlowParamEntry* f = ::getFlowParams(idx);
    if (!f) return { nullptr, 0 };
    return { f->params, f->count };
}

uint8_t     FlowFieldsModule::emitterCount()         { return (uint8_t)EMITTER_COUNT; }
uint8_t     FlowFieldsModule::flowCount()            { return (uint8_t)FLOW_COUNT; }
const char* FlowFieldsModule::emitterName(uint8_t i) { return (i < EMITTER_COUNT) ? EMITTER_NAMES[i] : ""; }
const char* FlowFieldsModule::flowName   (uint8_t i) { return (i < FLOW_COUNT)    ? FLOW_NAMES[i]    : ""; }
