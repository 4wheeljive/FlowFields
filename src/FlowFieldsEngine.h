#pragma once

#include "componentEnums.h"
#include "flowFieldsTypes.h"   // Perlin1D/2D, ValueNoise2D, ColorF, ModConfig, math helpers
#include "modulators.h"        // timers, modulators structs

namespace flowFields {

class FlowFieldsEngine {
public:
    // ── Dimensions ────────────────────────────────────────────────────────
    uint8_t  _width   = 0;
    uint8_t  _height  = 0;
    uint16_t _numLeds = 0;
    uint8_t  _minDim  = 0;

    // ── Float grids — float**[height][width] allocated in setup() ─────────
    float** gR = nullptr;
    float** gG = nullptr;
    float** gB = nullptr;
    float** tR = nullptr;
    float** tG = nullptr;
    float** tB = nullptr;

    // ── Noise generators ──────────────────────────────────────────────────
    Perlin1D     noiseX, noiseY;
    Perlin1D     ampVarX, ampVarY;
    Perlin2D     noise2X, noise2Y;
    ValueNoise2D kaleidoNoise;

    // ── Noise profiles — float*[_width] / float*[_height] ────────────────
    float* xProf = nullptr;
    float* yProf = nullptr;

    // ── Frame timing ──────────────────────────────────────────────────────
    unsigned long lastFrameMs = 0;
    float t  = 0.0f;
    float dt = 0.0f;

    // ── Config ────────────────────────────────────────────────────────────
    float globalSpeed = 1.0f;
    float persistence = 0.05f;
    float colorShift  = 0.20f;
    bool  useRainbow  = false;

    // ── Selection state ───────────────────────────────────────────────────
    uint8_t lastEmitter = 255;
    uint8_t lastFlow    = 255;
    Emitter activeEmitter = EMITTER_ORBITALDOTS;
    Flow    activeFlow    = FLOW_NOISE;

    // ── XY mapping ────────────────────────────────────────────────────────
    uint16_t (*xyFunc)(uint8_t x, uint8_t y) = nullptr;

    // ── Modulator state ───────────────────────────────────────────────────
    timers    timings;
    modulators move;
    unsigned long _modLastRealMs = 0;
    float         _modVirtualMs  = 0.0f;

    // ── Callbacks (null-safe; set by caller during setup) ─────────────────
    void (*onEmitterChanged)() = nullptr;
    void (*onFlowChanged)()    = nullptr;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void setup(uint8_t width, uint8_t height, uint16_t numLeds,
               uint16_t (*xy)(uint8_t, uint8_t));
    void run(fl::CRGB* leds);
    void teardown();

    // ── Modulator calculation — called by emitters/flows as g_engine->calculate_modulators(N)
    void calculate_modulators(uint8_t numActiveTimers);

    // ── Color helpers ─────────────────────────────────────────────────────
    static ColorF hsvSpectrum(float hue);
    static ColorF hsvRainbow(float hue);
    ColorF rainbow(float t_val, float speed, float phase) const;

    // ── Drawing primitives — called by emitters/flows as g_engine->drawXxx(...)
    void drawDot(float cx, float cy, float diam, float cr, float cg, float cb);
    void blendPixelWeighted(int px, int py, float cr, float cg, float cb, float w);
    void drawAAEndpointDisc(float cx, float cy, float cr, float cg, float cb,
                            float radius = 0.85f);
    void drawAASubpixelLine(float x0, float y0, float x1, float y1);

    // ── cVar bridge (implemented in flowFieldsEngine.hpp; uses bleControl globals) ──
    void pushDefaultsToCVars();
    void syncFromCVars();
    void pushFlowDefaultsToCVars();
    void syncFlowFromCVars();

private:
    static float** allocGrid(uint8_t w, uint8_t h);
    static void    freeGrid(float** g, uint8_t h);
};

// Set by FlowFieldsEngine::run() before dispatching to any emitter/flow function.
// Safe for single-threaded embedded use (one active engine at a time).
extern FlowFieldsEngine* g_engine;

} // namespace flowFields
