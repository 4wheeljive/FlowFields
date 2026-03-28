# Modulation System — Analysis & Guarded Exploration Brief

## Purpose

This is NOT a refactor request.

This is an **analysis and constrained exploration task**.
The goal is to improve clarity and maintainability **without breaking or oversimplifying the existing system**.

---

## Project Context

This project implements LED visualizations (FastLED, ESP32) using:

* A shared modulation system (`timings`, `calculate_modulators()`, `move.*`)
* Multiple emitters (e.g., orbital dots, swarming dots)
* A real-time **Web BLE UI control interface**

Each adjustable parameter (e.g., orbit diameter, speed, line length) is controlled by:

* `base` value (user-controlled)
* `modRate` (controls modulation speed)
* `modLevel` (controls modulation depth)

These values can change **at runtime via UI interaction**.

---

## Critical Design Reality (Do Not Violate)

1. **Parameters are not static**

   * `base`, `modRate`, and `modLevel` can all change in real time
   * Modulation must respond immediately and correctly

2. **Modulation behavior is parameter-specific**

   * Not all parameters use the same mapping or scaling
   * Some are multiplicative, some additive, some centered, etc.

3. **Modulation may depend on the current base value**

   * The relationship between base and modulation is not uniform

4. **Existing system is intentional**

   * `timings` + `calculate_modulators()` + `move.*` is a working design
   * It provides multiple derived signals (linear, sine, directional noise, etc.)
   * This flexibility must be preserved

5. **A previous abstraction attempt failed**

   * A `ModConfig`-style wrapper was too rigid
   * It could not adapt cleanly across different parameter types

---

## Current Friction Points

* Index-based modulation (`[0]`, `[1]`, etc.) reduces readability
* Parameter intent is not explicit at call site
* Scaling and modulation math is embedded per-emitter
* Mixed usage patterns (sometimes using `move.*`, sometimes direct noise calls)
* Slot usage may become harder to manage as system grows

---

## Constraints for Any Proposed Improvements

Any suggestion MUST:

* Preserve current runtime behavior
* Preserve UI control model (`base`, `modRate`, `modLevel`)
* Preserve parameter-specific modulation flexibility
* Avoid forcing a single modulation pattern across all parameters
* Avoid heavy abstraction layers or over-generalization
* Avoid dynamic allocation or expensive constructs (ESP32 constraints)
* Be incrementally adoptable (one emitter/flowField at a time)

---

## What to Avoid

Do NOT:

* Propose a full rewrite
* Replace the modulation engine
* Introduce a rigid “one-size-fits-all” modulator class
* Assume all parameters can share the same modulation semantics
* Hide important math behind abstractions that reduce control

---

## Requested Tasks

### 1. Analyze the Current System

Based on the codebase:

* Identify how responsibilities are currently divided between:

  * modulation engine (`timings`, `move.*`)
  * emitters/flowFields
  * parameter structures

* Distinguish:

  * essential complexity (must remain)
  * accidental complexity (could be improved)

---

### 2. Identify Low-Risk Improvements

Suggest **small, safe improvements** that:

* Improve readability of emitter code
* Reduce reliance on implicit index meaning
* Improve consistency of modulation usage

Examples (not prescriptive):

* naming patterns
* light helper functions
* clearer mapping of slot → parameter meaning

---

### 3. Explore Minimal Abstractions (Only If Justified)

If (and only if) appropriate, propose:

* very thin wrappers or helpers that:

  * do not reduce flexibility
  * do not enforce uniform behavior, but minimize having to "reinvent the wheel" every time modulation is added to a new paramter 
  * make parameter intent clearer

These must:

* integrate cleanly with `timings` and `move.*`
* allow per-parameter customization
* avoid repeating the failure of rigid `ModConfig`-style designs

---

### 4. Apply to One Concrete Example

Refactor or rewrite **ONE emitter only** (e.g., `emitOrbitalDots`) to demonstrate:

* improved clarity
* preserved behavior
* no loss of flexibility

Explain:

* what changed
* why it is safer/better
* what tradeoffs remain

---

### 5. Explicitly Call Out Tradeoffs

For every suggestion:

* what improves
* what becomes more complex
* what risks are introduced

---

## Success Criteria

A good result will:

* Respect the existing system rather than replace it
* Improve readability without hiding control
* Keep modulation behavior fully expressive
* Be practical for an embedded system
* Feel like a natural evolution, not a redesign

---

## Reminder

This system already works.

The goal is not to make it “cleaner” in the abstract,
but to make it **easier to reason about without losing power**.
