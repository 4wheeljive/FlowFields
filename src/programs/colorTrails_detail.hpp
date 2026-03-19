#pragma once

// ****************** DANGER: UNDER CONSTRUCTION  ******************

//=====================================================================================
//  colortrails began with a FastLED Reddit post by u/StefanPetrick:
//  https://www.reddit.com/r/FastLED/comments/1rny5j3/i_used_codex_for_the_first_time/
//
//  I had Claude help me (1) port it to a FastLED/Arduino-friendly/C++ sketch and then
//  (2) implement that as this new "colorTrails" AuroraPortal prorgam.
//  As Stefan has shared subsequent ideas, I've been implementing them here.
//=====================================================================================

#include "bleControl.h"

namespace colorTrails {

    constexpr float CT_PI = 3.14159265358979f;

    // ═══════════════════════════════════════════════════════════════════
    //  GRID STATE & TIMING
    // ═══════════════════════════════════════════════════════════════════

    bool colorTrailsInstance = false;
    uint16_t (*xyFunc)(uint8_t x, uint8_t y);

    // Floating-point RGB grid, row-major [y][x].
    // Two copies: g* is the live buffer, t* is scratch for advection.
    static float gR[HEIGHT][WIDTH], gG[HEIGHT][WIDTH], gB[HEIGHT][WIDTH];
    static float tR[HEIGHT][WIDTH], tG[HEIGHT][WIDTH], tB[HEIGHT][WIDTH];

    static unsigned long t0;
    static unsigned long lastFrameMs;
    uint8_t lastEmitter = 255;    // force initial setup on first frame
    uint8_t lastFlowField = 255;  // force initial setup on first frame


    // ═══════════════════════════════════════════════════════════════════
    //  NOISE GENERATORS
    // ═══════════════════════════════════════════════════════════════════

    // 1D Perlin noise ---------------------------------------
    class Perlin1D {
    public:
        void init(uint32_t seed) {
            uint8_t p[256];
            for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
            // Fisher-Yates shuffle with a simple LCG
            uint32_t s = seed;
            for (int i = 255; i > 0; i--) {
                s = s * 1664525u + 1013904223u;
                int j = (int)((s >> 16) % (uint32_t)(i + 1));
                uint8_t tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            }
            for (int i = 0; i < 256; i++) {
                perm[i]       = p[i];
                perm[i + 256] = p[i];
            }
        }

        // Classic 1-D Perlin: fade, grad, lerp — returns roughly [-1, 1].
        float noise(float x) const {
            int   xi = ((int)fl::floorf(x)) & 255;
            float xf = x - fl::floorf(x);
            float u  = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);
            float ga = (perm[xi]     & 1) ? -xf         :  xf;
            float gb = (perm[xi + 1] & 1) ? -(xf - 1.f) : (xf - 1.f);
            return ga + u * (gb - ga);
        }

    private:
        uint8_t perm[512];
    };

    // 2D Perlin noise ---------------------------------------
    class Perlin2D {
    public:
        void init(uint32_t seed) {
            uint8_t p[256];
            for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
            uint32_t s = seed;
            for (int i = 255; i > 0; i--) {
                s = s * 1664525u + 1013904223u;
                int j = (int)((s >> 16) % (uint32_t)(i + 1));
                uint8_t tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            }
            for (int i = 0; i < 256; i++) {
                perm[i]       = p[i];
                perm[i + 256] = p[i];
            }
        }

        float noise(float x, float y) const {
            int xi = ((int)fl::floorf(x)) & 255;
            int yi = ((int)fl::floorf(y)) & 255;
            float xf = x - fl::floorf(x);
            float yf = y - fl::floorf(y);
            float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);
            float v = yf * yf * yf * (yf * (yf * 6.0f - 15.0f) + 10.0f);

            int aa = perm[perm[xi]     + yi];
            int ab = perm[perm[xi]     + yi + 1];
            int ba = perm[perm[xi + 1] + yi];
            int bb = perm[perm[xi + 1] + yi + 1];

            float x1 = lerp(grad(aa, xf, yf),         grad(ba, xf - 1.0f, yf),        u);
            float x2 = lerp(grad(ab, xf, yf - 1.0f),  grad(bb, xf - 1.0f, yf - 1.0f), u);
            return lerp(x1, x2, v);
        }

    private:
        uint8_t perm[512];

        static float grad(int h, float x, float y) {
            switch (h & 7) {
                case 0: return  x + y;
                case 1: return -x + y;
                case 2: return  x - y;
                case 3: return -x - y;
                case 4: return  x;
                case 5: return -x;
                case 6: return  y;
                default: return -y;
            }
        }

        static float lerp(float a, float b, float t) {
            return a + t * (b - a);
        }
    };

    // -------------------------------------------------------------------

    static Perlin1D noiseX, noiseY;
    static Perlin1D ampVarX, ampVarY;
    static Perlin2D noise2X, noise2Y;

    static float xProf[WIDTH];    // one noise value per column
    static float yProf[HEIGHT];   // one noise value per row


    // ═══════════════════════════════════════════════════════════════════
    //  MATH HELPERS
    // ═══════════════════════════════════════════════════════════════════

    // Non-negative float modulo (matches Python's % for positive m).
    static inline float fmodPos(float x, float m) {
        float r = fl::fmodf(x, m);
        return r < 0.0f ? r + m : r;
    }

    static inline float clampf(float v, float lo, float hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    static inline uint8_t f2u8(float v) {
        int i = (int)v;
        if (i < 0)   return 0;
        if (i > 255) return 255;
        return (uint8_t)i;
    }


    // ═══════════════════════════════════════════════════════════════════
    //  DRAWING PRIMITIVES
    // ═══════════════════════════════════════════════════════════════════

    // Full-saturation, full-brightness rainbow from a continuous hue.
    static CRGB rainbow(float t, float speed, float phase) {
        float hue = fmodPos(t * speed + phase, 1.0f);
        CHSV hsv((uint8_t)(hue * 255.0f), 255, 255);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        return rgb;
    }

    // Draw an anti-aliased sub-pixel circle into the float grid.
    static void drawCircle(float cx, float cy, float diam,
                            uint8_t cr, uint8_t cg, uint8_t cb) {
        float rad = diam * 0.5f;
        int x0 = max(0,               (int)fl::floorf(cx - rad - 1.0f));
        int x1 = min((int)WIDTH  - 1, (int)fl::ceilf (cx + rad + 1.0f));
        int y0 = max(0,               (int)fl::floorf(cy - rad - 1.0f));
        int y1 = min((int)HEIGHT - 1, (int)fl::ceilf (cy + rad + 1.0f));

        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float dx   = (x + 0.5f) - cx;
                float dy   = (y + 0.5f) - cy;
                float dist = fl::sqrtf(dx * dx + dy * dy);
                float cov  = clampf(rad + 0.5f - dist, 0.0f, 1.0f);
                if (cov <= 0.0f) continue;
                float inv = 1.0f - cov;
                gR[y][x] = gR[y][x] * inv + cr * cov;
                gG[y][x] = gG[y][x] * inv + cg * cov;
                gB[y][x] = gB[y][x] * inv + cb * cov;
            }
        }
    }

    // Blend a single pixel with weighted alpha (used by line and disc drawing).
    static void blendPixelWeighted(int px, int py,
                                    uint8_t cr, uint8_t cg, uint8_t cb,
                                    float w) {
        if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT) return;
        w = clampf(w, 0.0f, 1.0f);
        if (w <= 0.0f) return;
        float inv = 1.0f - w;
        gR[py][px] = gR[py][px] * inv + cr * w;
        gG[py][px] = gG[py][px] * inv + cg * w;
        gB[py][px] = gB[py][px] * inv + cb * w;
    }

    // Anti-aliased disc at a sub-pixel position (for line endpoints).
    static void drawAAEndpointDisc(float cx, float cy,
                                    uint8_t cr, uint8_t cg, uint8_t cb,
                                    float radius = 0.85f) {
        int x0 = max(0,               (int)fl::floorf(cx - radius - 1.0f));
        int x1 = min((int)WIDTH  - 1, (int)fl::ceilf (cx + radius + 1.0f));
        int y0 = max(0,               (int)fl::floorf(cy - radius - 1.0f));
        int y1 = min((int)HEIGHT - 1, (int)fl::ceilf (cy + radius + 1.0f));
        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                float dx   = (px + 0.5f) - cx;
                float dy   = (py + 0.5f) - cy;
                float dist = fl::sqrtf(dx * dx + dy * dy);
                float w    = clampf(radius + 0.5f - dist, 0.0f, 1.0f);
                blendPixelWeighted(px, py, cr, cg, cb, w);
            }
        }
    }

    // Anti-aliased sub-pixel line with rainbow color varying along its length.
    static void drawAASubpixelLine(float x0, float y0, float x1, float y1,
                                    float t, float colorShift) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float maxd = fl::fabsf(dx) > fl::fabsf(dy) ? fl::fabsf(dx) : fl::fabsf(dy);
        int steps = max(1, (int)(maxd * 3.0f));
        for (int i = 0; i <= steps; i++) {
            float u  = (float)i / (float)steps;
            float x  = x0 + dx * u;
            float y  = y0 + dy * u;
            int   xi = (int)fl::floorf(x);
            int   yi = (int)fl::floorf(y);
            float fx = x - xi;
            float fy = y - yi;
            CRGB c = rainbow(t, colorShift, u);
            blendPixelWeighted(xi,     yi,     c.r, c.g, c.b, (1.0f - fx) * (1.0f - fy));
            blendPixelWeighted(xi + 1, yi,     c.r, c.g, c.b, fx * (1.0f - fy));
            blendPixelWeighted(xi,     yi + 1, c.r, c.g, c.b, (1.0f - fx) * fy);
            blendPixelWeighted(xi + 1, yi + 1, c.r, c.g, c.b, fx * fy);
        }
    }


    // ═══════════════════════════════════════════════════════════════════
    //  COMPONENT TYPES & ENUMS
    // ═══════════════════════════════════════════════════════════════════

    enum EmitterType : uint8_t {
        EMITTER_ORBITAL = 0,
        EMITTER_LISSAJOUS,
        EMITTER_BORDERRECT,
        // future: EMITTER_TRIANGLE, ...
        EMITTER_COUNT
    };

    enum FlowFieldType : uint8_t {
        FLOW_NOISE = 0,
        FLOW_SPIRAL,
        // future: FLOW_CENTER, FLOW_OUTWARD, FLOW_POLARWARP, ...
        FLOW_COUNT
    };

    // Function pointer types for dispatch
    using EmitterFn     = void(*)(float t);
    using FlowPrepFn    = void(*)(float t);
    using FlowAdvectFn  = void(*)(float dt);

    
    // ═══════════════════════════════════════════════════════════════════
    //  COLOR FLOW FIELDS ===============================================
    // ═══════════════════════════════════════════════════════════════════

    /*  Quoting/paraphrasing Stefan:

    You can think of a ColorFlowField as an invisible wind that moves the previous pixels and blends them together.
    Each ColorFlowField follows its own different rules and can produce characteristic outputs:
    - spirals
    - vortices / flows towards or away from an origin (which could be staionary or dynamic)
    - polar warp flows
    - directional / geometric flows?
    - smoke/vapor?

    A ColorFlowField may use one or more noise functions in its internal pipeline

    The current/initial NoiseFlowField is especially interesting because it creates these emergent, dynamic,
    turbulence-like shapes that remind one of a fluid simulation.
    It is the result of wind blowing from two directions with varying intensities.

    */


    // ═══════════════════════════════════════════════════════════════════
    //  MODULATORS ======================================================
    // ═══════════════════════════════════════════════════════════════════

    // future:
    //Modulator sin/beatsin8/???;
    //Modulator AudioModulation;


    // ═══════════════════════════════════════════════════════════════════
    //  EMITTERS ========================================================
    // ═══════════════════════════════════════════════════════════════════

    /*  Quoting/paraphrasing Stefan:
        The emitter (aka injector / color source) is anything that is drawn directly.
        Think of it as a pencil or painbrush or paint spray gun. But it can be anything, for example:
        - bouncing balls
        - an audio-reactive pulsating ring
        - Animartrix output that still contains some black areas
        The emitter geometry can be static or dynamic (e.g., fixed rectangular border vs. orbiting dots)
        The emitter may use one or more noise functions in its internal pipeline
        The emitter output could be displayed and would be a normal animation
    */
     
    // --- Emitter parameter structs ---

    struct OrbitalParams {
        float orbitSpeed = 0.35f;
        float colorSpeed = 0.10f;
        float circleDiam = 1.5f;
        float orbitDiam  = 10.f;
    };

    struct LissajousParams {
        float endpointSpeed = 0.35f;
        float colorShift    = 0.10f;
        float lineAmplitude = (MIN_DIMENSION - 4) * 0.75f;
    };

    struct BorderRectParams {
        float colorShift = 0.10f;
    };

    // Live emitter param instances
    OrbitalParams    orbital;
    LissajousParams  lissajous;
    BorderRectParams borderRect;


    // ═══════════════════════════════════════════════════════════════════
    //  VIZUALIZER CONFIG ===============================================
    // ═══════════════════════════════════════════════════════════════════

    /* this becomes the new basic unit of organization for colorTrails
       It holds:
        - current applicable universal objects (e.g., fadeRate, axis toggles, ???)
        - an Emitter struct object
            - applicable noise elements?
            - optional Modulator(s) struct objects
        - a ColorFlowField struct object
            - applicable noise elements?
            - optional Modulator(s) struct objects
       Each struct object holds its own applicable parameter variables
    */

    struct CtVizConfig {
        // Universal params
        float fadeRate       = 0.99922f;
        bool  flipVertical   = false;   // placeholder
        bool  flipHorizontal = false;   // placeholder

        // Active component selections
        EmitterType   emitter   = EMITTER_ORBITAL;  // tied to / selected by MODE 
        FlowFieldType flowField = FLOW_NOISE; // only option for now; 
                                              // will add new UI panel w/ buttons to select   

        // Active modulators
        bool useAmpMod = true;
    };

    CtVizConfig vizConfig;

    #include "flow_noise.h"


    // ═══════════════════════════════════════════════════════════════════
    //  EMITTER IMPLEMENTATIONS
    // ═══════════════════════════════════════════════════════════════════

    // Orbiting circles — reads from `orbital`
    static void emitOrbitalDots(float t) {
        float ocx  = WIDTH  * 0.5f - 0.5f;
        float ocy  = HEIGHT * 0.5f - 0.5f;
        float orad = orbital.orbitDiam * 0.5f;
        float base = t * orbital.orbitSpeed;
        for (int i = 0; i < 3; i++) {
            float a  = base + i * (2.0f * CT_PI / 3.0f);
            float cx = ocx + fl::cosf(a) * orad;
            float cy = ocy + fl::sinf(a) * orad;
            CRGB c = rainbow(t, orbital.colorSpeed, i / 3.0f);
            drawCircle(cx, cy, orbital.circleDiam, c.r, c.g, c.b);
        }
    }

    // Lissajous line — reads from `lissajous`
    static void emitLissajousLine(float t) {
        const float c = (MIN_DIMENSION - 1) * 0.5f;
        float s = lissajous.endpointSpeed;
        const float amp = lissajous.lineAmplitude; // (MIN_DIMENSION - 4) * 0.5f;

        float lx1 = c + (amp + 1.5f) * fl::sinf(t * s * 1.13f + 0.20f);
        float ly1 = c + (amp + 0.5f) * fl::sinf(t * s * 1.71f + 1.30f);
        float lx2 = c + (amp + 2.0f) * fl::sinf(t * s * 1.89f + 2.20f);
        float ly2 = c + (amp + 1.0f) * fl::sinf(t * s * 1.37f + 0.70f);

        drawAASubpixelLine(lx1, ly1, lx2, ly2, t, lissajous.colorShift);
        CRGB ca = rainbow(t, lissajous.colorShift, 0.0f);
        CRGB cb = rainbow(t, lissajous.colorShift, 1.0f);
        drawAAEndpointDisc(lx1, ly1, ca.r, ca.g, ca.b, 0.85f);
        drawAAEndpointDisc(lx2, ly2, cb.r, cb.g, cb.b, 0.85f);
    }

    // Rainbow border rectangle — reads from `borderRect`
    static void emitRainbowBorder(float t) {
        const int total = 2 * (WIDTH + HEIGHT) - 4;
        int idx = 0;
        // Top edge: left to right
        for (int x = 0; x < WIDTH; x++) {
            CRGB c = rainbow(t, borderRect.colorShift, (float)idx / total);
            gR[0][x] = c.r; gG[0][x] = c.g; gB[0][x] = c.b;
            idx++;
        }
        // Right edge: top+1 to bottom
        for (int y = 1; y < HEIGHT; y++) {
            CRGB c = rainbow(t, borderRect.colorShift, (float)idx / total);
            gR[y][WIDTH-1] = c.r; gG[y][WIDTH-1] = c.g; gB[y][WIDTH-1] = c.b;
            idx++;
        }
        // Bottom edge: right-1 to left
        for (int x = WIDTH - 2; x >= 0; x--) {
            CRGB c = rainbow(t, borderRect.colorShift, (float)idx / total);
            gR[HEIGHT-1][x] = c.r; gG[HEIGHT-1][x] = c.g; gB[HEIGHT-1][x] = c.b;
            idx++;
        }
        // Left edge: bottom-1 to top+1
        for (int y = HEIGHT - 2; y > 0; y--) {
            CRGB c = rainbow(t, borderRect.colorShift, (float)idx / total);
            gR[y][0] = c.r; gG[y][0] = c.g; gB[y][0] = c.b;
            idx++;
        }
    }


    // ═══════════════════════════════════════════════════════════════════
    //  DISPATCH TABLES
    // ═══════════════════════════════════════════════════════════════════

    const EmitterFn EMITTER_RUN[] = {
        emitOrbitalDots,      // EMITTER_ORBITAL
        emitLissajousLine,    // EMITTER_LISSAJOUS
        emitRainbowBorder,    // EMITTER_BORDERRECT
    };

    const FlowPrepFn FLOW_PREPARE[] = {
        noiseFlowPrepare,     // FLOW_NOISE
    };

    const FlowAdvectFn FLOW_ADVECT[] = {
        noiseFlowAdvect,      // FLOW_NOISE
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

        noiseX.init(42);
        noiseY.init(1337);
        noise2X.init(42);
        noise2Y.init(1337);
        ampVarX.init(101);
        ampVarY.init(202);
    }



    // Push emitter + universal defaults into cVars (called on emitter/mode change)
    static void pushDefaultsToCVars() {
        // Universal
        cFadeRate       = vizConfig.fadeRate;
        cFlipVertical   = vizConfig.flipVertical;
        cFlipHorizontal = vizConfig.flipHorizontal;
        // Emitter: orbital
        cOrbitSpeed     = orbital.orbitSpeed;
        cColorSpeed     = orbital.colorSpeed;
        cCircleDiam     = orbital.circleDiam;
        cOrbitDiam      = orbital.orbitDiam;
        // Emitter: lissajous / borderRect
        cEndpointSpeed  = lissajous.endpointSpeed;
        cColorShift     = lissajous.colorShift;
        cLineAmplitude  = lissajous.lineAmplitude;
    }

    // Read cVars into component structs (called every frame)
    static void syncFromCVars() {
        vizConfig.fadeRate       = cFadeRate;
        vizConfig.flipVertical   = cFlipVertical;
        vizConfig.flipHorizontal = cFlipHorizontal;
        orbital.orbitSpeed       = cOrbitSpeed;
        orbital.colorSpeed       = cColorSpeed;
        orbital.circleDiam       = cCircleDiam;
        orbital.orbitDiam        = cOrbitDiam;
        lissajous.endpointSpeed  = cEndpointSpeed;
        lissajous.colorShift     = cColorShift;
        lissajous.lineAmplitude  = cLineAmplitude;
        borderRect.colorShift    = cColorShift;
        // Flow field + modulator
        syncFlowFromCVars();
    }

    void runColorTrails() {
        unsigned long now = fl::millis();
        float dt = (now - lastFrameMs) * 0.001f;
        lastFrameMs = now;
        float t = (now - t0) * 0.001f;

        // Map AuroraPortal MODE to emitter selection
        if (MODE < EMITTER_COUNT && MODE != lastEmitter) {
            vizConfig.emitter = (EmitterType)MODE;
            lastEmitter = MODE;
            pushDefaultsToCVars();
            sendVisualizerState();
        }

        // Detect flow field changes
        uint8_t currentFlowField = (uint8_t)vizConfig.flowField;
        if (currentFlowField < FLOW_DISPATCH_COUNT && currentFlowField != lastFlowField) {
            lastFlowField = currentFlowField;
            pushFlowDefaultsToCVars();
            sendVisualizerState();
        }

        // Sync UI-controlled values into component structs
        syncFromCVars();

        // 1. Flow field: prepare (build profiles, apply modulators, apply flips)
        FLOW_PREPARE[vizConfig.flowField](t);

        // 2. Emitter: inject color onto grid
        EMITTER_RUN[vizConfig.emitter](t);

        // 3. Flow field: advect + fade
        FLOW_ADVECT[vizConfig.flowField](dt);

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
