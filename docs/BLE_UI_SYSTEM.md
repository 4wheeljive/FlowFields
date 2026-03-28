# FlowFields BLE/UI Control System

NOTE: This file was originally prepared as part of the AuroraPortal project. Certain elements no longer apply, such as PROGRAM and MODE. Important new elements include EMITTER and FLOW, which are not reflected here yet. Numerous examples of variable names below are not part of the current project, but the standardized naming convention still applies. There are also some different Parameter Regsitries and Auto-rendering grouped components. The former system where specific modes MODES belonged to specific PROGRAMS, and the associated UI rendering, is gone. The UI selector boxes for these were replaced by EMITTER and FLOW, which are completely independent. The lookup system for creating program-mode names no longer exists. The preset system is disabled and likely does not reflect the current program architecture. Various "sync state" functions are also different.      


## Overview

FlowFields uses a Web Bluetooth (BLE) bridge between a browser-based UI (`index.html`) and an ESP32 controller (`bleControl.h`). All communication is JSON-based (except raw button values), flowing over 4 BLE characteristics. The UI is built entirely with vanilla Web Components that auto-render controls based on the active "visualizer."

---

## 1. BLE Transport Layer

### 1.1 Four Characteristics

| Characteristic | UUID suffix | Purpose | Send format | Receive (notify) format |
|---|---|---|---|---|
| **Button** | `...1214` | Programs, modes, presets, triggers | Raw `Uint8Array([value])` | Raw string of the value |
| **Checkbox** | `...2214` | Boolean toggles | `{"id":"cxN","val":bool}` | Same JSON echoed back |
| **Number** | `...3214` | Slider/dropdown values | `{"id":"inParam","val":float}` | Same JSON echoed back |
| **String** | `...4214` | State sync | `{"id":"..","val":".."}` | Same JSON echoed back |

All four characteristics support READ, WRITE, and NOTIFY.

### 1.2 Communication Flow

```
UI Action --> write to characteristic --> ESP32 BLE callback
  --> processButton/Checkbox/Number/String()
  --> applies value to global `cParam` variable
  --> sendReceipt*() notifies back on same characteristic
  --> UI handleChange handler updates display
```

The "receipt" pattern ensures the UI always reflects the device's actual accepted value.

### 1.3 BLE MTU Constraint

The String characteristic has a practical payload limit of ~250 bytes (after MTU negotiation and JSON escaping overhead). State sync messages that exceed this are silently truncated, causing JSON parse errors on the UI side. Any new state sync message must be kept within this limit -- split into multiple smaller messages if necessary (see Section 7.3 for the bus state example).

### 1.4 Bus Parameters (extended Number format)

For per-bus audio parameters, the Number characteristic carries an extra `"bus"` field:
```json
{"id": "inThreshold", "val": 0.35, "bus": 0}
```
On the ESP32 side, `processNumber()` checks for `busId >= 0` and delegates to a function-pointer callback (`setBusParam -> handleBusParam`) which routes to `busA`/`busB`/`busC` struct fields directly.

On the UI side, `sendBusParamCharacteristic(paramId, value, busId)` handles this.

---

## 2. Deprecated
---

## 3. Variable Naming Conventions

The system uses a strict prefix-based naming convention that threads through the entire stack:

| Context | Pattern | Example | Where Used |
|---|---|---|---|
| **C++ global variable** | `c` + PascalCase | `cZoom`, `cSpeed` | `bleControl.h` globals |
| **BLE parameter ID** | `in` + PascalCase | `inZoom`, `inSpeed` | JSON `"id"` field over BLE |
| **JS registry key** | camelCase | `zoom`, `speed` | `VISUALIZER_PARAMETER_REGISTRY` |
| **HTML attribute** | `parameter-id="inZoom"` | `parameter-id="inZoom"` | `<control-slider>` elements |
| **Visualizer param list** | camelCase | `["zoom","speed"]` | `VISUALIZER_PARAMS` / C++ arrays |
| **Checkbox ID** | `cx` + number | `cx5`, `cx10` | `sendCheckboxCharacteristic` |

### Conversion helpers (JS):
- `getParameterLabel(paramName)` - `"zoom"` -> `"Zoom"` (capitalize first letter)
- `getParameterId(paramName)` - `"zoom"` -> `"inZoom"` (add `in` prefix + capitalize)

### X-Macro Convention (C++):
The `PARAMETER_TABLE` X-macro entries use PascalCase names (e.g., `Zoom`). The macro expansions:
- `c##parameter` -> `cZoom` (the global variable)
- `"in" #parameter` -> `"inZoom"` (the BLE parameter ID)
- `#parameter` -> `"Zoom"` (for JSON key in presets/state)

---

## 4. Parameter Registries

### 4.1 Parallel Registries (C++ and JS must stay in sync)

