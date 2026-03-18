# ColorTrails: Python → AuroraPortal Translation Guide

This document defines how to translate Stefan Petrick's Python Pygame sketches
(`perlin_grid_visualization_*.py`) into AuroraPortal's C++ colorTrails program.

---

## 1. File Locations

| Role | Path |
|------|------|
| Python originals | `colorTrailsOrig/perlin_grid_visualization_*.py` |
| AuroraPortal detail | `src/programs/colorTrails_detail.hpp` |
| AuroraPortal header | `src/programs/colorTrails.hpp` |
| BLE/param registration | `src/bleControl.h` |
| Web UI | `index.html` |
| Standalone C++ port (testing) | `C:\...\ColorTrails\src\colorTrails.h` |

---

## 2. What to Port vs. What to Ignore

### Port
- **Noise classes** (Perlin1D, Perlin2D, etc.)
- **`sample_profile()`** — the bridge between noise and the advection engine
- **Emitter / injection functions** (`inject_*`, orbiting circles logic)
- **Drawing helpers** needed by emitters (blend_pixel_weighted, draw_aa_subpixel_line, etc.)
- **New slider parameters** — become CTParams fields + BLE cVars
- **Profile manipulation** (e.g., `x_profile = list(reversed(x_profile))`)

### Ignore
- All Pygame UI: Slider class, draw_grid, draw_x_graph, draw_y_graph
- Window/event management, surface scaling, display code
- Layout constants: CELL_SIZE, MARGIN, WINDOW_WIDTH, etc.
- Color constants for UI chrome: BG, PANEL, TRACK, FILL, KNOB, TEXT, etc.

### Flag for Review (do not auto-port)
Any change to an existing shared function (`advect_axes_and_dim`, `sample_profile`,
`rainbow_color_with_phase`, etc.) must be flagged before porting. Changes fall into
two categories:

1. **Mechanical** — optimization, bug fix, code cleanup that doesn't change visual
   output. These should supersede the existing C++ logic silently.
2. **Artistic** — changes the visual character (e.g., different fade curve, noise
   shaping, color mapping). These should be presented as a choice:
   - Replace the existing behavior?
   - Add as a switchable option (new param or mode)?
   - Ignore?

When in doubt, flag it. Present a diff of the Python change and describe the
visual impact before making any modification to the shared C++ engine.

---

## 3. Math Function Mapping

AuroraPortal uses FastLED's `fl::` namespace. **Always** use `fl::` prefixed
versions; bare `floorf`, `sinf`, etc., cause ambiguity errors.

| Python | C++ (AuroraPortal) |
|--------|--------------------|
| `math.floor(x)` | `fl::floorf(x)` |
| `math.ceil(x)` | `fl::ceilf(x)` |
| `math.sin(x)` | `fl::sinf(x)` |
| `math.cos(x)` | `fl::cosf(x)` |
| `abs(x)` / `math.fabs(x)` | `fl::fabsf(x)` |
| `math.sqrt(x)` | `fl::sqrtf(x)` |
| `math.hypot(dx, dy)` | `fl::sqrtf(dx*dx + dy*dy)` |
| `math.pow(x, y)` | `fl::powf(x, y)` |
| `x % m` (float, positive m) | `fmodPos(x, m)` helper (wraps `fl::fmodf`) |
| `max(a, b)` / `min(a, b)` | `max(a, b)` / `min(a, b)` (Arduino macros, OK as-is) |

---

## 4. Grid / Pixel Storage

| Python | C++ |
|--------|-----|
| `pygame.Surface((GRID_SIZE, GRID_SIZE))` | `float gR[HEIGHT][WIDTH], gG[...], gB[...]` (live) + `tR, tG, tB` (scratch) |
| `grid_surface.get_at((x, y)).r` | `gR[y][x]` |
| `grid_surface.set_at((x, y), (r,g,b))` | `gR[y][x] = r; gG[y][x] = g; gB[y][x] = b;` |
| `src = grid_surface.copy()` | The `tR/tG/tB` scratch buffers serve this role during advection |

Note: Python stores uint8 per pixel; C++ uses float grids and truncates via
`fl::floorf()` in `advectAndDim()` to match the uint8 truncation behavior
that prevents sub-1.0 residual wash.

---

## 5. Color

| Python | C++ |
|--------|-----|
| `colorsys.hsv_to_rgb(hue, 1.0, 1.0)` → `(r,g,b)` floats 0–1 | `hsv2rgb_rainbow(CHSV(hue*255, 255, 255), rgb)` → `CRGB` with uint8 0–255 |
| `rainbow_color_with_phase(t, speed, phase)` | `rainbow(t, speed, phase)` returning `CRGB` |
| Color tuples `(r, g, b)` as int 0–255 | `CRGB` struct or separate `uint8_t cr, cg, cb` args |

