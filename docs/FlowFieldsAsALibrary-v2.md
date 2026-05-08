# FlowFields as a Library — v2

Restart from upstream `dbb877f`. v1 (see [FlowFieldsAsALibrary-v1.md](FlowFieldsAsALibrary-v1.md))
was rejected as too invasive.

---

## 1. Why v1 didn't work

v1 reshaped the engine into a `FlowFieldsEngine` class so library consumers
became its primary user; cVars were deleted, every emitter/flow header
rewritten, the BLE side reworked to drive engine struct fields directly. Each
cleanup exposed another seam (ODR violations, `.hpp` not picked up by the
library builder, audio transitive includes, null-pointer struct defaults,
binding-order bug). The author's existing 5-file parameter-add workflow grew,
and design choices that were deliberate (cVar bridge, X-macro, 5-file pattern)
were removed in the name of "cleanup". Net effect upstream: ~30 files
touched, ~250 lines deleted, every emitter/flow header changed.

**The mistake:** treating the existing engine as raw material to be reshaped.
v2 inverts this: the engine stays as it is, library exposure is a thin shim
on top.

---

## 2. Hard requirements

1. **FastLED-MM module lifecycle:** `setup()`, `loop()`, `teardown()`,
   `onSizeChanged()`. Panel size changes at runtime.
2. **Parameter writes from `onUpdate(name)`** — when a slider moves, FastLED-MM
   calls `onUpdate(name)` and the new value must reach the engine.
3. **Dynamic grid alloc/free** — no compile-time max; buffers created in
   `setup()`, freed in `teardown()`, re-created on `onSizeChanged()`.
4. **Big-panel support** — up to 128×128 on ESP32-S3, up to 1000×1000 on PC.
5. **Engine state info exposed** — emitter/flow names + per-emitter/per-flow
   parameter lists, so FastLED-MM can show only the relevant sliders.
6. **Minimum upstream disturbance** — author's BLE / cVars / X-macro / 5-file
   parameter workflow stays exactly as at `dbb877f`.

---

## 3. Proposal — thin façade + dynamic grids

### 3.1 Three new files (the library)

```cpp
// src/FlowFieldsModule.h            (~40 lines, public API)
class FlowFieldsModule {
public:
    using XYFunc = uint32_t (*)(uint16_t, uint16_t);

    void  setup(uint16_t w, uint16_t h, XYFunc xy, fl::CRGB* leds, uint32_t n);
    void  onSizeChanged(uint16_t w, uint16_t h);
    void  loop();
    void  teardown();

    void  setEmitter(uint8_t);
    void  setFlow   (uint8_t);
    void  setParameter(const char* name, float value);
    float getParameter(const char* name) const;

    // Info for dynamic UI (uses existing PARAM_LOOKUP tables)
    struct ParamList { const char* const* names; uint8_t count; };
    ParamList  getGlobalParams()                  const;
    ParamList  getEmitterParams(uint8_t idx)      const;
    ParamList  getFlowParams   (uint8_t idx)      const;

    static uint8_t      emitterCount();
    static uint8_t      flowCount();
    static const char*  emitterName(uint8_t);
    static const char*  flowName   (uint8_t);
};
```

```cpp
// src/FlowFieldsModule.cpp          (the only TU that includes engine headers)
#include "FlowFieldsModule.h"
#include "flowFieldsEngine.hpp"     // unchanged — included exactly as main.cpp does
#include "parameterSchema.h"        // unchanged cVars + PARAMETER_TABLE X-macro

// Runtime panel dims — declared extern here, defined by boardConfig.h in firmware.
// In library mode these are set by setup() / onSizeChanged() below.
extern int WIDTH, HEIGHT, MIN_DIMENSION;
extern uint32_t NUM_LEDS;

void FlowFieldsModule::setup(uint16_t w, uint16_t h, XYFunc xy,
                              fl::CRGB* /*leds*/, uint32_t n) {
    WIDTH = w;  HEIGHT = h;  MIN_DIMENSION = fl::min(w, h);  NUM_LEDS = n;
    flowFields::initFlowFields(xy);
}
void FlowFieldsModule::onSizeChanged(uint16_t w, uint16_t h) {
    // 1. Allocate new grids, nearest-neighbour scale pixel content, free old.
    // 2. Update dims — next loop() renders at new size with plausible content.
    WIDTH = w;  HEIGHT = h;  MIN_DIMENSION = fl::min(w, h);
    flowFields::resizeFlowFields(w, h);   // new helper in flowFieldsEngine.hpp
}
void FlowFieldsModule::loop()    { flowFields::runFlowFields(); }
void FlowFieldsModule::teardown(){ flowFields::teardownFlowFields(); }

void FlowFieldsModule::setEmitter(uint8_t i) { EMITTER = i; }
void FlowFieldsModule::setFlow   (uint8_t i) { FLOW    = i; }

void FlowFieldsModule::setParameter(const char* name, float v) {
    // Walk PARAMETER_TABLE X-macro; write matching c##Name cVar.
    // Special cases: "emitter"/"flow" write EMITTER/FLOW globals directly.
    #define X(type, Name, def) \
        if (strcasecmp(name, #Name) == 0) { c##Name = (type)v; return; }
    PARAMETER_TABLE
    #undef X
    if (strcasecmp(name, "emitter") == 0) { EMITTER = (uint8_t)v; return; }
    if (strcasecmp(name, "flow")    == 0) { FLOW    = (uint8_t)v; return; }
}
```

