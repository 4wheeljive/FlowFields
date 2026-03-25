# ColorTrails Architecture & BLE/UI System Guide

## Overview

ColorTrails is an ESP32 LED visualization program built on PlatformIO + FastLED. It renders color onto a 2D pixel grid using a component pipeline: **Emitters** inject color, **ColorFlowFields** advect it, and **Modulators** modify parameters over time. The result is continuously composited onto WxH float-precision RGB buffers and pushed to LEDs each frame.

All parameters are controllable over BLE from a web-based UI served from `index.html`.

Much of the visualization functionality was ported from Python to C++. The original Python files are in the /colorTrailsOrig folder. There is also a TRANSLATION_GUIDE.md that outlines many aspects of the conversion. The ColorTrails architecture has changed dramatically since  TRANSLATION_GUIDE.md was prepared, so certain aspects of the "target landing environment" may be different than described. 

---

## Component Architecture

### Pipeline (per frame)

```
1. Flow Prepare  — build noise profiles, apply modulators, apply axis flips
2. Emit          — draw color sources onto the float grid
3. Flow Advect   — shift pixels via bilinear interpolation + fade
4. Copy to LEDs  — quantize float grid to uint8 per pixel
```

### Component Types

| Component | Purpose | Current Implementations |
|-----------|---------|------------------------|
| **Emitter** | Injects color onto the grid | OrbitalDots, LissajousLine, RainbowBorder |
| **ColorFlowField** | Advects (shifts/blends) existing pixels | NoiseFlow |
| **Modulator** | Time-varies flow field parameters | AmpMod (amplitude modulation via 1D Perlin) |

Each component type has its own parameter struct. Only one emitter and one flow field are active at a time. The active emitter is selected by `MODE`.

### Key Structs

```cpp
// Each emitter owns its params AND a NoiseFlowParams with its preferred flow defaults
struct OrbitalParams {
    float orbitSpeed, colorSpeed, circleDiam, orbitDiam;
    NoiseFlowParams noiseFlow{...};  // emitter-specific noise defaults
};

struct LissajousParams {
    float endpointSpeed, colorShift, lineAmplitude;
    NoiseFlowParams noiseFlow{...};
};

struct BorderRectParams {
    float colorShift;
    NoiseFlowParams noiseFlow{...};
};

// Flow field params (live working instance — synced from cVars every frame)
struct NoiseFlowParams {
    float xSpeed, ySpeed;           // noise scroll speed
    float xAmplitude, yAmplitude;   // noise amplitude
    float xFrequency, yFrequency;   // noise spatial scale
    float xShift, yShift;           // max pixel shift per row/column
    bool  use2DNoise;               // 1D vs 2D Perlin
};

// Modulator
struct AmpModParams {
    float intensity, speed;
    bool active;
};

// Top-level config binding everything together
struct CtVizConfig {
    float fadeRate;
    bool flipVertical, flipHorizontal;
    EmitterType emitter;      // selected by MODE
    FlowFieldType flowField;  // currently always FLOW_NOISE
    bool useAmpMod;
};
```

### Dispatch Tables

Emitters and flow fields are invoked through function pointer arrays indexed by their enum:

```cpp
const EmitterFn    EMITTER_RUN[]  = { emitOrbitalDots, emitLissajousLine, emitRainbowBorder };
const FlowPrepFn   FLOW_PREPARE[] = { noiseFlowPrepare };
const FlowAdvectFn FLOW_ADVECT[]  = { noiseFlowAdvect };
```

### Noise Generators

Two noise systems operate simultaneously:

- **Spatial profiles** (Perlin1D or Perlin2D): Drive per-row and per-column shift amounts in the flow field. Controlled by `NoiseFlowParams`.
- **Amplitude modulation** (Perlin1D): Slowly varies the flow field's xAmplitude/yAmplitude over time. Controlled by `AmpModParams`.

Each emitter's `NoiseFlowParams` member holds defaults appropriate to that emitter's visual character. When the user switches modes (emitters), `pushDefaultsToCVars()` copies the active emitter's noise defaults into the live `noiseFlow` instance and the cVars.

---

## Timer/Modulator System

The `timers`/`modulators` framework (in `modulators.h`) provides coordinated, millisecond-synchronized time-based modulation across all components.

### Architecture

- `timers timings` — input struct: `ratio[i]` sets each timer's time-sensitivity, `offset[i]` sets phase offset
- `modulators move` — output struct: 5 derived output arrays, all computed from the same `runtime = fl::millis()` snapshot
- `calculate_modulators(timings)` — single call per frame computes all 10 timers × 5 output types

### Core loop (per timer)

```cpp
move.linear[i]            = (runtime + timings.offset[i]) * timings.ratio[i];   // 0 to FLT_MAX
move.radial[i]            = fmod(move.linear[i], 2π);                            // 0 to 2π
move.directional_sine[i]  = sin(move.radial[i]);                                 // -1 to 1
move.directional_noise[i] = noiseX.noise(move.linear[i]);                        // -1 to 1
move.radial_noise[i]      = π * (1 + move.directional_noise[i]);                 // 0 to 2π
```