---

## 6. Time

| Python | C++ |
|--------|-----|
| `pygame.time.get_ticks() / 1000.0` | `fl::millis()` (returns `unsigned long` in ms) |
| `t = ticks - start_time` | `float t = (now - t0) * 0.001f;` |
| `dt = clock.tick(FPS) / 1000.0` | `float dt = (now - lastFrameMs) * 0.001f;` |
| Fade at fixed 60fps | Frame-rate-independent: `powf(fadePct*0.01, 60.0)` then `powf(fadePerSec, dt)` |

---

## 7. Size / Dimension Constants

| Python | C++ |
|--------|-----|
| `GRID_SIZE` (assumes square) | `WIDTH`, `HEIGHT`, or `MIN_DIMENSION` when square assumption needed |
| `GRID_SIZE - 1` | `WIDTH - 1` / `HEIGHT - 1` |
| Hardcoded pixel amplitudes (e.g., `11.5`) | Derive from `MIN_DIMENSION`: `(MIN_DIMENSION - 4) * 0.5f + offset` |

---

## 8. Noise Classes

### Perlin1D (used in _1 and _2)
- Python: `Perlin1D(seed)` with `noise(x: float) → float`
- C++: `class Perlin1D` with `init(uint32_t seed)` and `noise(float x) → float`
- Defined **outside** `namespace colorTrails` (before it)
- Uses Fisher-Yates shuffle with LCG (matches Python's `random.shuffle`)

### Perlin2D (introduced in _3)
- Python: `Perlin2D(seed)` with `noise(x: float, y: float) → float`
- C++: needs `class Perlin2D` with `init(uint32_t seed)` and `noise(float x, float y) → float`
- 2D gradient with 8 directions (Python `_grad(h, x, y)` with `h & 7`)
- Also defined **outside** `namespace colorTrails`

### Impact on sample_profile()
The noise class affects how `sample_profile()` works:
- **1D**: `noise.noise(i * freq * scale + phase)` — spatial and temporal on same axis
- **2D**: `noise.noise(i * freq * scale, scroll_y)` — spatial on x, temporal on y (independent axes)

This means `sample_profile` needs a variant or parameter to select 1D vs 2D behavior.

---

## 9. Core Engine Functions (Shared by All Modes)

These are mode-independent and rarely change between Python revisions:

| Python function | C++ function | Notes |
|----------------|-------------|-------|
| `sample_profile()` | `sampleProfile()` | Builds one noise profile (one value per row or column) |
| `advect_axes_and_dim()` | `advectAndDim()` | Two-pass bilinear advection + fade; shared by all modes |
| `rainbow_color_with_phase()` | `rainbow()` | HSV rainbow from continuous hue |
| `blend_pixel_weighted()` | `blendPixelWeighted()` | Single pixel alpha blend into float grid |
| `blend_subpixel()` | Not ported (drawCircle used instead) | |

---

## 10. Emitter Modes

Each Python emitter function becomes a `case N:` in `runColorTrails()`'s
`switch(MODE)`. The advection engine is shared; only the injection differs.

| Mode | Python source | C++ injection | Python revision |
|------|--------------|---------------|-----------------|
| 0 "orbital" | 3 orbiting circles (inline in main loop) | `drawCircle()` in a 3-iteration loop | _1 |
| 1 "lissajous" | `inject_lissajous_line()` | `injectLissajousLine()` | _2 |
| 2 "borderRect" | `inject_rainbow_border_rect()` (_3) / `inject_rainbow_two_rect_lines()` (_4) | `injectRainbowBorder()` | _3, _4 |

### smearMode (orthogonal to emitter mode)
The `x_profile = list(reversed(x_profile))` from _2 and _3 is controlled by
`params.smearMode`. When `smearMode == 1`, the xProf array is reversed after
sampling. This is independent of which emitter mode is active.

---

## 11. Parameter Pipeline

Each tunable parameter flows through multiple layers:

### A. CTParams struct (colorTrails_detail.hpp)
```cpp
struct CTParams {
    float xScale = 0.33f;  // Noise spatial scale (column axis)
    // ...
};
```
Field naming: **camelCase**, matching the JavaScript convention.

### B. Global cVar (bleControl.h — manual declaration)
```cpp
float cXScale = 0.33f;
```
Naming: **cPascalCase** — prefix `c` + PascalCase of the param name.

### C. PARAMETER_TABLE X-macro entry (bleControl.h)
```cpp
X(float, XScale, 0.33f) \
```
Format: `X(type, PascalName, default)`. Generates references to `cPascalName`.
**Does NOT declare the variable** — the manual declaration (B) is required.

### D. Param array + lookup (bleControl.h)
```cpp
const char* const COLORTRAILS_ORBITAL_PARAMS[] PROGMEM = {"xScale", "yScale", ...};
// In VISUALIZER_PARAM_LOOKUP:
{"colortrails-orbital", COLORTRAILS_ORBITAL_PARAMS, 2},
```
String names use **camelCase** (must match JS keys exactly).

### E. JavaScript VISUALIZER_PARAMETER_REGISTRY (index.html)
```js
xScale: {min: 0.1, max: 4, default: 0.33, step: 0.01},
```
Key is **camelCase**. Ranges should correspond to Python slider min/max.

### F. JavaScript VISUALIZER_PARAMS (index.html)
```js
"colortrails-orbital": ["xScale", "yScale", ...],
```

### G. Apply in runColorTrails() (colorTrails_detail.hpp)
```cpp
params.xScale = cXScale;
```

### Python slider → BLE param name mapping
| Python slider label | CTParams field | cVar | JS key |
|--------------------|---------------|------|--------|
| "X Speed" | xSpeed | cXSpeed | xSpeed |
| "X Amplitude" | xAmplitude | cXAmplitude | xAmplitude |
| "X Frequency" / "X Scale" | xScale | cXScale | xScale |
| "Y Speed" | ySpeed | cYSpeed | ySpeed |
| "Y Amplitude" | yAmplitude | cYAmplitude | yAmplitude |
| "Y Frequency" / "Y Scale" | yScale | cYScale | yScale |
| "Fade %" | fadePct | cFadePct | fadePct |
| "Color Shift" | colorShift | cColorShift | colorShift |
| "Endpoint Speed" | endpointSpeed | cEndpointSpeed | endpointSpeed |
| "Orbit Speed" | orbitSpeed | cOrbitSpeed | orbitSpeed |
| "Color Speed" | colorSpeed | cColorSpeed | colorSpeed |
| "Variation Intensity" | variationIntensity | cVariationIntensity | variationIntensity |
| "Variation Speed" | variationSpeed | cVariationSpeed | variationSpeed |
| (modulateAmplitude toggle) | modulateAmplitude | cModulateAmplitude | modulateAmplitude |

---

## 12. AuroraPortal Integration Checklist

When adding a new mode or parameter, touch all of these:

### New MODE:
- [ ] Add emitter function in `colorTrails_detail.hpp`
- [ ] Add `case N:` in `runColorTrails()`'s `switch(MODE)`
- [ ] Add PROGMEM mode name string in `bleControl.h`
- [ ] Add to `COLORTRAILS_MODES[]` array
- [ ] Update `MODE_COUNTS[]` entry for colortrails
- [ ] Add `COLORTRAILS_<MODE>_PARAMS[]` PROGMEM array
- [ ] Add entry in `VISUALIZER_PARAM_LOOKUP[]`
- [ ] Add mode name to JS `COLORTRAILS_MODES` array
- [ ] Update JS `MODE_COUNTS` array
- [ ] Add `"colortrails-<mode>": [...]` to JS `VISUALIZER_PARAMS`

### New PARAMETER:
- [ ] Add field to `CTParams` struct
- [ ] Add `cVarName` global declaration in `bleControl.h`
- [ ] Add `X(type, Name, default)` to `PARAMETER_TABLE`
- [ ] Add param name string to relevant `COLORTRAILS_*_PARAMS[]` arrays
- [ ] Update param count in `VISUALIZER_PARAM_LOOKUP[]`
- [ ] Add `params.field = cVarName;` in `runColorTrails()`
- [ ] Add entry to JS `VISUALIZER_PARAMETER_REGISTRY`
- [ ] Add param name to relevant JS `VISUALIZER_PARAMS` arrays
- [ ] Update param count in relevant JS arrays (if shown)

---

## 13. Revision History

| Python file | Key additions | C++ modes/features |
|-------------|-------------|-------------------|
| `_1` | Perlin1D, orbiting circles, advection engine | Mode 0 "orbital", core engine |
| `_2` | Lissajous line, AA drawing helpers, reversed xProfile | Mode 1 "lissajous", smearMode, blendPixelWeighted/drawAASubpixelLine/drawAAEndpointDisc |
| `_3` | Perlin2D, inject_rainbow_border_rect, xFrequency/yFrequency sliders | Mode 2 "borderRect", Perlin2D class, noiseMode param (0=1D, 1=2D), xScale/yScale range expanded to 0.10–4.00 |
| `_4` | inject_rainbow_two_rect_lines (border rect with seam at origin), amplitude modulation via slow 1D Perlin (variationIntensity, variationSpeed), self-modulating variation depth | modulateAmplitude toggle, variationIntensity/variationSpeed params, ampVarX/ampVarY Perlin1D instances (seeds 101, 202). Modulates xAmplitude/yAmplitude before noise profile sampling. |
