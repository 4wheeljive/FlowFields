#pragma once

// ═══════════════════════════════════════════════════════════════════
//  DIRECTIONAL FLOW FIELD — flow_directional.h
// ═══════════════════════════════════════════════════════════════════
//
//  Self-contained flow field implementation.
//  Includes colorTrailsTypes.h for shared types and instances.
//  cVar bridge helpers (pushFlowDefaultsToCVars / syncFlowFromCVars)
//  live in colorTrails_detail.hpp since they depend on bleControl.h.

#include "colorTrailsTypes.h"

namespace colorTrails {

    struct DirectionalParams {
        //bool rotate = true;
        //bool wave = true;
        float rotateSpeed = 1.0f;
        float waveAmp = 0.2f;
        float waveFreq = 0.2f;
        //bool noise = false;
    };

    DirectionalParams   directional;

    static void directionalPrepare(float t) {
        (void)t;   
    }

    static void directionalAdvect(float dt) {
        (void)dt;
    }


} // namespace colorTrails