### Why this design

1. **One coordinated calculation per frame** — all modulator outputs share the same `fl::millis()` snapshot, so relationships between timers are precise to the millisecond
2. **Timing relationships are visible** — a developer can see all `ratio` and `offset` assignments together in each emitter/flow function, making it easy to tune relative speeds and phase relationships
3. **Multiple output shapes from one timer** — setting `timings.ratio[2] = 0.00005f` produces `move.linear[2]`, `move.radial[2]`, `move.directional_sine[2]`, `move.directional_noise[2]`, and `move.radial_noise[2]` simultaneously, each at its natural range

### ModConfig — bridging timers to parameters

Each modulatable parameter has a companion `ModConfig` struct:

**Hardcoded** (developer architectural choices, not UI-exposed):
- `type` — which `move.*` output to read (e.g. `MOD_DIRECTIONAL_NOISE`)
- `op` — how the wave modifies the base value (`OP_SCALE` or `OP_ADD`)
- `timer` — which timer index (0–9) to use

**UI-tunable** (wired through the 5-file cVar pattern):
- `rate` — written to `timings.ratio[timer]` before `calculate_modulators()`, controls modulation speed
- `level` — modulation depth (0 = mod off)

Convention: timers 0–5 for emitters, 6–9 for flow fields.

Usage pattern: `configureTimer()` → `calculate_modulators()` → `Modulators::apply(base, modConfig)`

---

## The Float Grid

ColorTrails maintains two WxH float grids (`gR/gG/gB` and `tR/tG/tB`). The live buffer receives emitter drawing; the scratch buffer is used during two-pass advection. All drawing uses float precision for smooth anti-aliased sub-pixel rendering. The grid is quantized to uint8 only at the final LED copy step.

Fade is frame-rate-independent: `fadePerSec = pow(fadeRate, 60)`, then `fade = pow(fadePerSec, dt)`.

---

## BLE/UI Control System

### Architecture Overview

The BLE system uses 4 characteristics for bidirectional communication:

| Characteristic | Direction | Format | Purpose |
|---------------|-----------|--------|---------|
| **Button** | UI → ESP32 | Raw uint8 | Program/mode selection, preset save/load, triggers |
| **Checkbox** | Bidirectional | JSON `{id, val}` | Boolean toggles |
| **Number** | Bidirectional | JSON `{id, val}` | Parameter sliders |
| **String** | ESP32 → UI | JSON `{id, val}` | State sync (visualizerState, audioState, busState) |

### Naming Convention

Parameter names flow through three representations:

```
camelCase (JS/arrays)  →  inPascalCase (BLE id)  →  cPascalCase (C++ global)

Example:
  "orbitSpeed"         →  "inOrbitSpeed"          →  cOrbitSpeed
```

The JS-side `getParameterId()` function prepends "in" and capitalizes the first letter.
The C++ X-macro `PARAMETER_TABLE` generates `cPascalCase` globals automatically.
Matching between array names (camelCase) and X-macro names (PascalCase) uses `strcasecmp()`.

### X-Macro Parameter Table (bleControl.h)

The `PARAMETER_TABLE` X-macro is the single source of truth for all controllable parameters:

```cpp
#define PARAMETER_TABLE \
   X(float, OrbitSpeed, 0.35f) \
   X(float, FadeRate, 0.99922f) \
   X(float, XSpeed, -1.73f) \
   ...
```

This macro auto-generates:
- **`captureCurrentParameters()`** — serialize all cVars to JSON (for preset save)
- **`applyCurrentParameters()`** — deserialize JSON into cVars (for preset load)
- **`processNumber()`** — match incoming BLE id to cVar and update it
- **`sendVisualizerState()`** — iterate a program's param list and serialize matching cVars

### Parameter Registration (5-file pattern)

Adding a new parameter requires edits in these locations:

#### 1. bleControl.h — cVar declaration
```cpp
float cOrbitSpeed = 0.35f;
```

#### 2. bleControl.h — PARAMETER_TABLE entry
```cpp
X(float, OrbitSpeed, 0.35f) \
```

#### 3. bleControl.h — Program's PROGMEM param array
```cpp
const char* const COLORTRAILS_PARAMS[] PROGMEM = {
    "fadeRate", "orbitSpeed", ...
};
```

#### 4. bleControl.h — VISUALIZER_PARAM_LOOKUP entry (update count)
```cpp
{"colortrails-orbital", COLORTRAILS_PARAMS, 19},
```

#### 5. index.html — VISUALIZER_PARAMETER_REGISTRY
```javascript
orbitSpeed: {min: 0.01, max: 25, default: 0.35, step: 0.01},
```

#### 6. index.html — VISUALIZER_PARAMS arrays
```javascript
"colortrails-orbital": ["fadeRate", "orbitSpeed", ...],
```

