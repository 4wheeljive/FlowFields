#pragma once

// ═══════════════════════════════════════════════════════════════════
//  FLUID (NAVIER-STOKES) FLOW FIELD — flow_fluid.h
// ═══════════════════════════════════════════════════════════════════
//
//  Stable-fluids simulation (Jos Stam, 1999).  Maintains a persistent
//  velocity field (u, v) that advects RGB dye.  Each frame:
//    - velocity diffuse → project → self-advect → project
//    - optional vorticity confinement → project
//    - dye diffuse + advect through final velocity field
//    - per-frame dissipation
//
//  This flow expects pairing with emitter_fluidJet, which writes to
//  both gR/gG/gB (dye) and u/v (velocity).
//
//  Ported from colorTrailsOrig/navier_stokes_1.py.

#include "flowFieldsTypes.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct FluidParams {
        float viscosity           = 0.0f;     // velocity diffusion coefficient
        float diffusion           = 0.0f;     // dye diffusion coefficient
        float velocityDissipation = 0.5f;     // per-second velocity decay (0..1, 1=no decay)
        float dyeDissipation      = 0.5f;     // per-second dye decay (overrides project persistence)
        float vorticity           = 0.0f;     // confinement strength (0 = disabled)
        float gravity             = 0.0f;     // uniform vertical force on v
        uint8_t solverIterations  = 3;        // Jacobi passes per lin_solve
    };

    FluidParams fluid;

    // Persistent simulation state (survives across frames)
    static float u[HEIGHT][WIDTH], v[HEIGHT][WIDTH];
    static float uPrev[HEIGHT][WIDTH], vPrev[HEIGHT][WIDTH];
    static float pressure[HEIGHT][WIDTH], divergence[HEIGHT][WIDTH];

    // Internal "size" parameter for the solver (scales velocity-to-cells conversion).
    // Stam's algorithm assumes a square grid; we pick a single representative size.
    static constexpr float SIM_SIZE = (float)MIN_DIMENSION;

    // ───────────────────────────────────────────────────────────────
    //  Boundary conditions
    //    b == 0: scalar (dye, pressure) — no enforcement (relies on clamp in samplers)
    //    b == 1: u-velocity — zero at left/right walls (no penetration)
    //    b == 2: v-velocity — zero at top/bottom walls (no penetration)
    // ───────────────────────────────────────────────────────────────
    static void setBnd(int b, float (*x)[WIDTH]) {
        if (b == 1) {
            for (int y = 0; y < HEIGHT; y++) {
                x[y][0]         = 0.0f;
                x[y][WIDTH - 1] = 0.0f;
            }
        } else if (b == 2) {
            for (int xc = 0; xc < WIDTH; xc++) {
                x[0][xc]          = 0.0f;
                x[HEIGHT - 1][xc] = 0.0f;
            }
        }
    }

    // Sample with edge clamping (mirror behavior of Python's set_bnd for scalars)
    static inline float sampleClamped(float (*x)[WIDTH], int yi, int xi) {
        if (yi < 0) yi = 0; else if (yi >= HEIGHT) yi = HEIGHT - 1;
        if (xi < 0) xi = 0; else if (xi >= WIDTH)  xi = WIDTH  - 1;
        return x[yi][xi];
    }

    // ───────────────────────────────────────────────────────────────
    //  Linear solver (Jacobi iteration)
    //    x[i,j] = (x0[i,j] + a*(x[i-1,j] + x[i+1,j] + x[i,j-1] + x[i,j+1])) / c
    // ───────────────────────────────────────────────────────────────
    static void linSolve(int b, float (*x)[WIDTH], float (*x0)[WIDTH], float a, float c, int iter) {
        const float invC = 1.0f / c;
        for (int k = 0; k < iter; k++) {
            for (int y = 0; y < HEIGHT; y++) {
                for (int xc = 0; xc < WIDTH; xc++) {
                    float xm = (xc > 0)         ? x[y][xc - 1] : x[y][xc];
                    float xp = (xc < WIDTH - 1) ? x[y][xc + 1] : x[y][xc];
                    float ym = (y  > 0)          ? x[y - 1][xc] : x[y][xc];
                    float yp = (y  < HEIGHT - 1) ? x[y + 1][xc] : x[y][xc];
                    x[y][xc] = (x0[y][xc] + a * (xm + xp + ym + yp)) * invC;
                }
            }
            setBnd(b, x);
        }
    }

    static void diffuse(int b, float (*x)[WIDTH], float (*x0)[WIDTH], float diff, float dt_) {
        if (diff <= 0.0f) {
            // No diffusion: result is just x0
            memcpy(x, x0, sizeof(float) * HEIGHT * WIDTH);
            setBnd(b, x);
            return;
        }
        const float a = dt_ * diff * SIM_SIZE * SIM_SIZE;
        linSolve(b, x, x0, a, 1.0f + 4.0f * a, fluid.solverIterations);
    }

    // Semi-Lagrangian advection: backtrace each cell along velocity field, bilinearly sample source
    static void advectField(int b, float (*d)[WIDTH], float (*d0)[WIDTH],
                            float (*velU)[WIDTH], float (*velV)[WIDTH], float dt_) {
        const float dt0 = dt_ * SIM_SIZE;
        const float maxX = (float)WIDTH  - 1.5f;
        const float maxY = (float)HEIGHT - 1.5f;

        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                float sx = (float)xc - dt0 * velU[y][xc];
                float sy = (float)y  - dt0 * velV[y][xc];
                sx = clampf(sx, 0.5f, maxX);
                sy = clampf(sy, 0.5f, maxY);

                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = ix0 + 1;
                int   iy1 = iy0 + 1;
                if (ix1 >= WIDTH)  ix1 = WIDTH  - 1;
                if (iy1 >= HEIGHT) iy1 = HEIGHT - 1;

                float fx = sx - ix0;
                float fy = sy - iy0;

                float top = d0[iy0][ix0] * (1.0f - fx) + d0[iy0][ix1] * fx;
                float bot = d0[iy1][ix0] * (1.0f - fx) + d0[iy1][ix1] * fx;
                d[y][xc] = top * (1.0f - fy) + bot * fy;
            }
        }
        setBnd(b, d);
    }

    // Hodge projection: subtract pressure gradient to make velocity divergence-free
    static void project() {
        const float h = 1.0f / SIM_SIZE;

        // 1. Compute divergence of velocity field
        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                float uxp = (xc < WIDTH  - 1) ? u[y][xc + 1] : u[y][xc];
                float uxm = (xc > 0)          ? u[y][xc - 1] : u[y][xc];
                float vyp = (y  < HEIGHT - 1) ? v[y + 1][xc] : v[y][xc];
                float vym = (y  > 0)          ? v[y - 1][xc] : v[y][xc];
                divergence[y][xc] = -0.5f * h * (uxp - uxm + vyp - vym);
                pressure[y][xc]   = 0.0f;
            }
        }
        setBnd(0, divergence);
        setBnd(0, pressure);

        // 2. Solve Poisson equation for pressure
        linSolve(0, pressure, divergence, 1.0f, 4.0f, fluid.solverIterations);

        // 3. Subtract pressure gradient from velocity
        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                float pxp = (xc < WIDTH  - 1) ? pressure[y][xc + 1] : pressure[y][xc];
                float pxm = (xc > 0)          ? pressure[y][xc - 1] : pressure[y][xc];
                float pyp = (y  < HEIGHT - 1) ? pressure[y + 1][xc] : pressure[y][xc];
                float pym = (y  > 0)          ? pressure[y - 1][xc] : pressure[y][xc];
                u[y][xc] -= 0.5f * (pxp - pxm) * SIM_SIZE;
                v[y][xc] -= 0.5f * (pyp - pym) * SIM_SIZE;
            }
        }
        setBnd(1, u);
        setBnd(2, v);
    }

    // Vorticity confinement: re-add small swirls lost to numerical diffusion.
    // Uses `divergence` as scratch for the curl field.
    static void applyVorticityConfinement(float dt_) {
        // 1. Compute curl (scalar 2D vorticity) into divergence buffer
        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                float vxp = (xc < WIDTH  - 1) ? v[y][xc + 1] : v[y][xc];
                float vxm = (xc > 0)          ? v[y][xc - 1] : v[y][xc];
                float uyp = (y  < HEIGHT - 1) ? u[y + 1][xc] : u[y][xc];
                float uym = (y  > 0)          ? u[y - 1][xc] : u[y][xc];
                divergence[y][xc] = 0.5f * (vxp - vxm - (uyp - uym));
            }
        }

        // 2. Compute gradient of |curl|, normalize, apply force
        const float strength = fluid.vorticity;
        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                float gxp = fl::fabsf(sampleClamped(divergence, y,     xc + 1));
                float gxm = fl::fabsf(sampleClamped(divergence, y,     xc - 1));
                float gyp = fl::fabsf(sampleClamped(divergence, y + 1, xc));
                float gym = fl::fabsf(sampleClamped(divergence, y - 1, xc));
                float gx = 0.5f * (gxp - gxm);
                float gy = 0.5f * (gyp - gym);
                float len = fl::sqrtf(gx * gx + gy * gy) + 1e-5f;
                gx /= len;
                gy /= len;
                float w = divergence[y][xc];
                u[y][xc] += dt_ * strength *  gy * w;
                v[y][xc] += dt_ * strength * -gx * w;
            }
        }
        setBnd(1, u);
        setBnd(2, v);
    }

    // ───────────────────────────────────────────────────────────────
    //  Public emitter-side hooks
    // ───────────────────────────────────────────────────────────────
    static inline void fluidAddVelocity(int xc, int yc, float du, float dv) {
        if (xc < 0 || xc >= WIDTH || yc < 0 || yc >= HEIGHT) return;
        u[yc][xc] += du;
        v[yc][xc] += dv;
    }

    // ───────────────────────────────────────────────────────────────
    //  Pipeline entry points
    // ───────────────────────────────────────────────────────────────
    static void fluidPrepare() {
        // No precomputation needed — sim state evolves in advect step.
    }

    static void fluidAdvect() {
        // Apply gravity (uniform vertical force)
        if (fluid.gravity != 0.0f) {
            const float dvg = fluid.gravity * dt;
            for (int y = 0; y < HEIGHT; y++) {
                for (int xc = 0; xc < WIDTH; xc++) {
                    v[y][xc] += dvg;
                }
            }
        }

        // ─── VELOCITY STEP ─────────────────────────────────────────
        memcpy(uPrev, u, sizeof(u));
        memcpy(vPrev, v, sizeof(v));
        diffuse(1, u, uPrev, fluid.viscosity, dt);
        diffuse(2, v, vPrev, fluid.viscosity, dt);
        project();

        memcpy(uPrev, u, sizeof(u));
        memcpy(vPrev, v, sizeof(v));
        advectField(1, u, uPrev, uPrev, vPrev, dt);
        advectField(2, v, vPrev, uPrev, vPrev, dt);
        project();

        if (fluid.vorticity > 0.0f) {
            applyVorticityConfinement(dt);
            project();
        }

        // ─── DYE STEP ──────────────────────────────────────────────
        // For each channel: optionally diffuse, then advect through u,v.
        // Use tR/tG/tB as the previous-frame buffer.
        if (fluid.diffusion > 0.0f) {
            memcpy(tR, gR, sizeof(gR));
            memcpy(tG, gG, sizeof(gG));
            memcpy(tB, gB, sizeof(gB));
            diffuse(0, gR, tR, fluid.diffusion, dt);
            diffuse(0, gG, tG, fluid.diffusion, dt);
            diffuse(0, gB, tB, fluid.diffusion, dt);
        }

        memcpy(tR, gR, sizeof(gR));
        memcpy(tG, gG, sizeof(gG));
        memcpy(tB, gB, sizeof(gB));
        advectField(0, gR, tR, u, v, dt);
        advectField(0, gG, tG, u, v, dt);
        advectField(0, gB, tB, u, v, dt);

        // ─── DISSIPATION ───────────────────────────────────────────
        const float fadeVel = fl::powf(fluid.velocityDissipation, dt);
        const float fadeDye = fl::powf(fluid.dyeDissipation,      dt);
        for (int y = 0; y < HEIGHT; y++) {
            for (int xc = 0; xc < WIDTH; xc++) {
                u[y][xc]  *= fadeVel;
                v[y][xc]  *= fadeVel;
                gR[y][xc] *= fadeDye;
                gG[y][xc] *= fadeDye;
                gB[y][xc] *= fadeDye;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
