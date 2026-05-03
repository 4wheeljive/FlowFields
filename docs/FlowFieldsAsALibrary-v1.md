# FlowFields as a Library — v1 (archived)

> This approach was rejected by the upstream owner as too invasive.
> See [FlowFieldsAsALibrary-v2.md](FlowFieldsAsALibrary-v2.md) for the v2 plan.

---

## Goal

Allow any project to add FlowFields as a PlatformIO dependency and expose all
emitters, flows, and parameters through its own UI system without a BLE
dependency.

---

## What Needed to Change

| Problem | Root cause |
|---|---|
| ODR violations in headers | `flowFieldsTypes.h` and `parameterSchema.h` defined variables (not just declared) at namespace scope — multiple-TU link error |
| Static compile-time grid dimensions | `static float gR[HEIGHT][WIDTH]` — size baked in at build; library consumers may have different or runtime panel sizes |
| No library manifest | PlatformIO needs `library.json` for `lib_deps = ewowi/FlowFields` |
| No lifecycle API | Engine exposed only free functions `initFlowFields` / `runFlowFields`; library consumers need `setup` / `loop` / `teardown` |

---

## Original 6-phase plan

| Phase | Description | Risk |
|---|---|---|
| 1 | Wrap engine in `FlowFieldsEngine` class; move namespace globals to members; add `g_engine` pointer; `setup`/`run`/`teardown` | Medium — every emitter/flow header + `flow_fluid.h` |
| 2 | Replace static `gR[H][W]` with heap `float**`; `setup()` allocates, `teardown()` frees | Medium — `flow_fluid.h` signatures |
| 3 | Pointer binding: `bindParam("name", float*)` redirects internal pointer to caller-owned float | Low — additive only |
| 4 | `library.json` PlatformIO manifest | Zero |
| 5 | `examples/FastLEDMM/FlowFieldsEffect.h` ProducerModule example | Zero |
| 6 | Update `main.cpp` call sites (4 lines) | Zero |

Phases 1+2+6 were Session 1. Phases 3+4+5 were Session 2.
Sessions 3–5 addressed bugs and improvements found after integration.

---

## Architecture (the v1 approach)

All engine state moved into a `FlowFieldsEngine` class. A file-scope
`static FlowFieldsEngine* g_engine = nullptr` pointer let every existing
emitter/flow header continue to read `g_engine->gR[y][x]`,
`g_engine->_width`, etc. after a mechanical find-replace.

```
FlowFieldsEngine { setup/run/teardown, grids (float**), noise, modulators,
                   all param structs, all cVars as public members }
           ↑
       g_engine  (set at start of each run())
           ↑
  emitter_*.h / flow_*.h read through g_engine→
```

Parameter binding (Phase 3): `bindParam("name", float*)` redirected an
engine pointer to a caller-owned float. Library consumer called `bindParam`
once in `setup()`; `run()` applied bindings via pointer dereference — zero
string lookup per frame. BLE layer wrote directly to `engine.cXxx` members;
default pointers already pointed there.

---

## Session 1 — Class extraction + dynamic grids

**Scope:** Phases 1, 2, 6 — `FlowFieldsEngine` class, `float**` grids,
`main.cpp` call-site update.

| Task | Result |
|---|---|
| `FlowFieldsEngine.h` — class declaration, `g_engine` extern | ✅ |
| `flowFieldsTypes.h` — stripped to types-only | ✅ |
| `flowFieldsEngine.hpp` — class method implementations | ✅ |
| All 6 emitter headers — `WIDTH/HEIGHT/MIN_DIMENSION` → `g_engine->` | ✅ |
| 5 simple flow headers — same | ✅ |
| `flow_fluid.h` — `SIM_SIZE` → runtime, arrays → `float**`, helper signatures `float (*)[WIDTH]` → `float**` | ✅ |
| `main.cpp` — `initFlowFields` → `engine.setup()`, `runFlowFields` → `engine.run()` | ✅ |
| Build compiles cleanly on `seeed_xiao_esp32s3` | ✅ |

