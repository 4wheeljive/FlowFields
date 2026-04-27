Question 1: Why is it running better than expected?

Three reasons stack up:

You're not running 64×64. Your matrices are 32-ish on each side (1536 px ≈ 32×48; 3072 px ≈ 48×64). Stefan's 220 KB estimate was for 64×64 = 4096 cells. At ~1500 cells you're at ~37% of that workload per pass — and the solver is O(N) per pass, so that's directly a 2.7× speedup.

The C++ port already implements most of Zach's optimizations (see Q2 below). We didn't ship the naive Python translation; we shipped the lean version. The port avoids the ii/jj precomputes, doesn't have separate dye_*_prev arrays, and skips the +2 padding ring. So the actual memory cost is closer to Zach's 120-150 KB estimate scaled down by the smaller grid — meaning under 50 KB of new sim state on your hardware.

C++ tight loops vs Python+numpy is genuinely a big speedup. Numpy is fast per call but every operation has framework overhead. A C++ inner loop with -O3 + FL_FAST_MATH over a 32×48 grid does in microseconds what Python does in milliseconds. Plus we use fastpow instead of exp() in the splat (10-20× faster), sin_fast/sincos_fast for jet trig (LUT-based, much faster than libm), and the ESP32-S3/P4 both have hardware FPUs. The P4 in particular has dual-issue FP, which doubles solver throughput.

The Jacobi solver iterations dominate frame time, but at 3-5 iterations × 6 lin_solve calls × ~1500 cells = ~30K cell updates per frame. That's well within budget.

Question 2: Thoughts on Zach's 5 suggestions

#	Suggestion	Status in our port	If applied: tradeoff
1	Drop ii/jj (~32 KB)	Already done. We compute coords inline.	None — already free.
2	Drop full double-buffering	Partially done. No separate dye_*_prev (we reuse tR/tG/tB). We do keep uPrev/vPrev because Jacobi requires a frozen source during iteration; eliminating them means switching to Gauss-Seidel (in-place updates), which converges faster per iteration but introduces directional anisotropy (visual streaks).	Visual: small directional bias artifacts. Probably not worth ~12 KB.
3	Consolidate dye into one (H,W,3) array	Not applied. We use separate gR/gG/gB because every other emitter/flow in the project does.	No memory savings (same total bytes), project-wide refactor cost, possibly small cache wins. Skip.
4	Reuse scratch aggressively	Partially done. divergence doubles as the curl buffer in vorticity. tR/tG/tB doubles as dye_*_prev. Could merge pressure and divergence since they're used in sequence inside project().	Saves one grid (~6-12 KB). Code becomes slightly trickier; same numerics. Probably worth doing if memory tightens.
5	Remove +2 padding	Already done. We use index clamping inside the inner loops instead of a border ring.	Tiny accuracy loss at edges (visually invisible). Already free.
Float16 / uint8 dye (the "go further later" options):

Float16: ESP32-S3/P4 don't have native FP16 hardware. Emulation would be slower than float32. Skip.
uint8 dye: 4× memory savings but you lose the smooth sub-LSB fading that makes plumes look continuous. The whole reason this project uses a float grid is to enable that smoothness. Major visual regression. Skip.
Summary: We're already at ~70% of the optimization headroom Zach described, plus your smaller grid is a free 2-3× speedup. The remaining 30% (sharing pressure/divergence, Gauss-Seidel) would buy maybe 10-15 KB and a few % FPS at the cost of either visual quality or code clarity. Not urgent unless you start running into RAM or want to push grid size up.