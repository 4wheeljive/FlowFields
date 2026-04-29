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

#include "FlowFieldsEngine.h"

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

    // Persistent simulation state: velocity field + scratch buffers.
    // All are float** (lazily allocated on first fluidPrepare() call).
    // u[y][x] and v[y][x] are also accessed by emitter_fluidJet.h.
    static float** u         = nullptr;
    static float** v         = nullptr;
    static float** uPrev     = nullptr;
    static float** vPrev     = nullptr;
    static float** pressure  = nullptr;
    static float** divergence = nullptr;
    static bool    fluidAllocated = false;


    // ───────────────────────────────────────────────────────────────
    //  Internal allocation helper
    // ───────────────────────────────────────────────────────────────
    static float** allocFluidGrid() {
        float** g = new float*[g_engine->_height];
        for (int i = 0; i < g_engine->_height; i++) {
            g[i] = new float[g_engine->_width]();   // zero-initialised
        }
        return g;
    }

    static void copyGrid2D(float** dst, float** src) {
        for (int y = 0; y < g_engine->_height; y++)
            memcpy(dst[y], src[y], g_engine->_width * sizeof(float));
    }

    // ───────────────────────────────────────────────────────────────
    //  Boundary conditions
    //    b == 0: scalar (dye, pressure) — no enforcement
    //    b == 1: u-velocity — zero at left/right walls
    //    b == 2: v-velocity — zero at top/bottom walls
    // ───────────────────────────────────────────────────────────────
    static void setBnd(int b, float** x) {
        if (b == 1) {
            for (int y = 0; y < g_engine->_height; y++) {
                x[y][0]         = 0.0f;
                x[y][g_engine->_width - 1] = 0.0f;
            }
        } else if (b == 2) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                x[0][xc]          = 0.0f;
                x[g_engine->_height - 1][xc] = 0.0f;
            }
        }
    }

    // Sample with edge clamping
    static inline float sampleClamped(float** x, int yi, int xi) {
        if (yi < 0) yi = 0; else if (yi >= g_engine->_height) yi = g_engine->_height - 1;
        if (xi < 0) xi = 0; else if (xi >= g_engine->_width)  xi = g_engine->_width  - 1;
        return x[yi][xi];
    }

    // ───────────────────────────────────────────────────────────────
    //  Linear solver (Jacobi iteration)
    // ───────────────────────────────────────────────────────────────
    static void linSolve(int b, float** x, float** x0, float a, float c, int iter) {
        const float invC = 1.0f / c;
        for (int k = 0; k < iter; k++) {
            for (int y = 0; y < g_engine->_height; y++) {
                for (int xc = 0; xc < g_engine->_width; xc++) {
                    float xm = (xc > 0)         ? x[y][xc - 1] : x[y][xc];
                    float xp = (xc < g_engine->_width - 1) ? x[y][xc + 1] : x[y][xc];
                    float ym = (y  > 0)          ? x[y - 1][xc] : x[y][xc];
                    float yp = (y  < g_engine->_height - 1) ? x[y + 1][xc] : x[y][xc];
                    x[y][xc] = (x0[y][xc] + a * (xm + xp + ym + yp)) * invC;
                }
            }
            setBnd(b, x);
        }
    }

    static void diffuse(int b, float** x, float** x0, float diff, float dt_) {
        if (diff <= 0.0f) {
            copyGrid2D(x, x0);
            setBnd(b, x);
            return;
        }
        const float SIM_SIZE = (float)g_engine->_minDim;
        const float a = dt_ * diff * SIM_SIZE * SIM_SIZE;
        linSolve(b, x, x0, a, 1.0f + 4.0f * a, fluid.solverIterations);
    }

    // Semi-Lagrangian advection: backtrace each cell along velocity field
    static void advectField(int b, float** d, float** d0,
                            float** velU, float** velV, float dt_) {
        const float SIM_SIZE = (float)g_engine->_minDim;
        const float dt0 = dt_ * SIM_SIZE;
        const float maxX = (float)g_engine->_width  - 1.5f;
        const float maxY = (float)g_engine->_height - 1.5f;

        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                float sx = (float)xc - dt0 * velU[y][xc];
                float sy = (float)y  - dt0 * velV[y][xc];
                sx = clampf(sx, 0.5f, maxX);
                sy = clampf(sy, 0.5f, maxY);

                int   ix0 = (int)fl::floorf(sx);
                int   iy0 = (int)fl::floorf(sy);
                int   ix1 = ix0 + 1;
                int   iy1 = iy0 + 1;
                if (ix1 >= g_engine->_width)  ix1 = g_engine->_width  - 1;
                if (iy1 >= g_engine->_height) iy1 = g_engine->_height - 1;

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
        const float SIM_SIZE = (float)g_engine->_minDim;
        const float h = 1.0f / SIM_SIZE;

        // 1. Compute divergence of velocity field
        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                float uxp = (xc < g_engine->_width  - 1) ? u[y][xc + 1] : u[y][xc];
                float uxm = (xc > 0)          ? u[y][xc - 1] : u[y][xc];
                float vyp = (y  < g_engine->_height - 1) ? v[y + 1][xc] : v[y][xc];
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
        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                float pxp = (xc < g_engine->_width  - 1) ? pressure[y][xc + 1] : pressure[y][xc];
                float pxm = (xc > 0)          ? pressure[y][xc - 1] : pressure[y][xc];
                float pyp = (y  < g_engine->_height - 1) ? pressure[y + 1][xc] : pressure[y][xc];
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
        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                float vxp = (xc < g_engine->_width  - 1) ? v[y][xc + 1] : v[y][xc];
                float vxm = (xc > 0)          ? v[y][xc - 1] : v[y][xc];
                float uyp = (y  < g_engine->_height - 1) ? u[y + 1][xc] : u[y][xc];
                float uym = (y  > 0)          ? u[y - 1][xc] : u[y][xc];
                divergence[y][xc] = 0.5f * (vxp - vxm - (uyp - uym));
            }
        }

        // 2. Compute gradient of |curl|, normalize, apply force
        const float strength = fluid.vorticity;
        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
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
        if (xc < 0 || xc >= g_engine->_width || yc < 0 || yc >= g_engine->_height) return;
        u[yc][xc] += du;
        v[yc][xc] += dv;
    }

    // ───────────────────────────────────────────────────────────────
    //  Pipeline entry points
    // ───────────────────────────────────────────────────────────────
    static void fluidPrepare() {
        // Lazy-allocate internal simulation arrays on first call
        if (!fluidAllocated) {
            u         = allocFluidGrid();
            v         = allocFluidGrid();
            uPrev     = allocFluidGrid();
            vPrev     = allocFluidGrid();
            pressure  = allocFluidGrid();
            divergence = allocFluidGrid();
            fluidAllocated = true;
        }

        const ModConfig& velMod = fluid.modVelDissip;
        const ModConfig& dyeMod = fluid.modDyeDissip;

        // 1) Plumbing
        g_engine->timings.ratio[velMod.modTimer] = 0.0004f  * velMod.modRate;
        g_engine->timings.ratio[dyeMod.modTimer] = 0.00045f * dyeMod.modRate;
        g_engine->calculate_modulators(2);

        // 2) Signal acquisition: bipolar [-1, 1]
        const float velSignal = g_engine->move.directional_noise[velMod.modTimer];
        const float dySignal  = g_engine->move.directional_noise[dyeMod.modTimer];

        // 3) Artistic application
        workVelDissip = fluid.velocityDissipation *
            ((1.0f - velMod.modLevel) + velMod.modLevel * velSignal);
        workVelDissip = fmaxf(0.01f, fminf(1.0f, workVelDissip));

        workDyeDissip = fluid.dyeDissipation *
            ((1.0f - dyeMod.modLevel) + dyeMod.modLevel * dySignal);
        workDyeDissip = fmaxf(0.01f, fminf(1.0f, workDyeDissip));
    }

    static void fluidAdvect() {
        const float dt_ = g_engine->dt;

        // Apply gravity (uniform vertical force)
        if (fluid.gravity != 0.0f) {
            const float dvg = fluid.gravity * dt_;
            for (int y = 0; y < g_engine->_height; y++) {
                for (int xc = 0; xc < g_engine->_width; xc++) {
                    v[y][xc] += dvg;
                }
            }
        }

        // ─── VELOCITY STEP ─────────────────────────────────────────
        copyGrid2D(uPrev, u);
        copyGrid2D(vPrev, v);
        diffuse(1, u, uPrev, fluid.viscosity, dt_);
        diffuse(2, v, vPrev, fluid.viscosity, dt_);
        project();

        copyGrid2D(uPrev, u);
        copyGrid2D(vPrev, v);
        advectField(1, u, uPrev, uPrev, vPrev, dt_);
        advectField(2, v, vPrev, uPrev, vPrev, dt_);
        project();

        if (fluid.vorticity > 0.0f) {
            applyVorticityConfinement(dt_);
            project();
        }

        // ─── DYE STEP ──────────────────────────────────────────────
        // For each channel: optionally diffuse, then advect through u,v.
        // Use tR/tG/tB as the previous-frame buffer.
        if (fluid.diffusion > 0.0f) {
            copyGrid2D(g_engine->tR, g_engine->gR);
            copyGrid2D(g_engine->tG, g_engine->gG);
            copyGrid2D(g_engine->tB, g_engine->gB);
            diffuse(0, g_engine->gR, g_engine->tR, fluid.diffusion, dt_);
            diffuse(0, g_engine->gG, g_engine->tG, fluid.diffusion, dt_);
            diffuse(0, g_engine->gB, g_engine->tB, fluid.diffusion, dt_);
        }

        copyGrid2D(g_engine->tR, g_engine->gR);
        copyGrid2D(g_engine->tG, g_engine->gG);
        copyGrid2D(g_engine->tB, g_engine->gB);
        advectField(0, g_engine->gR, g_engine->tR, u, v, dt_);
        advectField(0, g_engine->gG, g_engine->tG, u, v, dt_);
        advectField(0, g_engine->gB, g_engine->tB, u, v, dt_);

        // ─── DISSIPATION ───────────────────────────────────────────
        const float fadeVel = fl::powf(workVelDissip, dt_);
        const float fadeDye = fl::powf(workDyeDissip, dt_);
        for (int y = 0; y < g_engine->_height; y++) {
            for (int xc = 0; xc < g_engine->_width; xc++) {
                u[y][xc]  *= fadeVel;
                v[y][xc]  *= fadeVel;
                g_engine->gR[y][xc] *= fadeDye;
                g_engine->gG[y][xc] *= fadeDye;
                g_engine->gB[y][xc] *= fadeDye;
            }
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields
