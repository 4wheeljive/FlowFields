#pragma once

// ═══════════════════════════════════════════════════════════════════
//  flowFieldsTypes.h — Shared types, constants, noise generators,
//  and math helpers for the flowFields system.
//  (Variable instances and drawing primitives live in FlowFieldsEngine.)
// ═══════════════════════════════════════════════════════════════════

#include <FastLED.h>
#include "componentEnums.h"

namespace flowFields {

    constexpr float CT_PI = 3.14159265358979f;
    constexpr float CT_2PI = 6.28318530717958f;

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
        if (v <= 0.0f)   return 0;
        if (v >= 255.0f) return 255;
        // Round to nearest to reduce low-end bias vs truncation.
        int i = (int)(v + 0.5f);
        return (uint8_t)i;
    }

    // 4x4 Bayer matrix for ordered output dithering. Values are centered at 0
    // with range ~[-0.47, +0.47] (16 distinct values, 1/16 spacing).
    static const float bayerOutputDither[4][4] = {
        { -7.5f / 16.0f, +0.5f / 16.0f, -5.5f / 16.0f, +2.5f / 16.0f },
        { +4.5f / 16.0f, -3.5f / 16.0f, +6.5f / 16.0f, -1.5f / 16.0f },
        { -4.5f / 16.0f, +3.5f / 16.0f, -6.5f / 16.0f, +1.5f / 16.0f },
        { +7.5f / 16.0f, -0.5f / 16.0f, +5.5f / 16.0f, -2.5f / 16.0f }
    };

    // Dithered float→uint8: shifts the rounding boundary spatially per pixel.
    static inline uint8_t f2u8d(float v, int x, int y) {
        return f2u8(v + bayerOutputDither[y & 3][x & 3]);
    }

    // Wrapper functions that take radians and return float (-1.0 to 1.0)
    // Using FastLED's sin32/cos32 approximations for better performance
    constexpr float RADIANS_TO_SIN32 = 2671177.0f;  // 16777216 / (2*PI)
    constexpr float SIN32_TO_FLOAT = 1.0f / 2147418112.0f;

    inline float sin_fast(float angle_radians) {
        uint32_t angle_sin32 = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        return fl::sin32(angle_sin32) * SIN32_TO_FLOAT;
    }

    inline float cos_fast(float angle_radians) {
        uint32_t angle_cos32 = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        return fl::cos32(angle_cos32) * SIN32_TO_FLOAT;
    }

    // Combined sin+cos from a single LUT pass.
    struct SinCosResult { float sin_val; float cos_val; };

    inline SinCosResult sincos_fast(float angle_radians) {
        uint32_t angle = (uint32_t)(angle_radians * RADIANS_TO_SIN32);
        fl::SinCos32 sc = fl::sincos32(angle);
        return { sc.sin_val * SIN32_TO_FLOAT, sc.cos_val * SIN32_TO_FLOAT };
    }

    #define FL_SIN_F(x) sin_fast(x)
    #define FL_COS_F(x) cos_fast(x)

    // IEEE 754 bit-trick fast power for base in [0,1]. ~5% error, 10-20x faster than powf.
    inline float fastpow(float base, float exp) {
        union { float f; int32_t i; } v = { base };
        v.i = (int32_t)(exp * (v.i - 1065353216) + 1065353216);
        return v.f;
    }

    // ═══════════════════════════════════════════════════════════════════
    //  NOISE GENERATORS
    // ═══════════════════════════════════════════════════════════════════
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

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

        // Returns roughly [-0.5, 0.5].
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

    // 2D Value noise (cheaper than Perlin gradient — no grad() dispatch)
    class ValueNoise2D {
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

            // Corner values from perm table, scaled to [-1, 1]
            constexpr float S = 1.0f / 127.5f;
            float n00 = perm[perm[xi]     + yi]     * S - 1.0f;
            float n10 = perm[perm[xi + 1] + yi]     * S - 1.0f;
            float n01 = perm[perm[xi]     + yi + 1] * S - 1.0f;
            float n11 = perm[perm[xi + 1] + yi + 1] * S - 1.0f;

            float x1 = n00 + u * (n10 - n00);
            float x2 = n01 + u * (n11 - n01);
            return x1 + v * (x2 - x1);
        }

    private:
        uint8_t perm[512];
    };

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

    // ═══════════════════════════════════════════════════════════════════
    //  COMPONENT TYPES
    // ═══════════════════════════════════════════════════════════════════

    // Function pointer types for dispatch
    using EmitterFn     = void(*)();
    using FlowPrepFn    = void(*)();
    using FlowAdvectFn  = void(*)();

    // ═══════════════════════════════════════════════════════════════════
    //  MODULATION TYPES
    // ═══════════════════════════════════════════════════════════════════

    struct ModConfig {
        // Hardcoded by developer — architectural choices, set on the instance in the emitter file
        uint8_t modTimer = 0;          // which timer index to read from (0 to num_timers)

        // UI-tunable via cVars — struct values are defaults, overwritten by syncFromCVars()
        float   modRate  = 0.0f;       // UI adjustment to timings.ratio[timer] (developer uses in formula)
        float   modLevel = 0.0f;       // modulation depth (0 = mod off)
    };

} // namespace flowFields
