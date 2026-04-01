# Eliminating Color Banding in Float-Precision LED Pipelines

## Summary

When building a visualization system that uses float-precision grids for anti-aliased sub-pixel rendering, we discovered two compounding uint8 bottlenecks in the standard FastLED color workflow that produce visible color banding on large LED matrices. This report documents the root causes, quantifies the impact, and describes the float-precision workarounds we implemented. We also reimplemented FastLED's `hsv2rgb_rainbow` color curve in float to preserve its perceptual character without the quantization artifacts.

## Context

**Project**: ColorTrails — an ESP32-S3 LED visualization system using FastLED, rendering onto 22x22 and 32x48 WS2812 matrices. The pipeline uses float-precision RGB grids for smooth anti-aliased sub-pixel drawing (dots, lines, flow field advection via bilinear interpolation). Colors are composited at float precision and quantized to uint8 only at the final LED copy step.

**FastLED version**: 3.10.3 (pinned master commit `e229673`)

**Symptom**: Visible bands of identical color on the display, especially at slow `colorShift` speeds where hue changes gradually across the grid. The banding appears as discrete steps rather than smooth gradients, despite the entire rendering pipeline being float-based.

---

## Root Cause 1: uint8 Hue Quantization in `CHSV`

### The Problem

The standard FastLED workflow uses `CHSV` with a `uint8_t` hue (0-255) for HSV-to-RGB conversion:

```cpp
// Typical usage
float hue = fmodPos(t * speed + phase, 1.0f);   // continuous float hue
CHSV hsv((uint8_t)(hue * 255.0f), 255, 255);     // ← quantized to 256 steps
CRGB rgb;
hsv2rgb_rainbow(hsv, rgb);
```

With only 256 possible hue values, the hue wheel is divided into coarse steps. In `hsv2rgb_rainbow`'s 8-section structure, each section covers ~32 hue values. Within a section, only one or two RGB channels are ramping, and each hue step produces a **~6-unit jump** in the ramping channel:

```
Section 0 (R→O): 32 steps ramping green from 0 to ~85
  → each hue step ≈ 2.6 green units

Section 1 (O→Y): 32 steps ramping green from 85 to 170
  → each hue step ≈ 2.6 green units

Combined: green ramps 0→170 over 64 hue steps ≈ 2.6 units/step
```

For `hsv2rgb_spectrum` (6 even sectors), each sector covers ~42 hue values with one channel ramping 0→255:

```
Per sector: 255 / 42 ≈ 6 RGB units per hue step
```

On a 32x48 display showing a slow rainbow gradient, multiple adjacent pixels map to the same uint8 hue value, creating visible bands of identical color.

### The Fix

Replace `CHSV` + `hsv2rgb_rainbow` with a direct float-precision HSV→RGB conversion:

```cpp
struct ColorF { float r, g, b; };

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
    }
    return ColorF{r * 255.0f, g * 255.0f, b * 255.0f};
}
```

The hue input is now continuous (float), so each pixel can have a unique hue value. Adjacent pixels might differ by a fraction of one RGB unit — no banding.

---

## Root Cause 2: uint8 `CRGB` in the Drawing Pipeline

### The Problem

Fixing the hue quantization helped but did not eliminate banding entirely. The second bottleneck: even with float HSV→RGB conversion, the result was being stored in `CRGB` (uint8 per channel) before being written to the float grid:

```cpp
// After fixing hue quantization — still banding!
static CRGB rainbow(float t, float speed, float phase) {
    float hue = fmodPos(t * speed + phase, 1.0f);
    // ... float HSV→RGB math ...
    return CRGB((uint8_t)(r * 255.0f), (uint8_t)(g * 255.0f), (uint8_t)(b * 255.0f));
    //          ^^^^^^^^^ truncated to uint8 HERE
}

// Drawing function receives uint8 colors
static void drawDot(float cx, float cy, float diam,
                    uint8_t cr, uint8_t cg, uint8_t cb) {
    // ... anti-aliased sub-pixel blending into float grid ...
    gR[y][x] = gR[y][x] * inv + cr * cov;   // cr is uint8, gR is float
}
```

The float grid receives pre-quantized color values. While the blending math produces float intermediates, the source colors are stepped at 256 levels per channel. For a slowly-changing gradient, many adjacent pixels receive identical uint8 RGB values, creating bands.

### The Fix

Keep colors as floats from generation through all drawing functions. Uint8 quantization happens only at the final LED copy step:

```cpp
// Rainbow returns float-precision color
static ColorF rainbow(float t, float speed, float phase) { ... }

// All drawing functions accept float colors
static void drawDot(float cx, float cy, float diam,
                    float cr, float cg, float cb) { ... }

static void blendPixelWeighted(int px, int py,
                                float cr, float cg, float cb,
                                float w) { ... }

// Quantization happens ONLY here — at the hardware boundary
for (uint8_t y = 0; y < HEIGHT; y++)
    for (uint8_t x = 0; x < WIDTH; x++) {
        leds[idx].r = f2u8(gR[y][x]);  // float → uint8 at the very end
        leds[idx].g = f2u8(gG[y][x]);
        leds[idx].b = f2u8(gB[y][x]);
    }
```

With both fixes combined, the color pipeline is fully float-precision from HSV generation through anti-aliased sub-pixel blending. The only uint8 quantization is at the LED hardware boundary, where it's unavoidable and happens per-pixel with no spatial banding.