**Visualizer parameters** - which sliders to show for each visualizer:
- C++: `VISUALIZER_PARAM_LOOKUP[]` array of `VisualizerParamEntry` structs
- JS: `VISUALIZER_PARAMS` object (`{"radii-octopus": ["zoom","angle","speedInt"], ...}`)

**Parameter ranges** - min/max/step/default for each parameter:
- JS only: `VISUALIZER_PARAMETER_REGISTRY` object
- C++: defaults live in the `PARAMETER_TABLE` X-macro (but not ranges)

**Audio parameters** - global audio controls (not per-bus):
- JS: `AUDIO_PARAMETER_REGISTRY` + `AUDIO_PARAMS`
- C++: `AUDIO_PARAMS[]` array, used by `sendAudioState()` to iterate and match against `PARAMETER_TABLE`

**Bus parameters** - per-bus beat/envelope settings:
- JS: `BUS_PARAMETER_REGISTRY` (threshold, minBeatInterval, expDecayFactor, rampAttack, rampDecay, peakBase)
- C++: `Bus` struct fields in `audioTypes.h`, accessed via `setBusParam`/`getBusParam` callback pointers

### 4.2 X-Macro Parameter Table

The `PARAMETER_TABLE` in `bleControl.h` is the single source of truth for all `cParam` variables. It drives:

1. **`captureCurrentParameters()`** - serializes all `cParam` values into a JSON object (for preset save)
2. **`applyCurrentParameters()`** - deserializes JSON into `cParam` variables + sends UI receipts (for preset load)
3. **`processNumber()`** - auto-generated `if (receivedID == "inParam") { cParam = value; }` chains
4. **`sendVisualizerState()`** - iterates the current visualizer's param list and reads matching `cParam` values via `strcasecmp`
5. **`sendAudioState()`** - iterates `AUDIO_PARAMS[]` and reads matching `cParam` values via `strcasecmp`

Each entry is `X(type, PascalName, defaultValue)`:
```cpp
X(float, Zoom, 1.0f)       // creates cZoom, matches "inZoom", serializes as "Zoom"
X(float, Speed, 1.0f)      // creates cSpeed, matches "inSpeed", serializes as "Speed"
```

### 4.3 Bus Parameters -- Outside the X-Macro System

Bus parameters (`threshold`, `minBeatInterval`, `expDecayFactor`, `rampAttack`, `rampDecay`, `peakBase`) live directly on `myAudio::Bus` struct instances (`busA`, `busB`, `busC`), not as `cParam` globals. They are accessed via function-pointer callbacks registered during `initAudioProcessing()`:

- **`setBusParam`** (setter): `handleBusParam(busId, paramId, value)` - routes UI writes to the correct Bus field
- **`getBusParam`** (getter): `handleGetBusParam(busId, paramName)` - reads current Bus field values for state sync

This callback pattern decouples `bleControl.h` from the `myAudio` namespace.

### 4.4 Mode Audio Presets (Bus Parameter Overrides)

Some visualizer modes override bus parameter defaults on mode entry. This is handled by `ModeAudioPreset` structs in `animartrix_detail.hpp`:
- `applyModeAudioPreset(MODE)` is called whenever `MODE` changes
- Uses `BusPreset` with a `-1` sentinel: fields set to `-1` are not overridden
- After the preset is applied, the user can still adjust individual bus params via the UI
- `sendBusState()` reads whatever values the Bus structs currently hold, regardless of source (init defaults, mode preset, or user adjustment)

---

## 5. Web Components

### 5.1 Component Architecture

Components fall into two categories:

**"One-off" controls** - placed directly in HTML with explicit attributes:
| Component | Tag | Description |
|---|---|---|
| `BLEConnectionPanel` | `<ble-connection-panel>` | Connect/disconnect buttons + status |
| `ControlButton` | `<control-button>` | Sends a button value; attrs: `label`, `data-my-number` |
| `ControlCheckbox` | `<control-checkbox>` | Boolean toggle; attrs: `label`, `data-my-number`, `checked`/`unchecked` |
| `ControlSlider` | `<control-slider>` | Sends number value; attrs: `label`, `parameter-id`, `min`, `max`, `step`, `default-value` |
| `ControlDropdown` | `<control-dropdown>` | Index selector; attrs: `label`, `parameter-id`; options from inner text |

**Auto-rendering grouped components** - dynamically generate their content:
| Component | Tag | Description |
|---|---|---|
| `ProgramSelector` | `<program-selector>` | Grid of program buttons, highlights current |
| `ModeSelector` | `<mode-selector>` | Grid of mode buttons, updates when program changes |
| `ParameterSettings` | `<parameter-settings>` | Auto-renders sliders from `VISUALIZER_PARAMS[currentVisualizer]` |
| `AudioSettings` | `<audio-settings>` | Auto-renders sliders from `AUDIO_PARAMS["audio"]` |
| `BusSettings` | `<bus-settings>` | Renders 3 groups (A/B/C) of sliders from `BUS_PARAMETER_REGISTRY` |
| `Presets` | `<preset-controls>` | Save (101-110) / Load (121-130) preset button grids |

