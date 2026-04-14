#pragma once

//========================================================================================
// cube based originally on AI-generated code shared by Fluffy-Wishbone-3497 here: 
// https://www.reddit.com/r/FastLED/comments/1nvuzjg/claude_does_like_to_code_fastled/
//========================================================================================

#include "flowFieldsTypes.h"
#include "modulators.h"

namespace flowFields {
    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN

    struct CubeParams {
        float scale = 1.0f;
        float rotateSpeed[3] = {0.02f, 0.03f, 0.01f};  // X, Y, Z rotation rates
        bool axisFreeze[3] = {false, false, false};   // per-axis freeze toggles

        ModConfig modScale        = {0, 0.5f, 0.0f};  // modTimer, modRate, modLevel
        ModConfig modRotateSpeedX = {1, 0.5f, 0.0f};
        ModConfig modRotateSpeedY = {2, 0.5f, 0.0f};
        ModConfig modRotateSpeedZ = {3, 0.5f, 0.0f};
    };

    CubeParams cube;

    // Runtime working values prepared each frame by cubePrepare()
    static float workScale = 1.0f;
    static float workRotateSpeed[3] = {0.0f, 0.0f, 0.0f};

    // Cube vertices in 3D space
    float vertices[8][3] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}
    };
    
    // Cube edges (pairs of vertex indices)
    int edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  // Back face
    {4, 5}, {5, 6}, {6, 7}, {7, 4},  // Front face
    {0, 4}, {1, 5}, {2, 6}, {3, 7}   // Connecting edges
    };
    
    float rotated[8][3];
    float projected[8][2];
    
    float angleX = 0;
    float angleY = 0;
    float angleZ = 0;

    // Anti-aliased line with fixed color, drawing onto the float grid.
    // Same oversampled bilinear approach as drawAASubpixelLine but with constant color.
    static void drawFixedColorLine(float x0, float y0, float x1, float y1,
                                    float cr, float cg, float cb) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float maxd = fl::fabsf(dx) > fl::fabsf(dy) ? fl::fabsf(dx) : fl::fabsf(dy);
        int steps = max(1, (int)(maxd)); // * 3.0f    multiply maxd to make edges thicker 
        for (int i = 0; i <= steps; i++) {
            float u  = (float)i / (float)steps;
            float x  = x0 + dx * u;
            float y  = y0 + dy * u;
            int   xi = (int)fl::floorf(x);
            int   yi = (int)fl::floorf(y);
            float fx = x - xi;
            float fy = y - yi;
            blendPixelWeighted(xi,     yi,     cr, cg, cb, (1.0f - fx) * (1.0f - fy));
            blendPixelWeighted(xi + 1, yi,     cr, cg, cb, fx * (1.0f - fy));
            blendPixelWeighted(xi,     yi + 1, cr, cg, cb, (1.0f - fx) * fy);
            blendPixelWeighted(xi + 1, yi + 1, cr, cg, cb, fx * fy);
        }
    }

    // 3D rotation and projection =====================================

    void rotateCube() {
        float cosX = cos(angleX), sinX = sin(angleX);
        float cosY = cos(angleY), sinY = sin(angleY);
        float cosZ = cos(angleZ), sinZ = sin(angleZ);
    
        for (int i = 0; i < 8; i++) {
            float x = vertices[i][0];
            float y = vertices[i][1];
            float z = vertices[i][2];
        
            // Rotate around X axis
            float y1 = y * cosX - z * sinX;
            float z1 = y * sinX + z * cosX;
        
            // Rotate around Y axis
            float x2 = x * cosY + z1 * sinY;
            float z2 = -x * sinY + z1 * cosY;
        
            // Rotate around Z axis
            float x3 = x2 * cosZ - y1 * sinZ;
            float y3 = x2 * sinZ + y1 * cosZ;
        
            rotated[i][0] = x3;
            rotated[i][1] = y3;
            rotated[i][2] = z2;
        
            // Perspective projection
            float scale = MIN_DIMENSION / 4; // 22.0
            projected[i][0] = x3 * scale * workScale + WIDTH / 2.0;
            projected[i][1] = y3 * scale * workScale + HEIGHT / 2.0;
        }
    } // rotateCube()

    // ===============================================================================

    static void cubePrepare() {

        const ModConfig& scaleMod = cube.modScale;
        const ModConfig& rsxMod   = cube.modRotateSpeedX;
        const ModConfig& rsyMod   = cube.modRotateSpeedY;
        const ModConfig& rszMod   = cube.modRotateSpeedZ;

        // -----------------------------------------------------------------
        // 1) Plumbing: configure timer channels
        // -----------------------------------------------------------------

        timings.ratio[scaleMod.modTimer] = 0.0004f * scaleMod.modRate;
        timings.ratio[rsxMod.modTimer]   = 0.0004f * rsxMod.modRate;
        timings.ratio[rsyMod.modTimer]   = 0.00045f * rsyMod.modRate;
        timings.ratio[rszMod.modTimer]   = 0.0005f * rszMod.modRate;

        calculate_modulators(timings, 4);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: centered bipolar control signals [-1, 1]
        // -----------------------------------------------------------------

        const float scaleSignal = move.directional_noise[scaleMod.modTimer];

        const ModConfig* rsMods[3] = {&rsxMod, &rsyMod, &rszMod};
        float rsSignal[3];
        for (int i = 0; i < 3; i++) {
            rsSignal[i] = move.directional_noise[rsMods[i]->modTimer];
        }

        // -----------------------------------------------------------------
        // 3) Artistic application: decide what those signals mean
        // -----------------------------------------------------------------

        // Scale: orbitalDots-style modulation, clamped positive
        workScale = cube.scale *
            ((1.0f - scaleMod.modLevel) + scaleMod.modLevel * scaleSignal);
        workScale = fmaxf(0.1f, workScale);

        // RotateSpeed: orbitalDots-style per axis (allows reversals)
        for (int i = 0; i < 3; i++) {
            float speed = cube.rotateSpeed[i] *
                ((1.0f - rsMods[i]->modLevel) + rsMods[i]->modLevel * rsSignal[i]);
            workRotateSpeed[i] = cube.axisFreeze[i] ? 0.0f : speed;
        }
    }

    // =======================================================

    static void emitCube() {

		cubePrepare();
		rotateCube();

		// Draw all edges with rainbow colors onto the float grid
		for (int i = 0; i < 12; i++) {
			int v0 = edges[i][0];
			int v1 = edges[i][1];

			// Convert edge hue to float RGB (matching original CHSV(i * 21, 255, 255))
			ColorF c = hsvRainbow((i * 21) / 255.0f);

			drawFixedColorLine(projected[v0][0], projected[v0][1],
					projected[v1][0], projected[v1][1],
					c.r, c.g, c.b);
		}

		// Update rotation angles
		angleX += workRotateSpeed[0];
		angleY += workRotateSpeed[1];
		angleZ += workRotateSpeed[2];

    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END

} // namespace flowFields