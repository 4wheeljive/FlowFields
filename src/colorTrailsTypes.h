#pragma once

// ═══════════════════════════════════════════════════════════════════
//  colorTrailsTypes.h — Anchor header for the colorTrails system.
//  All shared types, constants, global instances, noise generators,
//  math helpers, and drawing primitives live here.
//  Sub-headers (flow_noise.h, future emitter headers) include this.
// ═══════════════════════════════════════════════════════════════════

#include <FastLED.h>
#include "componentEnums.h"

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
    uint8_t lastEmitter = 255;  // force initial setup on first frame
    uint8_t lastFlow = 255;  // force initial setup on first frame


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

    // Wrapper functions that take radians and return float (-1.0 to 1.0)
    // Using FastLED's sin32/cos32 approximations for better performance
    constexpr float RADIANS_TO_SIN32 = 2671177.0f;  // 16777216 / (2*PI)
    constexpr float SIN32_TO_FLOAT = 1.0f / 2147418112.0f;  // reciprocal for multiply instead of divide

    inline float sin_fast(float angle_radians) {
        uint32_t angle_sin32 = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        return fl::sin32(angle_sin32) * SIN32_TO_FLOAT;
    }

    inline float cos_fast(float angle_radians) {
        uint32_t angle_cos32 = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        return fl::cos32(angle_cos32) * SIN32_TO_FLOAT;
    }

    /*
    // Combined sin+cos from a single LUT pass — one radians->uint32 conversion,
    // shared table lookups. Used in render_value where both are needed for the
    // same angle.
    struct SinCosResult { float sin_val; float cos_val; };

    inline SinCosResult sincos_fast(float angle_radians) {
        uint32_t angle = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        fl::SinCos32 sc = fl::sincos32(angle);
        return { sc.sin_val * SIN32_TO_FLOAT, sc.cos_val * SIN32_TO_FLOAT };
    }
    */

    #define FL_SIN_F(x) sin_fast(x)
    #define FL_COS_F(x) cos_fast(x)

    /*
    // IEEE 754 bit-trick fast power for base in [0,1]. ~5% error, 10-20x faster than powf.
    inline float fastpow(float base, float exp) {
        union { float f; int32_t i; } v = { base };
        v.i = (int32_t)(exp * (v.i - 1065353216) + 1065353216);
        return v.f;
    }
    */

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

    // Noise instances
    static Perlin1D noiseX, noiseY;
    static Perlin1D ampVarX, ampVarY;
    static Perlin2D noise2X, noise2Y;

    static float xProf[WIDTH];    // one noise value per column
    static float yProf[HEIGHT];   // one noise value per row


    // ═══════════════════════════════════════════════════════════════════
    //  MODULATORS
    // ═══════════════════════════════════════════════════════════════════

    #define num_timers 10

    struct timers {
        float offset[num_timers];  // timers can be separated by a time offset
        float ratio[num_timers];   // ratio determines time-sensitivity
    };

    struct modulators {
        float linear[num_timers];               // returns 0 to FLT_MAX
        float radial[num_timers];               // returns 0 to 2*PI
        float directional[num_timers];          // returns -1 to 1
        float noise_directional[num_timers];    // returns -1 to 1
        float noise_angle[num_timers];          // returns 0 to 2*PI
    };

    timers timings;     // timer inputs; all time/speed settings in one place
    modulators move;    // timer outputs; all time-based modulators in one place

    
    void calculate_modulators(timers &timings) {

            float runtime = fl::millis(); 

            for (uint8_t i = 0; i < num_timers; i++) {

                // continously rising offsets, returns 0 to max_float
                move.linear[i] = 
                    (runtime + timings.offset[i]) * timings.ratio[i];

                // angle offsets for continous rotation, returns 0 to 2 * PI
                move.radial[i] = 
                    fl::fmodf(move.linear[i], 2 * PI); 

                // directional offsets or factors, returns -1 to 1
                move.directional[i] = 
                    fl::sinf(move.radial[i]);
                    
                // noise-based directional, offsets or factors, returns -1 to 1  
                move.noise_directional[i] = 
                    noiseX.noise(move.linear[i]);

                // noise based angle offset, returns 0 to 2 * PI
                move.noise_angle[i] =
                    PI * (1.f + move.noise_directional[i]); // noiseX.noise(move.linear[i])
            
            }
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

    // Draw an anti-aliased sub-pixel dot into the float grid.
    static void drawDot(float cx, float cy, float diam,
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

    void flipAxisX(){   
        for (int i = 0; i < HEIGHT / 2; i++) {
            float tmp = yProf[i];
            yProf[i] = yProf[HEIGHT - 1 - i];
            yProf[HEIGHT - 1 - i] = tmp;
        }
    }

    void flipAxisY() {
        for (int i = 0; i < WIDTH / 2; i++) {
            float tmp = xProf[i];
            xProf[i] = xProf[WIDTH - 1 - i];
            xProf[WIDTH - 1 - i] = tmp;
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  COMPONENT TYPES & ENUMS
    // ═══════════════════════════════════════════════════════════════════

    // Emitter and Flow enums defined in componentEnums.h

    // Function pointer types for dispatch
    using EmitterFn     = void(*)(float t);
    using FlowPrepFn    = void(*)(float t);
    using FlowAdvectFn  = void(*)(float dt);

    // ═══════════════════════════════════════════════════════════════════
    //  EMITTERS
    // ═══════════════════════════════════════════════════════════════════

    // --- Emitter parameter structs ---

    struct OrbitalDotsParams {
        uint8_t numDots = 3;
        float orbitSpeed = 3.0f;
        float dotDiam = 1.5f;
        float orbitDiam  = 10.f;
        float modOrbitSpeed = 0.3f;   // noise variation depth (0=constant, 1=can stop, >1=reverses)
    };

    struct SwarmingDotsParams {
        uint8_t numDots = 3;        // number of dots (1–5, limited by num_timers=10)
        float swarmSpeed = 0.5f;    // overall speed of swarming motion
        float swarmSpread = 1.0f;   // 0 = tight cluster, 1 = normal, >1 = wide spread
        float dotDiam = 1.5f;       // dot size
    };

    struct LissajousParams {
        float lineSpeed = 0.35f;
        float lineAmp = (MIN_DIMENSION - 4) * 0.75f;
    };

    struct BorderRectParams {
    };

    // Live emitter param instances
    OrbitalDotsParams   orbitalDots;
    SwarmingDotsParams  swarmingDots;
    LissajousParams     lissajous;
    BorderRectParams    borderRect;



    // RELOCATE/REFACTOR MODULATOR FUNCTIONALITY BELOW   
    // --- Amplitude modulator ---

    struct AmpModParams {
        float intensity = 4.0f;    // Depth of amplitude modulation (0 = off)
        float speed     = 1.0f;    // Temporal speed of the variation noise
        bool  active    = false;   // on/off
    };

    AmpModParams ampMod;

    //  Slow 1D Perlin noise modulates the flow field's xAmplitude/yAmplitude.
    //  Operates on working copies (does not mutate base noiseFlow params).

    static void applyAmpModulation(float t, float& xAmp, float& yAmp) {
        if (!ampMod.active) return;

        float nVarX = ampVarX.noise(t * 0.16f * ampMod.speed);
        float nVarY = ampVarY.noise(t * 0.13f * ampMod.speed + 17.0f);

        float selfMod = 0.5f + 0.5f * ((nVarX + nVarY) * 0.5f);
        float effVariation = ampMod.intensity * selfMod;

        xAmp = clampf(xAmp + nVarX * 0.45f * effVariation, 0.10f, 1.0f);
        yAmp = clampf(yAmp + nVarY * 0.45f * effVariation, 0.10f, 1.0f);
    }


    // ═══════════════════════════════════════════════════════════════════
    //  VIZUALIZER CONFIG
    // ═══════════════════════════════════════════════════════════════════

    struct CtVizConfig {
        // Universal params
        float persistence = 14.8f;  // trail half-life in seconds
        float colorShift = 0.10f;
        bool flipY = false;
        bool flipX = false;

        // Active component selections
        Emitter emitter = EMITTER_ORBITALDOTS;
        Flow flow = FLOW_NOISE;

        // Active modulators
        bool useAmpMod = true;
    };

    CtVizConfig vizConfig;

} // namespace colorTrails
