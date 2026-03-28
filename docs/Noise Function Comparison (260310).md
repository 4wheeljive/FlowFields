Head-to-Head Summary

┌───────────────────────────┬────────────────────┬──────────────────────┬─────────────────────┬───────────────────┐
│                           │ Animartrix pnoise  │   Ported Perlin2D    │   FastLED inoise8   │ FastLED inoise16  │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Max dimensions            │ 3D                 │ 2D                   │ 3D                  │ 4D                │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Arithmetic type           │ Float              │ Float                │ Integer             │ Integer           │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Output type               │ float (−1..+1)     │ float (−1..+1)       │ uint8 (0–255)       │ uint16 (0–65535)  │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Output resolution         │ ~23-bit mantissa   │ ~23-bit mantissa     │ 8-bit               │ 16-bit            │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Fade curve                │ C² smootherstep    │ C² smootherstep      │ Quadratic (C⁰)      │ Quadratic (C⁰)    │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Gradient directions       │ 12 (3D edges)      │ 8 (2D axis+diag)     │ 12/8 (3D/2D)        │ 12/8 (3D/2D)      │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Permutation table         │ 256 B (shared)     │ 512 B (per instance) │ 256 B (PROGMEM)     │ 256 B (PROGMEM)   │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Seedable                  │ No (fixed table)   │ Yes (per instance)   │ No (fixed table)    │ No (fixed table)  │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Float ops per 3D call     │ ~30                │ N/A (2D only)        │ 0                   │ 0                 │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Float ops per 2D call     │ ~30 (z=0)          │ ~25–30               │ 0                   │ 0                 │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Integer ops per 2D call   │ ~8 (indexing only) │ ~6 (indexing only)   │ ~40–50              │ ~60–80            │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Relative cost (2D, ESP32) │ Highest            │ High                 │ Very low            │ Low               │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Relative cost (2D, AVR)   │ Prohibitive        │ Prohibitive          │ Very low            │ Low               │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Visual smoothness         │ Best               │ Best                 │ Slightly chunky     │ Good              │
├───────────────────────────┼────────────────────┼──────────────────────┼─────────────────────┼───────────────────┤
│ Grid-edge artifacts       │ None               │ None                 │ Visible at low freq │ Minor at low freq │
└───────────────────────────┴────────────────────┴──────────────────────┴─────────────────────┴───────────────────┘

Key Takeaways

1. pnoise vs ported Perlin2D — same math family, different scope. Both use float smootherstep and produce the highest-quality output. pnoise carries the overhead of a 3D evaluation (8 gradients, 7 lerps) even when you only need 2D by zeroing z. The ported Perlin2D does exactly 2D work (4 gradients, 3 lerps) — roughly 30% cheaper per call for the same visual quality. It also supports per-instance seeding, which pnoise does not.

2. The ported Perlin2D is the best fit for colorTrails. It matches the Python prototype exactly, costs ~25–30 float ops per call (vs ~10 for the current 1D), and the total noise budget stays under 2,000 float ops/frame — still dwarfed by advection (~20,000 ops). The upgrade from 1D to 2D is visually significant (morphing profiles vs. sliding waveforms) at negligible CPU cost on ESP32.

3. For per-pixel noise fields, use FastLED integer noise. If you ever need noise evaluated at every pixel (not just per-row/column profiles), inoise8/inoise16 are the only viable options on a microcontroller. On a 32×32 grid that's 1,024 calls/frame — inoise8 handles that in ~100 µs on ESP32, while a float Perlin would take ~1–2 ms. At 16×16 the gap is smaller but still 5–10×.

4. inoise16 is the best general-purpose embedded noise. 16-bit output (65,536 levels) eliminates visible quantization for LED work, it runs entirely in integer math, and it uniquely offers a 4D variant for time-evolving 3D noise fields. Its only weakness is the quadratic fade curve producing slightly harsher grid transitions than the smootherstep used by pnoise/Perlin2D.