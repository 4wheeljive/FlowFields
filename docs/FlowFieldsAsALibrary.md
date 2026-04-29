# FlowFields as a PlatformIO Library

## Goal

Allow FastLED-MM (and any other project) to add FlowFields as a PlatformIO library dependency:

```ini
lib_deps = ewowi/FlowFields
```

…and wrap the engine in a `ProducerModule` subclass so all emitters, flows, and parameters are
available through the FastLED-MM / projectMM UI system without any BLE dependency.

---

## What Needs to Change

### 1. Global variables defined in headers (ODR violation)

`flowFieldsTypes.h` defines variables directly in the header (not just declares them):

```cpp
bool flowFieldsInstance = false;
uint16_t (*xyFunc)(uint8_t x, uint8_t y);
float globalSpeed = 1.0f;
float persistence = 0.05f;
// …etc
```

When a library consumer includes this header from multiple translation units the linker sees
multiple definitions of the same symbol — a hard build error.  The fix is to move all
definitions into a class so each instance owns its own copy.

### 2. Compile-time grid dimensions

`HEIGHT` and `WIDTH` come from `boardConfig.h` as compile-time `#define`s, and the grids are
declared as static 2-D arrays:

```cpp
static float gR[HEIGHT][WIDTH], gG[HEIGHT][WIDTH], gB[HEIGHT][WIDTH];
```

A library consumer's LED panel may be a different size, or the size may not be known until
`setup()`.  The grids must become dynamically allocated at runtime.

### 3. No library manifest

PlatformIO needs a `library.json` at the repo root to resolve `lib_deps = ewowi/FlowFields`.
The file tells the package manager which source files belong to the library (everything except
`main.cpp` and the BLE/hardware layer).

### 4. No lifecycle API

The engine currently exposes only free functions (`initFlowFields`, `runFlowFields`).  A library
consumer needs an object it can instantiate, configure, and destroy — matching the
`setup / loop / teardown / onSizeChanged` pattern used by FastLED-MM's `ProducerModule`.

---

## Proposed Changes

### Phase 1 — Wrap the engine in a class

Create `src/FlowFieldsEngine.h` (or promote the existing `flowFieldsEngine.hpp` to a class).
Move every namespace-level variable in `flowFieldsTypes.h` and `flowFieldsEngine.hpp` into
`FlowFieldsEngine` as member variables.

Public interface:

```cpp
class FlowFieldsEngine {
public:
    // Lifecycle
    void setup(uint8_t width, uint8_t height, uint16_t numLeds,
               uint16_t (*xyFunc)(uint8_t x, uint8_t y));
    void run(fl::CRGB* leds);   // one complete frame: prepare + emit + advect + copy
    void teardown();            // free grids

    // Selection
    void setEmitter(uint8_t idx);
    void setFlow(uint8_t idx);

    // Parameter API — set/get any parameter by its camelCase name
    void  setParam(const char* name, float value);
    float getParam(const char* name) const;

    // Direct cVar access (kept for the BLE layer in main.cpp)
    float cOrbitSpeed   = 0.35f;
    float cGlobalSpeed  = 1.0f;
    float cPersistence  = 0.05f;
    // …all other cVars as public members…
};
```

The emitter and flow functions currently read namespace globals (`t`, `dt`, `gR[y][x]`, etc.).
Introduce a single file-scope pointer:

```cpp
// in FlowFieldsEngine.cpp (or at the top of the .hpp implementation block)
static FlowFieldsEngine* g_engine = nullptr;
```

`FlowFieldsEngine::run()` sets `g_engine = this` before dispatching to any emitter/flow
function.  Because embedded targets run single-threaded and only one engine instance is active
at a time, this is safe.  All existing emitter/flow headers continue to read `g_engine->gAt(x,y)`
etc. with minimal changes.

Alternatively (cleaner but more refactor work): pass `FlowFieldsEngine&` as a parameter to
every emitter/flow function.  The dispatch table types change to
`using EmitterFn = void(*)(FlowFieldsEngine&)`.  This makes the data-flow explicit and removes
the global pointer entirely — recommended if touch-up of all headers is acceptable.

### Phase 2 — Dynamic grid allocation

Replace the static 2-D arrays with heap-allocated 1-D arrays and inline accessors:

```cpp
// In FlowFieldsEngine (private members)
float* gR = nullptr;
float* gG = nullptr;
float* gB = nullptr;
float* tR = nullptr;
float* tG = nullptr;
float* tB = nullptr;
uint8_t  _width  = 0;
uint8_t  _height = 0;
uint16_t _numLeds = 0;

// Inline accessor replacing gR[y][x]
inline float& gRAt(int x, int y) { return gR[y * _width + x]; }
```

`setup()` allocates:

```cpp
void FlowFieldsEngine::setup(uint8_t w, uint8_t h, uint16_t n,
                              uint16_t (*xy)(uint8_t, uint8_t)) {
    teardown();   // safe to call even before first setup
    _width = w; _height = h; _numLeds = n; xyFunc = xy;
    size_t sz = (size_t)w * h;
    gR = new float[sz](); gG = new float[sz](); gB = new float[sz]();
    tR = new float[sz](); tG = new float[sz](); tB = new float[sz]();
    // init noise, timers, etc.
}

void FlowFieldsEngine::teardown() {
    delete[] gR; delete[] gG; delete[] gB;
    delete[] tR; delete[] tG; delete[] tB;
    gR = gG = gB = tR = tG = tB = nullptr;
}
```

Every place that currently writes `gR[y][x]` becomes `gRAt(x, y)` (or the g_engine pointer
version: `g_engine->gRAt(x, y)`).  A find-replace across the emitter/flow headers handles
the bulk of this.

### Phase 3 — Pointer binding (zero hot-path overhead)

The goal is that `loop()` contains only `engine_.run(leds_)`.  All parameter bridging happens
via **pointer binding** set up once during `setup()`.

#### Design

Each parameter in the engine is backed by two things:
- A public `float cXxx` member — the default storage, written to directly by the BLE layer.
- A private `float* pXxx` pointer — defaults to `&cXxx`, but can be redirected to any
  caller-owned float during `setup()`.

`run()` reads only through the pointers — one dereference per parameter, no string lookups,
no copies.  Because the pointers are set during `setup()` they are effectively constants at
runtime.

```cpp
class FlowFieldsEngine {
public:
    // ── Default storage (the BLE layer writes here directly) ─────────────
    float cEmitter     = 0.0f;   // float so all bindings are uniform
    float cFlow        = 0.0f;
    float cGlobalSpeed = 1.0f;
    float cPersistence = 0.05f;
    float cOrbitSpeed  = 0.35f;
    // … all other cVars …

    // ── Redirect a pointer to a caller-owned float (call once in setup) ──
    // The string lookup happens only here, never in run().
    void bindParam(const char* name, float* externalPtr);

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void setup(uint8_t w, uint8_t h, uint16_t n,
               uint16_t (*xy)(uint8_t, uint8_t));
    void run(fl::CRGB* leds);   // hot path — pointer dereferences only
    void teardown();

private:
    // Pointers default to the public cVar members above.
    float* pEmitter     = &cEmitter;
    float* pFlow        = &cFlow;
    float* pGlobalSpeed = &cGlobalSpeed;
    float* pOrbitSpeed  = &cOrbitSpeed;
    // …
};
```

`bindParam` does a one-time string look-up (fine in `setup()`, never called in `run()`):

```cpp
void FlowFieldsEngine::bindParam(const char* name, float* ptr) {
    #define X(ptrMember, paramName) \
        if (strcasecmp(name, #paramName) == 0) { ptrMember = ptr; return; }
    X(pEmitter,     "emitter")
    X(pFlow,        "flow")
    X(pGlobalSpeed, "globalSpeed")
    X(pOrbitSpeed,  "orbitSpeed")
    // …
    #undef X
}
```

Inside `run()`, reading a parameter is just:

```cpp
uint8_t emitterIdx = (uint8_t)*pEmitter;   // plain pointer dereference
float   speed      = *pGlobalSpeed;
```

#### BLE / `main.cpp` usage — no binding needed

BLE callbacks write directly to the engine's `cXxx` public members.  Because `pXxx` defaults
to `&cXxx`, `run()` sees the updated value immediately with no extra wiring:

```cpp
// BLE callback on value change:
engine.cOrbitSpeed = newValue;   // pOrbitSpeed still points here → run() picks it up
```

#### FastLED-MM usage — bind once, loop is one line