**Key lessons:**
- `gR[y][x]` syntax works identically for `float**` — zero access-site changes needed.
  `float** gR` with row-pointer layout (`rows[y] = data + y*w`) preserves both syntax
  and contiguous memory; hardware prefetcher behaves identically to a flat array.
- `MIN_DIMENSION` in struct default initialisers (`orbitDiam = MIN_DIMENSION * 0.3f`)
  caused a null-ptr crash at static-init time (`g_engine` is null then). Fixed in Session 2
  by hardcoding (`orbitDiam = 6.6f`). Root cause: the find-replace of `MIN_DIMENSION →
  g_engine->_minDim` was applied everywhere, including static struct defaults where it was wrong.
- FastLED local-copy dependency was gitignored; had to switch to GitHub-pinned commit —
  introduced a FastLED version change that needed on-device verification (never completed).
- `flow_fluid.h` was the hardest single file: 6 internal 2-D arrays + 5 helper signatures
  `float (*)[WIDTH]` all changed. Bodies were unchanged but the type system change was
  non-mechanical.

---

## Session 2 — Library manifest, pointer binding, ODR fix

**Scope:** Phase 3 (pointer binding), Phase 4 (`library.json`), Phase 5 (example), portability cleanup, pre-use library fixes.

| Task | Result |
|---|---|
| Struct default `MIN_DIMENSION` → hardcoded values | ✅ |
| `library.json` — excludes `main.cpp`, `hosted_ble_bridge.cpp` | ✅ |
| `examples/FastLEDMM/FlowFieldsEffect.h` — `addControl` + `bindParam`; `loop()` is one line | ✅ |
| `bindParam(name, float*)` + `resolveCVar` | ✅ |
| ODR fix — `inline` on ~70 vars in `parameterSchema.h` | ✅ |
| `flowFieldsEngine.hpp` renamed → `.cpp` (PlatformIO only compiles `.cpp`) | ✅ |
| Audio — `#ifdef AUDIO_ENABLED` guard + no-op stub in `emitters_other.h` | ✅ |

**Key lessons:**
- ODR violations were latent — the single-TU firmware never caught them. `parameterSchema.h`
  defined ~70 mutable variables without `inline`; the `inline` fix is non-breaking for C++17
  single-TU builds but required touching every declaration.
- `.hpp` extension silently meant the library builder never compiled the file; any
  library consumer would have hit unresolved symbols. Not caught until review.
- Renaming `.hpp` → `.cpp` had two regressions: (1) removing `#include "flowFieldsEngine.hpp"`
  from `main.cpp` also dropped the only path to the engine class declaration — fixed by an
  explicit `#include "FlowFieldsEngine.h"`; (2) `emitters_other.h` compiled in the new
  separate TU without `main.cpp`'s audio include chain. Fix: `#ifdef AUDIO_ENABLED` guard.
- `bindParam` was designed and tested conceptually but not exercised against a running UI —
  the execution-order bug (Session 3) was not discovered until a slider was moved live.

---

## Session 3 — Binding bug fix, PSRAM, type widening

**Scope:** Critical binding bug (sliders had no effect in FastLED-MM),
`EMITTER`/`FLOW` encapsulation, PSRAM-aware allocation, dimension widening.

| Task | Result |
|---|---|
| `EMITTER`/`FLOW` globals → `_emitter`/`_flow` engine members | ✅ |
| PSRAM-aware `ps_malloc()` for all grid arrays | ✅ |
| Binding fix — `resolveCVar` → `resolveField` (points to struct fields, not cVars) | ✅ |
| Binding application moved to after `syncFromCVars()` in `run()` | ✅ |
| Dimension widening: `uint8_t → uint16_t` (dims), `uint16_t → uint32_t` (led count) | ✅ |
| `EMITTER_NAMES` / `FLOW_NAMES` inline arrays in `componentEnums.h` | ✅ |