`FlowFieldsModule.cpp` is the **only** TU that includes the engine headers —
single-TU constraint already satisfied by the firmware also holds in the
library, so no ODR cleanup, no `inline` adds, no `.hpp` → `.cpp` rename.

### 3.2 Engine touch — minimum needed for dynamic alloc + big panels

| File | Edit | Severity |
|---|---|---|
| `flowFieldsTypes.h` | `static float gR[H][W]` → `static float** gR` (×6 grids + 2 noise profiles). Add `allocGrids(w,h)` / `freeGrids()` using `ps_malloc` (PSRAM-aware). Widen `xyFunc` typedef. | Low — declarations + 2 helpers |
| `flowFieldsEngine.hpp` | `initFlowFields()` calls `allocGrids(w,h)`. Add `teardownFlowFields()` calling `freeGrids()`. Widen LED-copy loop counters & index cast. | Low — 4–5 lines |
| `flows/flow_fluid.h` | 6 internal arrays go `float**`, **5 helper signatures change** (`float (*)[WIDTH]` → `float**`), `SIM_SIZE` becomes runtime via local. | **High** — the non-trivial edit; bodies unchanged |
| `componentEnums.h` | **Add** `inline const char* const EMITTER_NAMES[]` and `FLOW_NAMES[]` arrays. | Trivial — pure addition |
| `boardConfig.h` | `myXY` signature widened to `uint32_t myXY(uint16_t x, uint16_t y)` | Trivial — type-only |
| `main.cpp` | `uint16_t ledNum` → `uint32_t ledNum` | Trivial — single token |

`WIDTH` / `HEIGHT` / `MIN_DIMENSION` change from `boardConfig.h` macros to
`extern int`; `NUM_LEDS` to `extern uint32_t`. In firmware mode these are
initialised from the existing boardConfig macros. In library mode `setup()`
sets them. Loops `for (y=0; y<HEIGHT; ...)` work unchanged. **All 6
emitter headers and 5 simple flow headers are not touched** — `gR[y][x]`
syntax works identically with `float**`.

### 3.3 Memory layout — single allocation per grid

```cpp
float* data = ps_malloc(w * h * sizeof(float));   // contiguous backing
float** rows = malloc(h * sizeof(float*));
for (int y = 0; y < h; y++) rows[y] = data + y*w;
gR = rows;
```

Hardware prefetcher behaves identically to a flat array (memory **is** flat;
row pointers just index into it). Free is `free(data); free(rows);`.

### 3.4 Performance at scale (sequential access dominates)

The compiler hoists `gR[y]` out of the inner loop:

```cpp
for (int y = 0; y < HEIGHT; y++) {
    float* row = gR[y];                    // ONE load per row
    for (int x = 0; x < WIDTH; x++) row[x] = ...;   // identical to flat
}
```

Inner-loop cost is identical to flat `float*`. Outer-loop overhead is one
pointer load per row — negligible at every scale:

| Panel | Row-ptr table size | Outer loads/frame | Working set |
|---|---|---|---|
| 22×22 | 88 B | 22 | 23 KB |
| 128×128 | 512 B | 128 | 768 KB |
| 1000×1000 | 4 KB | 1000 | 24 MB (DRAM-bound, indexing in noise) |

Random-access drawing primitives pay one pointer load per pixel **touched**,
not per cell — cost scales with content, not panel area. **No alternative
indexing scheme needed**; if profiling later flags a hot spot on PC builds,
swap-in a `Grid` wrapper class is a 1-file pocket optimisation.