### cVar Bridge (colorTrails_detail.hpp)

Component structs own their parameter values. cVars are the BLE-facing global variables. Two bridge functions keep them in sync:

**`pushDefaultsToCVars()`** — Called on mode change. Copies struct defaults → cVars, then triggers `sendVisualizerState()` so the UI updates its sliders.

```cpp
static void pushDefaultsToCVars() {
    cFadeRate = vizConfig.fadeRate;
    cOrbitSpeed = orbital.orbitSpeed;
    // ... all params ...
    // Load active emitter's noise defaults
    const NoiseFlowParams& nf = activeEmitterNoiseFlow();
    noiseFlow = nf;
    cXSpeed = nf.xSpeed;
    // ...
}
```

**`syncFromCVars()`** — Called every frame. Copies cVars → component structs so BLE-driven changes take effect immediately.

```cpp
static void syncFromCVars() {
    vizConfig.fadeRate = cFadeRate;
    orbital.orbitSpeed = cOrbitSpeed;
    // ...
}
```

### State Sync Flow

When the UI connects or a mode changes:

```
ESP32                                    Web UI
  │                                        │
  │──── sendVisualizerState() ────────────>│
  │     {id:"visualizerState",             │
  │      val:{program, mode, parameters}}  │
  │                                        │
  │     UI updates program/mode buttons,   │
  │     rebuilds slider panel,             │
  │     applies parameter values to sliders│
  │                                        │
  │<──── sendNumberCharacteristic() ───────│
  │      {id:"inOrbitSpeed", val:0.5}      │
  │                                        │
  │      processNumber() updates cVar,     │
  │      syncFromCVars() applies to struct │
```

The `sendVisualizerState()` envelope is a single JSON document (not double-encoded) to stay within BLE MTU limits (~247 bytes negotiated).

### Mode Change Flow

```
MODE changes → runColorTrails() detects MODE != lastEmitter
  → vizConfig.emitter = (EmitterType)MODE
  → pushDefaultsToCVars()       // load emitter's noise defaults into cVars
  → sendVisualizerState()       // notify UI of new param values
```

---

## Adding a New Program

### Registration Checklist

1. **bleControl.h**: Add to `Program` enum, add PROGMEM name string, add to `PROGRAM_NAMES[]`, update `PROGRAM_COUNT`
2. **bleControl.h**: If it has modes, add mode strings, mode array, add to `MODE_COUNTS[]`, add switch case in `VisualizerManager::getVisualizerName()`
3. **bleControl.h**: Create `YOURPROG_PARAMS[]` PROGMEM array listing param names
4. **bleControl.h**: Add entries to `VISUALIZER_PARAM_LOOKUP[]`
5. **index.html**: Add to `PROGRAMS` array (name + mode list)
6. **index.html**: Add param definitions to `VISUALIZER_PARAMETER_REGISTRY` (if new params)
7. **index.html**: Add entries to `VISUALIZER_PARAMS` object
8. **main.cpp**: Add case to program switch in the main loop

### Adding a New Mode to ColorTrails

1. Add a new `EmitterType` enum value
2. Create the emitter param struct (with embedded `NoiseFlowParams` for preferred defaults)
3. Create the live param instance
4. Write the emitter function (reads from its own struct)
5. Add to `EMITTER_RUN[]` dispatch table
6. Add mode string to `COLORTRAILS_MODES[]`
7. Update `MODE_COUNTS[COLORTRAILS]`
8. Wire into `pushDefaultsToCVars()` / `syncFromCVars()` / `activeEmitterNoiseFlow()`
9. Add any new params through the 5-file registration pattern
10. Update COLORTRAILS_PARAMS count in VISUALIZER_PARAM_LOOKUP

### Adding a New Parameter to an Existing Program

Follow the 5-file pattern documented above. Key details:
- The cVar name must be `c` + PascalCase version of the camelCase param name
- The PARAMETER_TABLE entry name is PascalCase (no `c` prefix)
- The PROGMEM array and JS arrays use camelCase
- `strcasecmp` bridges camelCase ↔ PascalCase automatically
- Don't forget to update the count in VISUALIZER_PARAM_LOOKUP

---

## File Map

| File | Role |
|------|------|
| `src/programs/colorTrails_detail.hpp` | All colorTrails logic: noise generators, drawing primitives, component structs, emitter/flow/modulator implementations, dispatch tables, cVar bridge, main loop |
| `src/bleControl.h` | BLE stack, parameter X-macro table, cVar declarations, program/mode registries, PROGMEM param arrays, VISUALIZER_PARAM_LOOKUP, state sync functions, preset save/load |
| `index.html` | Complete web UI: Web Components (ProgramSelector, ModeSelector, ParameterSettings), BLE transport, VISUALIZER_PARAMETER_REGISTRY, VISUALIZER_PARAMS, state handlers |
| `src/main.cpp` | Program dispatch loop, LED setup, global constants (WIDTH, HEIGHT, MIN_DIMENSION) |