**Binding bug root cause:** `pushDefaultsToCVars()` fires on the first frame and
on every emitter/flow change, silently overwriting whatever `bindParam` had
written. Fixed by reordering: defaults → cVars → `syncFromCVars` → bindings
(last-write wins, BLE path sees no bindings so it's a no-op).

**Remaining backlog after Session 3:**
- Audio integration: `audioTypes.h` has non-inline variables at namespace scope — a second
  TU including it causes duplicate-symbol link errors. Proposed fix: expose an
  `const AudioFrame* audioFrame` pointer on the engine; library consumers set it before
  each `run()` call, making the engine audio-agnostic. Not implemented.
- Typed cVar storage: BLE sends all values as `float`; ~12 cVars that should be `uint8_t`
  / `bool` / `uint16_t` were stored as `float` (or widened to float). `resolveField` still
  had two tiers — most params pointed to struct fields (immediate); shared params
  (`blendFactor`, `numDots`, `dotDiam`) still went through cVars (one-frame lag).
  Inconsistency was documented but not fully resolved; addressed in Session 4.

---

## Session 4 — Remove cVars entirely

**Scope:** Eliminated the entire cVar layer to give library consumers and BLE
one consistent write path: direct engine struct fields.

| Task | Result |
|---|---|
| `FlowFieldsParamTypes.h` extracted (breaks circular include created by B1) | ✅ |
| All 13 param struct instances moved into `FlowFieldsEngine` as public members | ✅ |
| All 13 emitter/flow headers updated to `g_engine->struct.field` | ✅ |
| `setParam` / `getParam` / `resolveField` removed from engine | ✅ |
| ~70 `c##Name` cVar globals removed from `parameterSchema.h` | ✅ |
| `syncFromCVars` / `pushDefaultsToCVars` deleted (~250 lines) | ✅ |
| `bleSetEngineParam(name, v)` / `bleGetEngineParam(name)` added to `bleControl.h` | ✅ |
| `FlowFieldsEffect.h` — `onUpdate()` writes struct fields directly; `loop()` is one line | ✅ |

```cpp
// Consumer onUpdate — direct struct writes, no bindings, no string lookup per frame
void onUpdate(const char* /*name*/) override {
    engine_._emitter            = emitterIdx_;
    engine_.orbitalDots.orbitSpeed = orbitSpeed_;
    engine_.noiseFlow.xSpeed    = xSpeed_;
    // … all params …
}
void loop() override { engine_.run(leds); }
```

**Benefits claimed by v1:**
- One write mechanism for everyone: `engine.orbitalDots.orbitSpeed = 2.5f` — obvious,
  immediate, typed. No string names, no float restriction on types.
- Adding a parameter: field on the struct + two lines in `bleControl.h`
  (`bleSetEngineParam` / `bleGetEngineParam`). Down from 5-file to 2-file change.
- `bool outward`, `bool axisFreeze[]`, `bool useRainbow` set as actual bools at the BLE
  boundary; no silent float cast throughout the engine.

**What this broke for the upstream owner:**
- The cVar bridge (`syncFromCVars`, `pushDefaultsToCVars`, X-macro, cVar globals)
  is load-bearing for BLE preset save/load and for the `sendEmitterState` state-sync
  path. Removing it required replacing all three with equivalent logic in
  `bleControl.h`. The 5-file parameter-add workflow grew to 7 files.
- `FlowFieldsParamTypes.h` extracted struct definitions from the emitter/flow headers
  where they were co-located with their functions — now two files to maintain per
  emitter/flow.

---

## Session 5 — Dynamic parameter visibility (plan only)

The engine supports 8 emitters × 6 flows. Only 8–15 of the ~35 controls are
relevant for any active combination. Session 5 designed four options:

| Option | Mechanism | Requires |
|---|---|---|
| A | Detect emitter/flow change in `onUpdate`, call `clearControls()` + re-register subset | `clearControls()` API from projectMM (not yet available) |
| B | `setControlVisible(name, bool)` from `onUpdate` | Same new projectMM API |
| C | One effect class per emitter (8 classes), hard-codes its emitter, registers only its params | Works today; user switches effects to switch emitters |
| D | Separate `EmitterModule` + `FlowModule` sharing one engine instance | Requires projectMM module composition |

**Key insight:** the existing `EMITTER_PARAM_LOOKUP[]` / `FLOW_PARAM_LOOKUP[]`
tables in `parameterSchema.h` already drive this for the BLE/web-UI side
(per-emitter JSON sent to browser). The same data should drive Option A/B once
the projectMM API exists — the switch/case pseudocode was added to
`FlowFieldsEffect.h` as a placeholder. Options A and B both require new projectMM
API (`clearControls()` or `setControlVisible()`); Option C works today at the cost
of 8 separate effect classes.

---

## Why v1 was rejected

By Session 4 the upstream project's own design idioms had been systematically
removed:

- The X-macro `PARAMETER_TABLE` and cVar globals, which the author uses to add
  and sync parameters, were deleted.
- The BLE/web-UI state-sync path was reworked to drive engine struct fields
  through new `bleSetEngineParam` / `bleGetEngineParam` dispatch functions.
- Struct definitions were split from their emitter/flow functions into a new
  header `FlowFieldsParamTypes.h`.
- Every emitter and flow header was changed (Sessions 1 + 4).
- The 5-file parameter-add recipe grew and changed.

The author's working world — adding a slider, tweaking defaults, tuning a
modulator — had been redesigned around the needs of library consumers. The v1
plan treated the existing code as raw material rather than as a finished system
to expose.

Net impact vs. `dbb877f`:

| Metric | v1 |
|---|---|
| Files modified | ~30 |
| Files added | 4 (`FlowFieldsEngine.h`, `FlowFieldsParamTypes.h`, `library.json`, `examples/`) |
| Lines deleted | ~250 (`syncFromCVars`, `pushDefaultsToCVars`, ~70 cVar globals) |
| Emitter / flow headers changed | all 11 (Sessions 1 + 4) |
| BLE layer changes | `bleControl.h` reworked to drive struct fields directly |
| On-device visual verification | never completed |
| Author's 5-file parameter-add workflow | grew to 7 files, structure changed |

---

## Why Tier B was skipped in v2

The 15 changes below were part of v1 but are not carried over to v2. Every one
is a consequence of B1 (engine-class refactor) — remove B1 and the rest
become unnecessary. The v2 thin-façade approach achieves the same library
isolation without any of them.

| # | v1 change | Why v2 skips it |
|---|---|---|
| B1 | Wrap engine state in `FlowFieldsEngine` class | Single shim TU achieves the same isolation without rewriting every emitter/flow file |
| B2 | `g_engine` file-scope pointer | Only needed because of B1 |
| B3 | Free functions → class methods | Façade trampolines; existing free functions stay |
| B4 | ~40 namespace globals → class members | Only needed because of B1; namespace globals are fine inside a single TU |
| B5 | Drawing primitives → class methods | Same as B4 |
| B6 | Pointer binding (`bindParam` / `pXxx`) | `setParameter("name", v)` is simpler and reuses the existing string registry |
| B7 | Remove cVar globals | cVars are load-bearing for BLE / X-macro / preset save/load |
| B8 | Remove `syncFromCVars` / `pushDefaultsToCVars` | Only needed because of B7; v2 reuses both as the parameter-application path |
| B9 | Move parameter dispatch to `bleSetEngineParam` | Only needed because of B7; current `processNumber()` already handles this |
| B10 | Extract `FlowFieldsParamTypes.h` | Only needed because of B1 (broke a circular include the existing tree doesn't have) |
| B11 | Encapsulate `EMITTER` / `FLOW` globals into engine | Existing globals work; façade exposes them via `setEmitter` / `setFlow` |
| B12 | Rename `flowFieldsEngine.hpp` → `.cpp` | New TU is `FlowFieldsModule.cpp`; it `#include`s the unchanged `.hpp` (same shape `main.cpp` already uses) |
| B13 | `inline` on ~70 vars in `parameterSchema.h` (ODR fix) | Engine headers visible to only one TU in v2; ODR violation doesn't manifest |
| B14 | `#ifdef AUDIO_ENABLED` guard in `emitters_other.h` | Single shim TU has the same include view as `main.cpp` |
| B15 | Hardcode struct default values | The null-ptr crash this fixed only existed because of B1 |