```cpp
void setup() override {
    engine_.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);
    addControl(emitter_,     "emitter",    "select", 0, EMITTER_COUNT - 1);
    addControl(globalSpeed_, "globalSpeed","slider",  0.1f, 5.0f);
    addControl(orbitSpeed_,  "orbitSpeed", "slider",  0.01f, 25.0f);
    // … register all controls …

    // Bind: engine reads the ProducerModule's floats directly, no copies in loop
    engine_.bindParam("emitter",     &emitter_);
    engine_.bindParam("globalSpeed", &globalSpeed_);
    engine_.bindParam("orbitSpeed",  &orbitSpeed_);
    // …
}

void loop() override {
    engine_.run(leds_);   // ← this is the entire hot path
}
```

projectMM updates `orbitSpeed_` when the slider moves; `run()` dereferences `pOrbitSpeed`
which now points at `orbitSpeed_` — zero latency, zero overhead.

### Phase 4 — Library manifest

Add `library.json` at the repo root:

```json
{
  "name": "FlowFields",
  "version": "1.0.0",
  "description": "Emitter + FlowField + Modulator LED visualization engine for ESP32/FastLED",
  "keywords": ["fastled", "led", "visualization", "esp32", "effects"],
  "authors": [{"name": "ewowi", "url": "https://github.com/ewowi"}],
  "repository": {"type": "git", "url": "https://github.com/ewowi/FlowFields"},
  "license": "MIT",
  "frameworks": ["arduino"],
  "platforms": ["espressif32"],
  "dependencies": [{"name": "fastled/FastLED", "version": ">=4.0.0"}],
  "build": {
    "srcDir": "src",
    "includeDir": "src",
    "srcFilter": "+<**/*> -<main.cpp> -<hosted_ble_bridge.cpp>"
  }
}
```

`main.cpp` and `hosted_ble_bridge.cpp` are excluded from the library build.  The BLE headers
(`bleControl.h`, `boardConfig.h`) are not compiled independently so they do not need
exclusion — they are only pulled in via `main.cpp`.

The existing standalone firmware continues to build normally via `platformio.ini`; that build
includes `main.cpp` in `src/` as usual.

### Phase 5 — FastLED-MM ProducerModule example

Create `examples/FastLEDMM/FlowFieldsEffect.h`.  All setup work — control registration and
pointer binding — happens in `setup()`.  The `loop()` is a single call.

```cpp
#pragma once
#include <projectMM.h>
#include <FlowFieldsEngine.h>   // from lib_deps = ewowi/FlowFields

class FlowFieldsEffect : public ProducerModule {
public:
    const char* name()     const override { return "FlowFields"; }
    const char* category() const override { return "source"; }
    uint8_t preferredCore() const override { return 0; }

    uint16_t pixelWidth()  const override { return WIDTH; }
    uint16_t pixelHeight() const override { return HEIGHT; }

    void setup() override {
        declareBuffer(leds_, NUM_LEDS, sizeof(fl::CRGB));

        // 1. Register controls — projectMM owns and updates these floats.
        addControl(emitter_,     "emitter",     "select", 0.0f, (float)(EMITTER_COUNT - 1));
        addControl(flow_,        "flow",         "select", 0.0f, (float)(FLOW_COUNT - 1));
        addControl(globalSpeed_, "globalSpeed",  "slider", 0.1f, 5.0f);
        addControl(persistence_, "persistence",  "slider", 0.01f, 2.0f);
        addControl(colorShift_,  "colorShift",   "slider", 0.0f, 1.0f);
        addControl(orbitSpeed_,  "orbitSpeed",   "slider", 0.01f, 25.0f);
        addControl(numDots_,     "numDots",      "slider", 1.0f, 20.0f);
        // … one addControl per parameter …

        // 2. Init the engine for this panel size.
        engine_.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);

        // 3. Bind: redirect engine pointers to the floats projectMM updates.
        //    Done once here; run() costs only a pointer dereference per param.
        engine_.bindParam("emitter",     &emitter_);
        engine_.bindParam("flow",        &flow_);
        engine_.bindParam("globalSpeed", &globalSpeed_);
        engine_.bindParam("persistence", &persistence_);
        engine_.bindParam("colorShift",  &colorShift_);
        engine_.bindParam("orbitSpeed",  &orbitSpeed_);
        engine_.bindParam("numDots",     &numDots_);
        // …
    }

    void onSizeChanged() override {
        engine_.teardown();
        engine_.setup(pixelWidth(), pixelHeight(), pixelWidth() * pixelHeight(), myXY);
        // Bindings survive teardown/setup because they point into this object's members.
    }

    void loop() override {
        engine_.run(leds_);   // ← entire hot path
    }

    void teardown() override { engine_.teardown(); }

    size_t classSize() const override { return sizeof(*this); }

private:
    fl::CRGB leds_[NUM_LEDS];
    FlowFieldsEngine engine_;

    // One float per parameter — projectMM writes here when controls change.
    float emitter_     = 0.0f;
    float flow_        = 0.0f;
    float globalSpeed_ = 1.0f;
    float persistence_ = 0.05f;
    float colorShift_  = 0.20f;
    float orbitSpeed_  = 0.35f;
    float numDots_     = 5.0f;
    // …
};

REGISTER_MODULE(FlowFieldsEffect)
```

