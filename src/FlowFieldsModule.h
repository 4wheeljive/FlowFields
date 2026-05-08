#pragma once

// ═══════════════════════════════════════════════════════════════════
//  FlowFieldsModule.h — thin façade for FastLED-MM / projectMM
// ═══════════════════════════════════════════════════════════════════
//
//  v2 design: engine headers are included only in FlowFieldsModule.cpp.
//  Library consumers include only this header; they never see cVars,
//  X-macros, or internal struct field names.

#include <FastLED.h>

class FlowFieldsModule {
public:
    using XYFunc = uint32_t (*)(uint16_t, uint16_t);

    // ── Lifecycle ────────────────────────────────────────────────────
    void  setup(uint16_t w, uint16_t h, XYFunc xy, fl::CRGB* leds, uint32_t n);
    void  onSizeChanged(uint16_t w, uint16_t h);
    void  loop();
    void  teardown();

    // ── Parameter control ────────────────────────────────────────────
    void  setEmitter(uint8_t idx);
    void  setFlow   (uint8_t idx);

    // Writes the matching cVar via the PARAMETER_TABLE X-macro walker.
    // syncFromCVars() picks it up on the next loop() call (zero per-frame cost).
    void  setParameter(const char* name, float value);
    float getParameter(const char* name) const;

    // ── Info queries for dynamic UI (reads existing PROGMEM tables) ──
    struct ParamList { const char* const* names; uint8_t count; };
    ParamList  getGlobalParams()              const;
    ParamList  getEmitterParams(uint8_t idx)  const;
    ParamList  getFlowParams   (uint8_t idx)  const;

    static uint8_t      emitterCount();
    static uint8_t      flowCount();
    static const char*  emitterName(uint8_t idx);
    static const char*  flowName   (uint8_t idx);
};
