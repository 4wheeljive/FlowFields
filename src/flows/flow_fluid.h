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
#include "modulators.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct FluidParams {
        float viscosity           = 0.0005f;     // velocity diffusion coefficient
        float diffusion           = 0.0005f;     // dye diffusion coefficient
        float velocityDissipation = 0.75f;     // per-second velocity decay (0..1, 1=no decay)
        float dyeDissipation      = 0.25f;     // per-second dye decay (overrides project persistence)
        float vorticity           = 7.0f;     // confinement strength (0 = disabled)
        float gravity             = 0.3f;     // uniform vertical force on v
        uint8_t solverIterations  = 5;        // Jacobi passes per lin_solve

        ModConfig modVelDissip = {0, 0.5f, 0.0f};   // modTimer, modRate, modLevel
        ModConfig modDyeDissip = {1, 0.5f, 0.0f};
    };

    FluidParams fluid;

    // Working values prepared each frame by fluidPrepare()
    static float workVelDissip = 0.5f;
    static float workDyeDissip = 0.5f;

    // Persistent simulation state — dynamically allocated by allocFluidGrids(w,h)
    static float** u;
    static float** v;
    static float** uPrev;
    static float** vPrev;
    static float** pressure;
    static float** divergence;

    static void allocFluidGrids(int w, int h) {
        u         = allocGrid(w, h);
        v         = allocGrid(w, h);
        uPrev     = allocGrid(w, h);
        vPrev     = allocGrid(w, h);
        pressure  = allocGrid(w, h);
        divergence= allocGrid(w, h);
    }

    static void freeFluidGrids() {
        freeGrid(u);     freeGrid(v);         u  = v  = nullptr;
        freeGrid(uPrev); freeGrid(vPrev);     uPrev = vPrev = nullptr;
        freeGrid(pressure); freeGrid(divergence); pressure = divergence = nullptr;
    }

    // ───────────────────────────────────────────────────────────────
    //  Boundary conditions
    //    b == 0: scalar (dye, pressure) — no enforcement (relies on clamp in samplers)
    //    b == 1: u-velocity — zero at left/right walls (no penetration)
    //    b == 2: v-velocity — zero at top/bottom walls (no penetration)
    // ───────────────────────────────────────────────────────────────
    static void setBnd(int b, float** x) {
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
    static inline float sampleClamped(float** x, int yi, int xi) {
        if (yi < 0) yi = 0; else if (yi >= HEIGHT) yi = HEIGHT - 1;
        if (xi < 0) xi = 0; else if (xi >= WIDTH)  xi = WIDTH  - 1;
        return x[yi][xi];
    }

    // ───────────────────────────────────────────────────────────────
    //  Linear solver (Jacobi iteration)
    //    x[i,j] = (x0[i,j] + a*(x[i-1,j] + x[i+1,j] + x[i,j-1] + x[i,j+1])) / c
    // ───────────────────────────────────────────────────────────────
    static void linSolve(int b, float** x, float** x0, float a, float c, int iter) {
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

    static void diffuse(int b, float** x, float** x0, float diff, float dt_) {
        if (diff <= 0.0f) {
            memcpy(x[0], x0[0], (size_t)WIDTH * HEIGHT * sizeof(float));
            setBnd(b, x);
            return;
        }
        const float SIM_SIZE = (float)fl::min(WIDTH, HEIGHT);
        const float a = dt_ * diff * SIM_SIZE * SIM_SIZE;
        linSolve(b, x, x0, a, 1.0f + 4.0f * a, fluid.solverIterations);
    }

    // Semi-Lagrangian advection: backtrace each cell along velocity field, bilinearly sample source
    static void advectField(int b, float** d, float** d0, float** velU, float** velV, float dt_) {
        const float SIM_SIZE = (float)fl::min(WIDTH, HEIGHT);
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
        const float SIM_SIZE = (float)fl::min(WIDTH, HEIGHT);
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
        const ModConfig& velMod = fluid.modVelDissip;
        const ModConfig& dyeMod = fluid.modDyeDissip;

        // 1) Plumbing
        timings.ratio[velMod.modTimer] = 0.0004f  * velMod.modRate;
        timings.ratio[dyeMod.modTimer] = 0.00045f * dyeMod.modRate;
        calculate_modulators(timings, 2);

        // 2) Signal acquisition: bipolar [-1, 1]
        const float velSignal = move.directional_noise[velMod.modTimer];
        const float dySignal  = move.directional_noise[dyeMod.modTimer];

        // 3) Artistic application: orbitalDots-style bipolar modulation,
        //    clamped to [0.01, 1.0] to keep dissipation values stable.
        workVelDissip = fluid.velocityDissipation *
            ((1.0f - velMod.modLevel) + velMod.modLevel * velSignal);
        workVelDissip = fmaxf(0.01f, fminf(1.0f, workVelDissip));

        workDyeDissip = fluid.dyeDissipation *
            ((1.0f - dyeMod.modLevel) + dyeMod.modLevel * dySignal);
        workDyeDissip = fmaxf(0.01f, fminf(1.0f, workDyeDissip));
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
        const size_t gridBytes = (size_t)WIDTH * HEIGHT * sizeof(float);
        memcpy(uPrev[0], u[0], gridBytes);
        memcpy(vPrev[0], v[0], gridBytes);
        diffuse(1, u, uPrev, fluid.viscosity, dt);
        diffuse(2, v, vPrev, fluid.viscosity, dt);
        project();

        memcpy(uPrev[0], u[0], gridBytes);
        memcpy(vPrev[0], v[0], gridBytes);
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
            memcpy(tR[0], gR[0], gridBytes);
            memcpy(tG[0], gG[0], gridBytes);
            memcpy(tB[0], gB[0], gridBytes);
            diffuse(0, gR, tR, fluid.diffusion, dt);
            diffuse(0, gG, tG, fluid.diffusion, dt);
            diffuse(0, gB, tB, fluid.diffusion, dt);
        }

        memcpy(tR[0], gR[0], gridBytes);
        memcpy(tG[0], gG[0], gridBytes);
        memcpy(tB[0], gB[0], gridBytes);
        advectField(0, gR, tR, u, v, dt);
        advectField(0, gG, tG, u, v, dt);
        advectField(0, gB, tB, u, v, dt);

        // ─── DISSIPATION ───────────────────────────────────────────
        const float fadeVel = fl::powf(workVelDissip, dt);
        const float fadeDye = fl::powf(workDyeDissip, dt);
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
