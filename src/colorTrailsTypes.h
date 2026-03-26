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
    bool useRainbow = false;  // false = spectrum (even HSV), true = FastLED rainbow character


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

    struct ColorF { float r, g, b; };

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
    //  DRAWING PRIMITIVES
    // ═══════════════════════════════════════════════════════════════════

    // Spectrum: standard HSV with even 60° sectors.
    static ColorF hsvSpectrum(float hue) {
        float h6 = hue * 6.0f;
        int sector = (int)h6;
        float frac = h6 - sector;
        float r, g, b;
        switch (sector % 6) {
            case 0: r = 1.0f;        g = frac;        b = 0.0f;        break;
            case 1: r = 1.0f - frac; g = 1.0f;        b = 0.0f;        break;
            case 2: r = 0.0f;        g = 1.0f;        b = frac;        break;
            case 3: r = 0.0f;        g = 1.0f - frac; b = 1.0f;        break;
            case 4: r = frac;        g = 0.0f;        b = 1.0f;        break;
            case 5: r = 1.0f;        g = 0.0f;        b = 1.0f - frac; break;
            default: r = g = b = 0.0f; break;
        }
        return ColorF{r * 255.0f, g * 255.0f, b * 255.0f};
    }

    // FastLED rainbow character in float precision (no uint8 banding).
    // 8-section piecewise curve: compressed yellow, expanded red/blue/purple.
    // Derived from FastLED's hsv2rgb_rainbow (Y1 mode).
    static ColorF hsvRainbow(float hue) {
        float h8 = hue * 8.0f;
        int section = (int)h8;
        float frac = h8 - section;
        float third = frac * 85.0f;
        float twothirds = frac * 170.0f;
        float r, g, b;
        switch (section % 8) {
            case 0: r = 255.0f - third; g = third;            b = 0.0f;              break; // R → O
            case 1: r = 171.0f;         g = 85.0f + third;    b = 0.0f;              break; // O → Y
            case 2: r = 171.0f - twothirds; g = 170.0f + third; b = 0.0f;            break; // Y → G
            case 3: r = 0.0f;           g = 255.0f - third;   b = third;             break; // G → A
            case 4: r = 0.0f;           g = 171.0f - twothirds; b = 85.0f + twothirds; break; // A → B
            case 5: r = third;          g = 0.0f;             b = 255.0f - third;    break; // B → P
            case 6: r = 85.0f + third;  g = 0.0f;             b = 171.0f - third;    break; // P → K
            case 7: r = 170.0f + third; g = 0.0f;             b = 85.0f - third;     break; // K → R
            default: r = g = b = 0.0f; break;
        }
        return ColorF{r, g, b};
    }

    // Full-saturation, full-brightness rainbow from a continuous hue.
    // Float-precision HSV→RGB eliminates banding from uint8 hue quantization.
    // useRainbow toggles between even spectrum and FastLED rainbow character.
    static ColorF rainbow(float t, float speed, float phase) {
        float hue = fmodPos(t * speed + phase, 1.0f);
        return useRainbow ? hsvRainbow(hue) : hsvSpectrum(hue);
    }

    // Draw an anti-aliased sub-pixel dot into the float grid.
    static void drawDot(float cx, float cy, float diam,
                            float cr, float cg, float cb) {
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
                                    float cr, float cg, float cb,
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
                                    float cr, float cg, float cb,
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
            ColorF c = rainbow(t, colorShift, u);
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

    // ═══════════════════════════════════════════════════════════════════
    //  MODULATION TYPES
    // ═══════════════════════════════════════════════════════════════════

    // Waveform source — maps 1:1 to modulators struct fields, native ranges preserved
    enum ModType : uint8_t {
        MOD_NONE              = 0,
        MOD_LINEAR            = 1,   // move.linear[timer],            0 to FLT_MAX
        MOD_RADIAL            = 2,   // move.radial[timer],            0 to 2*PI
        MOD_DIRECTIONAL_SINE  = 3,   // move.directional_sine[timer],  -1 to 1
        MOD_DIRECTIONAL_NOISE = 4,   // move.directional_noise[timer], -1 to 1
        MOD_RADIAL_NOISE      = 5,   // move.radial_noise[timer],      0 to 2*PI
    };

    // Mathematical operation applied to base value
    enum ModOp : uint8_t {
        OP_SCALE = 0,   // base * (1 + level * wave)  — percentage perturbation around base
        OP_ADD   = 1,   // base + level * wave         — fixed-magnitude offset from base
    };

    struct ModConfig {
        // Hardcoded by developer — architectural choices, set on the instance in the emitter file
        ModType type  = MOD_NONE;   // which move.* output to read
        ModOp   op    = OP_SCALE;   // how the wave modifies the base value
        uint8_t timer = 0;          // which timer index to read from (0–9)

        // UI-tunable via cVars — struct values are defaults, overwritten by syncFromCVars()
        float   rate  = 0.0f;       // UI adjustment to timings.ratio[timer] (developer uses in formula)
        float   level = 0.0f;       // modulation depth (0 = mod off)
    };

    // ═══════════════════════════════════════════════════════════════════
    //  EMITTER PARAM STRUCTS
    // ═══════════════════════════════════════════════════════════════════
    //  Struct definitions only — instances live in emitters.h
    //  alongside their functions so the developer can see and tune
    //  all values in one place.

    struct OrbitalDotsParams {
        uint8_t numDots;
        float orbitSpeed;
        ModConfig modOrbitSpeed;
        float dotDiam;
        float orbitDiam;
        ModConfig modOrbitDiam;
    };

    struct SwarmingDotsParams {
        uint8_t numDots;
        float swarmSpeed;
        float swarmSpread;
        float dotDiam;
    };

    struct LissajousParams {
        float lineSpeed;
        float lineAmp;
    };

    struct BorderRectParams {
    };

    struct AudioDotsParams {
        float dotDiam;
    };


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