### 5.2 Dynamic Visibility (`data-visualizers`)

Any HTML element (including one-off controls and wrapper `<div>`s) can have a `data-visualizers` attribute:
```html
<control-checkbox label="Rotate waves" data-my-number="10" data-visualizers="waves" checked>
<control-button label="Trigger" data-my-number="160" data-visualizers="fxwave2d, animartrix">
```

`updateDynamicControls()` runs whenever the visualizer changes and:
1. Queries ALL elements with `[data-visualizers]`
2. Parses the comma-separated patterns
3. Uses `visualizerMatches()` which supports both exact match (`"cube"`) and prefix match (`"animartrix"` matches `"animartrix-polarwaves"`)
4. Sets `display: ''` or `display: 'none'`

### 5.3 Rendering Lifecycle

```
BLE Connect
  --> syncInitialState()
      --> sends button 92 (visualizer state)
      --> sends button 93 (audio state)
      --> sends button 94 (bus state)
      (200ms delays between requests to avoid BLE congestion)

Visualizer state response:
  --> applyReceivedString("visualizerState", ...)
      --> Atomic sync: updates ProgramSelector + ModeSelector display WITHOUT
          triggering intermediate re-renders
      --> Renders ParameterSettings once with the correct visualizer
      --> Applies device parameter values to sliders

Audio state response:
  --> applyReceivedString("audioState", ...)
      --> Updates AudioSettings sliders with device values

Bus state responses (one per bus):
  --> applyReceivedString("busState", ...) x3
      --> Each message contains one bus's params
      --> Updates BusSettings sliders via syncInputs(busId, paramName, value)

Program/Mode change (user clicks):
  --> selectProgram()/selectMode() sends button value
  --> ESP32 processButton() sets PROGRAM/MODE, sends receipt
  --> applyReceivedButton() updates:
      1. ProgramSelector.updateDisplayProgram()
      2. ModeSelector.updateModes() (re-renders mode buttons for new program)
      3. ParameterSettings.updateParameters() (re-renders sliders for new visualizer)
      4. updateDynamicControls() (show/hide one-off controls)
```

---

## 6. Preset System

### 6.1 File Format (LittleFS on ESP32)

Presets are saved as `/preset_N.json`:
```json
{
  "programNum": 6,
  "modeNum": 2,
  "parameters": {
    "Speed": 1.5,
    "Zoom": 0.8,
    ...all PARAMETER_TABLE entries...
  }
}
```

### 6.2 Save/Load Flow

- **Save**: Button values 101-120 -> `savePreset(N)` -> `captureCurrentParameters()` captures ALL `cParam` values
- **Load**: Button values 121-140 -> `loadPreset(N)` -> sets `PROGRAM`/`MODE`, then `applyCurrentParameters()` sets each `cParam` and sends `sendReceiptNumber("inParam", value)` for UI sync

### 6.3 Preset Gaps

Presets currently only capture `PARAMETER_TABLE` (`cParam` globals). Not captured:
- Bus parameters (live on Bus structs, not cParam globals)
- Checkbox/boolean states (Layer1-9, audioEnabled, etc.)

---

## 7. State Sync Mechanisms

### 7.1 Visualizer State Sync (button 92)

`sendVisualizerState()`:
1. Gets current visualizer name
2. Looks up the parameter list for that visualizer via `VISUALIZER_PARAM_LOOKUP`
3. Uses X-macro + `strcasecmp` to match each param name to its `cParam` variable
4. Builds JSON with program, mode, and parameter values
5. Sends via string characteristic as `{"id":"visualizerState","val":"<JSON>"}`

The UI's `applyReceivedString()` handler performs an **atomic sync**:
1. Updates ProgramSelector display directly (no cascade)
2. Rebuilds ModeSelector for the new program, highlights correct mode
3. Calls `ParameterSettings.updateParameters()` once (renders sliders for correct visualizer)
4. Applies device parameter values to the rendered sliders

This avoids the problem of intermediate re-renders wiping slider values back to defaults.

### 7.2 Audio State Sync (button 93)

`sendAudioState()`:
1. Iterates `AUDIO_PARAMS[]` array
2. Matches each param name against `PARAMETER_TABLE` via X-macro to read `cParam` values
3. Sends as `{"id":"audioState","val":"{\"parameters\":{...}}"}`

UI handler updates `AudioSettings` sliders and any matching standalone `control-slider` elements.

### 7.3 Bus State Sync (button 94)