### 3.5 Parameter writes from `onUpdate(name)`

Consumer:
```cpp
void onUpdate(const char* name) override {
    ff_.setParameter(name, controlValue(name));
}
```

`setParameter(name, v)` walks the existing `PARAMETER_TABLE` X-macro with
`strcasecmp` and writes the matching `c##Name` cVar. Upstream's
`syncFromCVars()` then runs once per frame and lands the value in the right
struct field — **including the existing shared-parameter fan-out**
(`blendFactor` to three flow structs, `numDots`/`dotDiam` to two emitters).
Special cases `"emitter"` / `"flow"` route to `EMITTER` / `FLOW` globals;
booleans are cast at the boundary.

Cost: one `strcasecmp` walk per slider event. Zero per-frame cost. No
lookup in the hot path.

### 3.6 Net upstream change

| | v1 | v2 |
|---|---|---|
| Files modified | ~30 | **6** (4 type-only or additive; 2 substantive: `flow_fluid.h` signatures, `flowFieldsTypes.h` decls) |
| Files added | 4 | **3** (`FlowFieldsModule.h/.cpp`, `library.json`) |
| Lines deleted upstream | ~250 | **0** |
| cVar / X-macro / 5-file workflow | dismantled | **untouched** |
| Emitter / simple-flow headers | all 11 changed | **0** |
| Risk to firmware | Medium (every emitter/flow file touched) | Medium-low (concentrated in `flow_fluid.h`; visual A/B verifies) |

---

## 4. Phased plan

| Phase | Scope | Effort |
|---|---|---|
| **1** Shim + dynamic grids | New 3 files, `flowFieldsTypes.h` heap alloc, `flowFieldsEngine.hpp` setup/teardown calls, `flow_fluid.h` signature edit, dimension widening, NAMES arrays. Verify firmware visuals (esp. fluid emitter) on device. | ~5 h |
| **2** Parameter & info pass-through | `setParameter`/`getParameter` via `PARAMETER_TABLE` walker. `getEmitterParams`/`getFlowParams`/`getGlobalParams` reading existing PROGMEM lookup tables. FastLED-MM example showing `onUpdate` flow. | ~2 h |
| **3** (optional, later) PC perf polish | If 1000×1000 PC builds profile a hot-spot, drop in a `Grid` wrapper class — single-file change. Not on the critical path. | varies |

Phase 1 is the only phase with regression risk; everything from Phase 2 onward
is purely additive shim work.

---

## 5. v1 changes vs. v2 — full reconciliation

**Improves** = real technical merit. **Different approach** = same outcome,
different style (no real win).

### Tier A — Required for the library, kept in v2

| # | v1 change | Improves? | v2 verdict |
|---|---|---|---|
| A1 | `library.json` PlatformIO manifest | ✅ Required | **Keep** |
| A2 | Lifecycle entry points (`setup` / `loop` / `teardown`) | ✅ Required | **Keep** as façade methods, not engine refactor |
| A3 | Runtime panel size at `setup()` + `onSizeChanged()` | ✅ Required | **Keep** via dynamic alloc/free + `extern int WIDTH/HEIGHT` |
| A4 | Library-side parameter API | ✅ Required | **Keep** as `setParameter("name", v)` reusing `PARAMETER_TABLE` |
| A5 | FastLED-MM ProducerModule example | ✅ Required | **Keep** in `examples/FastLEDMM/` |

### Tier B — v1 architectural changes skipped by v2

15 changes from v1 are not carried over — all consequence of the engine-class
refactor that v2 avoids. See [FlowFieldsAsALibrary-v1.md](FlowFieldsAsALibrary-v1.md)
§ "Why Tier B was skipped" for the full table and per-item rationale.

### Tier C — Independent improvements