`NUM_LEDS`, `WIDTH`, `HEIGHT`, and `myXY` come from the example's `main.cpp` (same as in the
existing standalone firmware).

### Phase 6 — Minimal changes to the existing `main.cpp`

The standalone firmware changes only at the two call sites.  No pointer binding is needed
because the BLE layer writes directly to the engine's public `cXxx` members, and the engine's
pointers default to pointing at those same members.

```cpp
// BEFORE
if (!flowFields::flowFieldsInstance) {
    flowFields::initFlowFields(myXY);
}
flowFields::runFlowFields();

// AFTER — hot loop is identical in shape
static FlowFieldsEngine engine;
if (!engineInitialized) {
    engine.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);
    engineInitialized = true;
}
engine.run(leds);   // ← same single call, same hot path cost
```

BLE callbacks continue to write directly to the engine's members:

```cpp
// In bleControl.h / processNumber() — no change in behaviour:
engine.cOrbitSpeed = newValue;   // pOrbitSpeed points here by default → run() sees it
```

`sendEmitterState()` / `sendFlowState()` read from the same public members to serialise state
back to the UI — no additional API needed.

---

## Summary of Files Touched

| File | Change |
|------|--------|
| `src/FlowFieldsEngine.h` | **New** — `FlowFieldsEngine` class declaration |
| `src/flowFieldsEngine.hpp` | Convert namespace functions → class member implementations |
| `src/flowFieldsTypes.h` | Remove variable definitions; keep type/constant declarations only; add `gRAt` etc. helpers |
| `src/emitters/*.h` | Replace `gR[y][x]` → `g_engine->gRAt(x,y)`, `t` → `g_engine->t`, etc. |
| `src/flows/*.h` | Same as emitters |
| `src/main.cpp` | Replace `initFlowFields` / `runFlowFields` with `engine.setup()` / `engine.run()` |
| `library.json` | **New** — PlatformIO library manifest |
| `examples/FastLEDMM/FlowFieldsEffect.h` | **New** — FastLED-MM ProducerModule wrapper |

---

## Implementation Order

1. `FlowFieldsEngine` class skeleton with dynamic grids (Phase 1 + 2) — verify standalone build
2. Update all emitter/flow headers to use `g_engine` accessors — verify visuals unchanged
3. Add pointer binding (Phase 3) — update BLE layer to write to `engine.cXxx` members
4. Add `library.json` (Phase 4) — verify `lib_deps = ewowi/FlowFields` installs cleanly
5. Write and test `FlowFieldsEffect.h` example (Phase 5)

---

## Effort Estimate & Sequencing

### Per-phase breakdown

| Phase | Description | Estimate |
|-------|-------------|----------|
| 1 — `FlowFieldsEngine` class | Extract ~40 namespace globals into a class; add `g_engine` pointer; convert `initFlowFields`/`runFlowFields` to `setup()`/`run()`; move drawing primitives to member functions | 3–4 h |
| 2 — Dynamic grids | Replace `static float gR[HEIGHT][WIDTH]` with `float**` (row-pointer arrays); add `allocGrid`/`freeGrid` helpers; update all 50 grid access sites to `g_engine->gR[y][x]`; refactor `flow_fluid.h`'s internal functions from `float (*x)[WIDTH]` to `float**` (that file owns 6 internal 2D arrays and 5 helper functions that all need updating) | 3–4 h |
| 3 — Pointer binding | Add `float* pXxx` members defaulting to `&cXxx`; add `bindParam()`; update `bleControl.h` call sites | 1–2 h |
| 4 — `library.json` | New file, zero risk | 30 min |
| 5 — FastLED-MM example | New file | 1–2 h |
| 6 — `main.cpp` | 4 call-site changes + set 2 callbacks | 30 min |