`sendBusState()`:
1. Sends **one message per bus** (3 total) to stay within BLE MTU limits
2. Reads current bus values via `getBusParam` callback
3. Each message: `{"id":"busState","val":"{\"bus\":0,\"parameters\":{...}}"}`

UI handler parses the `bus` field and updates the corresponding `BusSettings` sliders via `syncInputs(busId, paramName, value)`.

The per-bus split is necessary because all 3 buses in a single JSON (~300 chars) plus the outer wrapper with escaped quotes exceeded the ~250-byte BLE MTU limit.

### 7.4 Manual Sync

The "Sync Viz State" button (value 92) triggers visualizer state sync only. Full sync (92+93+94) occurs automatically on BLE connect via `syncInitialState()`.

---

## 8. Remaining Gaps

### 8.1 Layer State Sync -- Not Implemented

`Layer1`..`Layer9` booleans exist in `bleControl.h` and are set via `cxLayer1`..`cxLayer9` checkboxes, but:
- No mechanism to query current layer state from device
- `LayerSelector` always initializes all layers as `true`
- Not included in preset save/load

### 8.2 Checkbox State Sync -- Partial

`applyReceivedCheckbox()` finds checkboxes by `data-my-number` attribute, but:
- Only works for `ControlCheckbox` elements, not `LayerSelector` checkboxes
- No bulk-sync mechanism exists (checkboxes are never queried from device state)
- Checkbox values aren't in presets

### 8.3 Preset Bus Parameter Gap

Presets don't capture or restore bus parameters. A mode preset (e.g., `CK6_PRESET`) will re-apply on mode change, but user-adjusted bus values are lost across preset save/load cycles.

---

## 9. Architecture Diagrams

### 9.1 Data Flow: UI -> Device (Visualizer Param)

```
[control-slider "inZoom"]
  |
  v
sendNumberCharacteristic("inZoom", 1.5)
  |
  v  (BLE write: {"id":"inZoom","val":1.5})
NumberCharacteristicCallbacks::onWrite()
  |
  v
processNumber("inZoom", 1.5)
  |-- sendReceiptNumber("inZoom", 1.5)  --> notify --> UI handleNumberCharacteristicChange
  |-- X-macro match: cZoom = 1.5
```

### 9.2 Data Flow: UI -> Device (Bus Param)

```
[bus-settings slider: busA threshold]
  |
  v
sendBusParamCharacteristic("inThreshold", 0.35, 0)
  |
  v  (BLE write: {"id":"inThreshold","val":0.35,"bus":0})
NumberCharacteristicCallbacks::onWrite()
  |
  v
processNumber("inThreshold", 0.35, busId=0)
  |-- sendReceiptNumber("inThreshold", 0.35)
  |-- setBusParam(0, "inThreshold", 0.35)
        |
        v
  handleBusParam() --> busA.threshold = 0.35
```

### 9.3 Data Flow: Full State Sync (BLE Connect)

```
syncInitialState()
  |
  |-- sendButtonCharacteristic(92) --> sendVisualizerState()
  |     --> {"id":"visualizerState","val":"{\"program\":6,\"mode\":2,\"parameters\":{...}}"}
  |     --> applyReceivedString: atomic program/mode/param update
  |
  |-- sendButtonCharacteristic(93) --> sendAudioState()
  |     --> {"id":"audioState","val":"{\"parameters\":{\"noiseGateOpen\":70,...}}"}
  |     --> applyReceivedString: update AudioSettings sliders
  |
  |-- sendButtonCharacteristic(94) --> sendBusState()
        --> {"id":"busState","val":"{\"bus\":0,\"parameters\":{...}}"} (x3, one per bus)
        --> applyReceivedString: update BusSettings sliders per bus
```

---

## 10. Button Code Reference

| Range | Purpose |
|---|---|
| 0-19 | Program selection (sets `PROGRAM`, resets `MODE` to 0) |
| 20-39 | Mode selection (value - 20 = mode index) |
| 92 | Request visualizer state sync |
| 93 | Request audio state sync |
| 94 | Request bus state sync |
| 95 | (Reserved: resetAll) |
| 98 | Display on |
| 99 | Display off |
| 101-120 | Save preset (value - 100 = preset number) |
| 121-140 | Load preset (value - 120 = preset number) |

| 160 | Trigger (fxwave2d, animartrix) |

---

## 11. File Reference

| File | Role |
|---|---|
| `index.html` | Complete web UI: styles, HTML structure, BLE transport, all Web Components |
| `src/bleControl.h` | BLE setup, characteristics, callbacks, processButton/Number/Checkbox, X-macro table, presets, state sync functions |
| `src/audio/audioTypes.h` | `Bus` struct, `BusPreset`, `AudioFrame`, bin/bus definitions |
| `src/audio/audioProcessing.h` | `handleBusParam()`, `handleGetBusParam()`, `initBus()`, audio pipeline, callback registration |