| # | v1 change | Improves? | v2 verdict |
|---|---|---|---|
| C1 | Dynamic grid alloc/free + PSRAM-aware | ✅ Required (req #3) | **Adopt** — `ps_malloc` for data block; one file for `flow_fluid.h` signature edit |
| C2 | Dimension widening uint8 → uint16, ledcount → uint32 | ✅ Required (req #4) | **Adopt** — type-only changes across `flowFieldsTypes.h`, `flowFieldsEngine.hpp`, `boardConfig.h`, `main.cpp` |
| C3 | `EMITTER_NAMES` / `FLOW_NAMES` in `componentEnums.h` | ✅ Required (req #5) | **Adopt** — pure addition, ~10 lines |
| C4 | Dynamic param-list info | ✅ Required (req #5) | **Adopt** as `getEmitterParams` / `getFlowParams` / `getGlobalParams` reading existing `EMITTER_PARAM_LOOKUP[]` / `FLOW_PARAM_LOOKUP[]` / `GLOBAL_PARAMS[]`. **No engine change.** UI rendering is FastLED-MM's job |

---

## 6. Q&A

**FlowFields**:

1. Is `src/FlowFieldsModule.h/.cpp` an acceptable location, or do you want
   them under `lib/FlowFieldsModule/`?
   → **Proposed:** `src/` — simplest for PlatformIO discovery; no extra
   `lib_deps` path needed. Move to `lib/` only if the author prefers the
   library code visually separated from firmware code.

2. Is the `flow_fluid.h` signature edit acceptable? It's the one substantive
   touch on existing engine code; the bodies are unchanged but ~5 helper
   signatures move from `float (*)[WIDTH]` to `float**`. v1 did this
   successfully but visual on-device verification of the fluid emitter was
   never completed.
   → **✅ Confirmed**

3. Phase 1 implementation: prefer (a) reuse existing `syncFromCVars()` as the
   apply path, or (b) `setParameter` writes directly to struct fields
   bypassing cVars? Option (a) is the v2 default and preserves BLE/preset
   parity; option (b) is simpler but breaks shared-fan-out behaviour.
   → **Proposed (a):** `setParameter` writes the cVar; `syncFromCVars()` runs
   once per frame as today. Preserves all shared-fan-out logic already in the
   engine. No engine logic change.

4. OK to add `library.json` at the repo root? Firmware build is unaffected;
   it only gates `lib_deps = ewowi/FlowFields`.
   → **✅ Confirmed**

**FastLED-MM**:

5. What does FastLED-MM's `onSizeChanged()` contract require — that the next
   `loop()` already renders at the new size, or is one frame of black
   acceptable?
   → **✅ Frame of black NOT acceptable (confirmed by FastLED-MM side).**
   Solution: on `onSizeChanged(w, h)`, allocate new grids first, then
   nearest-neighbour scale the existing pixel content into them (row-by-row,
   ~microseconds), then free the old grids and update `WIDTH`/`HEIGHT`. Timing
   state (`t`, `dt`, noise generators) is preserved across resize. The next
   `loop()` call renders at the new size with plausible content — no black
   frame. Full reset of timing state is skipped; `initFlowFields` is not
   re-called, only the grid realloc+scale step runs.

6. PSRAM availability — FastLED-MM defines `BOARD_HAS_PSRAM` consistently?
   `ps_malloc` falls back to `malloc` when PSRAM is absent, so this is
   informational rather than blocking.
   → **Proposed:** use `ps_malloc` guarded by `#if defined(BOARD_HAS_PSRAM)`,
   plain `malloc` otherwise. If FastLED-MM uses a different guard name, a
   single `#define` alias in `FlowFieldsModule.cpp` resolves it.

7. PC build allocator — is `malloc` for the 24 MB working set at 1000×1000
   acceptable, or does FastLED-MM wrap allocations through a custom allocator
   we should plumb through?
   → **Proposed:** standard `malloc` / `free` for now. The `allocGrids` /
   `freeGrids` helpers in `flowFieldsTypes.h` are a single seam — if
   FastLED-MM later needs a custom allocator it can be plumbed in there
   without touching any other file.

---

## 7. TL;DR

- v1 reshaped the engine and was rejected.
- v2 leaves cVars / X-macro / 5-file workflow intact and adds a 3-file shim
  (`FlowFieldsModule.h/.cpp` + `library.json`).
- Engine touch: dynamic alloc in `flowFieldsTypes.h` + `flowFieldsEngine.hpp`;
  one substantive edit in `flow_fluid.h` (signature change, bodies unchanged);
  type widening in 4 files; NAMES arrays added to `componentEnums.h`.
- **6 files modified, 3 added, 0 lines deleted.** Author's parameter-add
  workflow unchanged.
- FastLED-MM gets `setup` / `loop` / `teardown` / `onSizeChanged`,
  `setParameter("name", v)` driven from `onUpdate()`, and three info queries
  reading the existing PROGMEM lookup tables for dynamic UI.
- `float**` + contiguous backing scales fine to 1000×1000 (compiler hoists
  the row-pointer load; inner loop is identical to flat array).
- 15 architectural Tier-B changes from v1 are skipped; rationale in
  [FlowFieldsAsALibrary-v1.md](FlowFieldsAsALibrary-v1.md).