**Session 1** (Phase 1 + 2 + 6): ~7–9 hours. This is the structural core. Verify the standalone
BLE firmware builds and visuals are identical before proceeding.

**Session 2** (Phase 3 + 4 + 5): ~3–5 hours. Purely additive — no existing behaviour can
break once Session 1 is verified.

### Why phase-by-phase, not all at once

Session 1 contains the only real risk: two independent mechanical changes (class extraction and
grid refactor) happen simultaneously, and `flow_fluid.h` needs the most invasive edit. Keeping
Session 2 separate means a known-good checkpoint exists before the public API surface is
finalised. A broken `bindParam` call or `library.json` misconfiguration is much easier to
diagnose when the underlying engine is already verified.

---

## For the FlowFields Repo Owner — Impact & Benefits

### What changes in your repo

The table below shows every file that would be touched in a pull request.  Files marked
**unchanged** are not part of the PR at all.

| File | Change | Detail |
|------|--------|--------|
| `src/flowFieldsTypes.h` | Refactor | Variable *definitions* moved into `FlowFieldsEngine` class; the file keeps all type declarations, math helpers, noise classes, and drawing primitives exactly as they are |
| `src/flowFieldsEngine.hpp` | Refactor | Free functions `initFlowFields` / `runFlowFields` become `FlowFieldsEngine::setup` / `::run`; logic is identical |
| `src/emitters/*.h` | Minor edit | `gR[y][x]` → `g_engine->gRAt(x,y)`, `t` → `g_engine->t` — mechanical find-replace, no logic changes |
| `src/flows/*.h` | Minor edit | Same find-replace as emitters |
| `src/main.cpp` | 4 lines changed | `initFlowFields(myXY)` → `engine.setup(...)`, `runFlowFields()` → `engine.run(leds)` |
| `src/bleControl.h` | 1 line per cVar | Bare global `cOrbitSpeed` → `engine.cOrbitSpeed` — same name, same type, same value |
| `library.json` | **New file** | PlatformIO manifest; does not affect the firmware build at all |
| `examples/FastLEDMM/FlowFieldsEffect.h` | **New file** | Consumer example; not compiled by the standalone firmware |
| Everything else | **Unchanged** | `audio/`, `modulators.h`, `parameterSchema.h`, `componentEnums.h`, `index.html`, `platformio.ini`, `boardConfig.h`, … |

The BLE system, the web UI, the audio pipeline, the modulator framework, every emitter, every
flow field — none of their *logic* changes.  The PR is a structural refactor only: state that
currently lives in namespace-level globals moves into a class that owns it.

### What you get in return

**1. Listed in the PlatformIO registry**

Once `library.json` is present and the repo is public, anyone can add your effects to their
project with one line:

```ini
lib_deps = ewowi/FlowFields
```

PlatformIO will resolve, download, and wire up your library automatically.  No forking, no
copying files.

**2. Correct C++ (no ODR violation)**

The current header-defined globals (`bool flowFieldsInstance = false;` etc.) are technically
undefined behaviour when included from more than one translation unit.  The refactor fixes
this, making the code robust as projects grow and add more `.cpp` files.

**3. Runtime-configurable panel size**

The dynamic grid allocation means the engine works on any W×H panel without recompiling.
This benefits your own project too — changing panel size no longer requires a `#define` edit
and a full rebuild.

**4. Wider audience, more contributors**

FastLED-MM / projectMM is used across a growing community of LED installations.  Exposing
FlowFields as a library means those users can drop your emitters and flow fields into their
existing rigs with a single `addControl` / `bindParam` block.  Bug reports, new emitters, and
new flow fields are more likely to come back upstream when the barrier to entry is low.

**5. Your standalone firmware is unaffected**

The existing `platformio.ini`, BLE stack, web UI (`index.html`), and audio pipeline are not
part of the PR.  You flash and run the firmware exactly as you do today.  The only visible
difference in `main.cpp` is four lines.

---

## Session 1 Retrospective

Session 1 covered Phases 1, 2, and 6 from the plan — the structural core.

### Definition of Done