---

## Bonus: Float-Precision `hsv2rgb_rainbow` Reimplementation

FastLED's `hsv2rgb_rainbow` produces a perceptually more even rainbow than standard HSV (compressed yellow, expanded orange/purple). After switching to float-precision spectrum HSV, we wanted the option to use the FastLED rainbow character without the uint8 banding. We reimplemented the 8-section piecewise curve from `hsv2rgb_rainbow` (Y1 mode) in float:

```cpp
// Float-precision reimplementation of FastLED's hsv2rgb_rainbow (Y1 mode).
// 8-section piecewise curve matching the FastLED rainbow perceptual character.
// Derived from FastLED's hsv2rgb.cpp.hpp, lines 268-493.
static ColorF hsvRainbow(float hue) {
    float h8 = hue * 8.0f;
    int section = (int)h8;
    float frac = h8 - section;
    float third = frac * 85.0f;
    float twothirds = frac * 170.0f;
    float r, g, b;
    switch (section % 8) {
        case 0: r = 255.0f - third;       g = third;              b = 0.0f;               break;
        case 1: r = 171.0f;               g = 85.0f + third;      b = 0.0f;               break;
        case 2: r = 171.0f - twothirds;   g = 170.0f + third;     b = 0.0f;               break;
        case 3: r = 0.0f;                 g = 255.0f - third;     b = third;              break;
        case 4: r = 0.0f;                 g = 171.0f - twothirds; b = 85.0f + twothirds;  break;
        case 5: r = third;                g = 0.0f;               b = 255.0f - third;     break;
        case 6: r = 85.0f + third;        g = 0.0f;               b = 171.0f - third;     break;
        case 7: r = 170.0f + third;       g = 0.0f;               b = 85.0f - third;      break;
    }
    return ColorF{r, g, b};
}
```

This produces the same color curve as FastLED's rainbow — including the yellow compression and the 8-section transition structure — but with continuous float precision. We expose both modes via a runtime toggle so the user can switch between even spectrum and FastLED rainbow character.

---

## Note on `fl::HSV16`

FastLED 3.10.3 (commit `e229673`) includes `fl::HSV16` (in `fl/gfx/hsv16.h`) which uses 16-bit hue/saturation/value. This addresses the hue quantization issue (65,536 hue steps vs 256), but:

1. **Spectrum only** — `HSV16::ToRGB()` implements standard 6-sector HSV, not the `hsv2rgb_rainbow` perceptual curve. There is no rainbow-character variant of HSV16.
2. **Still outputs `CRGB`** — `ToRGB()` returns `CRGB` (uint8 per channel), so Root Cause 2 (uint8 colors in the drawing pipeline) is not addressed. For float-grid pipelines, the color values are still pre-quantized before reaching the grid.

For projects using standard `CRGB`-based pipelines without float grids, `fl::HSV16` is likely sufficient to address hue banding. For float-precision pipelines, the full float approach described above is necessary.

---

## Impact

| Metric | Before | After |
|--------|--------|-------|
| Hue precision | 256 steps (uint8) | Continuous (float32) |
| RGB color precision at grid write | 256 levels/channel (uint8) | Continuous (float32) |
| Effective unique colors in rainbow | ~1,536 (256 hue × 6 sectors) | Limited only by float32 mantissa |
| Max RGB jump between adjacent hue values | ~6 units/channel | Sub-unit (< 1.0) |
| Flash size delta | — | -452 bytes (removed hsv2rgb lookup tables) |
| FPS impact | — | None measured (48 FPS maintained) |

---

## Recommendations for FastLED

1. **Consider a `ColorF` or `CRGB16` type** for use in float/16-bit rendering pipelines. The current `CRGB` forces uint8 quantization at every color operation boundary, which compounds through a multi-stage pipeline.

2. **Consider a float-precision or 16-bit variant of `hsv2rgb_rainbow`**. The existing `fl::HSV16` only implements spectrum HSV. Users who want the rainbow perceptual curve with higher precision currently have to reimplement the 8-section algorithm themselves.

3. **Document the banding risk** when using `CHSV`/`CRGB` in pipelines that maintain higher internal precision. The issue is subtle — the uint8 truncation happens silently, and the banding is only visible on larger displays or with slow color gradients.

---

## Related FastLED Issues

- [#756 — 8-bit HSV color animations produce poor results & blinking](https://github.com/FastLED/FastLED/issues/756)
- [#1394 — Using HSV with CRGBPalette16 generates poor result](https://github.com/FastLED/FastLED/issues/1394)
- [#132 — rgb2hsv_rainbow?](https://github.com/FastLED/FastLED/issues/132)
- [#436 — rgb2hsv_approximate returns completely wrong color](https://github.com/FastLED/FastLED/issues/436)
- [#11 — Add support for 16-bit CRGB class](https://github.com/FastLED/FastLED/issues/11)

## References

- [FastLED HSV Colors Wiki](https://github.com/FastLED/FastLED/wiki/FastLED-HSV-Colors)
- [FastLED hsv2rgb.cpp.hpp source](https://github.com/FastLED/FastLED/blob/master/src/hsv2rgb.cpp.hpp) — lines 268-493 for `hsv2rgb_rainbow`
- [fl::HSV16 source](https://github.com/FastLED/FastLED/blob/master/src/fl/gfx/hsv16.h) — 16-bit HSV (spectrum only)