| Task | Status | Notes |
|------|--------|-------|
| `src/FlowFieldsEngine.h` — class declaration + `g_engine` extern | ✅ | Dynamic grids, noise members, modulator state, lifecycle API, callbacks |
| `src/flowFieldsTypes.h` — stripped to types-only | ✅ | All variable definitions removed; math helpers, noise classes, ColorF, ModConfig kept |
| `src/modulators.h` — stripped to type definitions only | ✅ | `timers` / `modulators` structs only; instances now live in `FlowFieldsEngine` |
| `src/flowFieldsEngine.hpp` — class method implementations | ✅ | `setup`/`run`/`teardown`, `calculate_modulators`, color helpers, drawing primitives, cVar bridge, dispatch tables, `g_engine` definition |
| All 6 emitter headers — `WIDTH`/`HEIGHT`/`MIN_DIMENSION` → `g_engine->` | ✅ | Logic unchanged; mechanical find-replace only |
| 5 simple flow headers — same replacement | ✅ | `flow_noise`, `flow_radial`, `flow_directional`, `flow_rings`, `flow_spiral` |
| `flow_fluid.h` — `SIM_SIZE` constexpr → runtime; `WIDTH`/`HEIGHT` → `g_engine->` | ✅ | Local `SIM_SIZE = (float)g_engine->_minDim` added in each function that uses it |
| `src/main.cpp` — `engine.setup()` / `engine.run()` | ✅ | Old `initFlowFields`/`runFlowFields` removed; callbacks wired |
| `platformio.ini` — GitHub deps instead of local `file://` paths | ✅ | FastLED and NimBLE-Arduino now resolved from GitHub |
| Build compiles cleanly | ✅ | Verified on `seeed_xiao_esp32s3` target |

### What Went Well

- **The `g_engine` pattern was invisible to callers.** All emitter and flow functions needed only a mechanical find-replace (`WIDTH` → `g_engine->_width`, etc.).  No logic changed in any of the 12 component files.
- **Dynamic `float**` grids dropped in without syntax changes.** Callers continue to write `g_engine->gR[y][x]` — the same `[y][x]` notation works for `float**` as for `float[H][W]`.
- **The cVar bridge translated directly.** `pushDefaultsToCVars` and `syncFromCVars` became class methods with no logic changes — they already referenced named structs that are now class members.
- **Separation of concerns paid off.** Because `flowFieldsTypes.h` already grouped types and `flowFieldsEngine.hpp` already grouped logic, the class extraction was a mechanical move, not a redesign.

### What Didn't Go So Well

- **Local library dependencies blocked the build.** `lib/FastLED-e713d6f` and `libNimBLE/NimBLE-Arduino-2.5.0` are gitignored. The specific FastLED commit (e713d6f) also has a `fl::span` overload ambiguity with ESP32 Arduino 3.3.5 that the local copy had apparently been patched to fix. Switched to a pinned FastLED 4.x pre-release commit (`c83f7632`, 2026-04-11) and NimBLE-Arduino from GitHub; the FastLED version change needs on-device verification.
- **`MIN_DIMENSION` in struct defaults was not updated.** Struct member initializers like `float orbitDiam = MIN_DIMENSION * 0.3f` still reference the `boardConfig.h` compile-time macro.  These values are overwritten by `syncFromCVars()` at runtime so behavior is unchanged — but they prevent the struct from being portable to a different grid size without recompiling.
- **pyyaml missing.** PlatformIO's ESP32 Arduino 3.3.5 framework requires `pyyaml` for its build script; had to install it manually.

### Recommendations for Session 2

**Portability cleanup (recommended before Phase 4 library manifest):**

- Replace the remaining `MIN_DIMENSION` references in struct default values with hardcoded constants that match the current tuned defaults (e.g., `orbitDiam = 6.6f`, `lineAmp = 13.5f`).  These are already the effective runtime values on the 22×22 grid.
- `boardConfig.h`'s `myXY()` still uses compile-time `WIDTH`/`HEIGHT` in the bounds check and index arithmetic. For the library's `library.json` to work cleanly, this function (or a replacement) needs to be runtime-aware.  The example `FlowFieldsEffect.h` in Phase 5 should demonstrate a portable `xyFunc`.

**Phase 3 — pointer binding** is purely additive: new `float* pXxx` members defaulting to `&cXxx`, and a `bindParam()` method.  No existing code changes.  Zero risk.

**Phase 4 — `library.json`** is a new file.  Zero risk.

**Phase 5 — FastLED-MM example** is a new file.  Zero risk.

**Before Phase 4:** verify on device that the FastLED version change (pinned pre-release 4.x commit `c83f7632` instead of the old local copy) produces identical visuals.  If there are regressions, bisect to a different 4.x commit in `platformio.ini`.
