#pragma once

// =====================================================
// leadEnergy.h — Lead element energy tracker
//
// Detects sustained, prominent tonal energy indicative
// of vocals, lead guitar, horns, piano solos, and other
// "front line" elements in a music mix.
//
// Output: leadConfidence [0, 1] — how "lead-like" the
// current busC energy is.  Drop-in replacement for the
// old scaledVoxConf in vocalResponse().
// =====================================================

#include "audioTypes.h"
#include "fl/math_macros.h"

namespace myAudio {

    //=====================================================================
    // LeadTracker state
    //=====================================================================

    struct LeadTracker {

        // --- Internal EMAs ---
        float fastEMA       = 0.0f;   // fast-tracking busC energy
        float slowEMA       = 0.0f;   // slow-tracking busC energy
        float meanEMA       = 0.0f;   // for variance calc
        float meanSqEMA     = 0.0f;   // for variance calc
        float smoothedConf  = 0.0f;   // smoothed composite
        float busCSmooth    = 0.0f;   // smoothed busC.norm (same role as old busCSmoothEMA)

        // --- Per-feature outputs (readable for diagnostics) ---
        float sustainRatio    = 0.0f; // [0,1] sustained vs transient
        float prominence      = 0.0f; // [0,1] busC dominance in mix
        float stability       = 0.0f; // [0,1] temporal smoothness (anti-percussive)
        float concentration   = 0.0f; // [0,1] spectral narrowness in busC bins

        // --- Composite outputs ---
        float confidence      = 0.0f; // [0,1] overall lead confidence (replaces scaledVoxConf)
        float energy          = 0.0f; // final output (replaces voxApprox)
    };

    LeadTracker lead;

    //=====================================================================
    // Feature 1: Sustained energy ratio
    //
    // Compares a slow EMA to a fast EMA of busC energy.
    // Sustained signals (voice, held notes): slow catches up → ratio ≈ 1
    // Transients (cymbal hit, snare bleed): fast spikes, slow lags → ratio → 0
    //=====================================================================

    inline float leadSustainRatio(LeadTracker& lt, float busCNorm) {
        constexpr float alphaFast = 0.40f;  // ~2-frame half-life
        constexpr float alphaSlow = 0.12f;  // was 0.06 — faster cold-start convergence
        lt.fastEMA += alphaFast * (busCNorm - lt.fastEMA);
        lt.slowEMA += alphaSlow * (busCNorm - lt.slowEMA);

        constexpr float eps = 0.001f;
        return fl::clamp(lt.slowEMA / (lt.fastEMA + eps), 0.0f, 1.0f);
    }

    //=====================================================================
    // Feature 2: busC prominence in the overall mix
    //
    // What fraction of total bus energy lives in the lead range?
    // Lead elements are mixed to sit above the rhythm section.
    // High prominence = something lead-like is dominating.
    //=====================================================================

    inline float leadProminence(float busANorm, float busBNorm, float busCNorm) {
        constexpr float eps = 0.001f;
        return busCNorm / (busANorm + busBNorm + busCNorm + eps);
    }

    //=====================================================================
    // Feature 3: Temporal stability
    //
    // Running variance of busC.norm — low variance means the energy
    // is sustained and smooth (melodic line), high variance means
    // percussive transients are landing in the busC range.
    //
    // Returns inverse-variance mapped to [0, 1].
    //=====================================================================

    inline float leadStability(LeadTracker& lt, float busCNorm) {
        constexpr float alpha = 0.08f;  // ~8-frame half-life
        lt.meanEMA   += alpha * (busCNorm - lt.meanEMA);
        lt.meanSqEMA += alpha * (busCNorm * busCNorm - lt.meanSqEMA);

        float variance = lt.meanSqEMA - lt.meanEMA * lt.meanEMA;
        variance = FL_MAX(variance, 0.0f);  // numerical safety

        constexpr float k = 15.0f;  // sensitivity — higher = more suppression of transients
        return 1.0f / (1.0f + k * variance);
    }

    //=====================================================================
    // Feature 4: Spectral concentration within busC bins
    //
    // Lead elements concentrate energy in a few bins (voice formants,
    // instrument harmonics).  Broadband sources (cymbals, noise) spread
    // energy across all bins.
    //
    // Uses spectral crest factor: peak / mean.
    // High crest = concentrated (lead-like), low crest = flat (broadband).
    // Avoids log/exp — just a max-scan and a mean.
    //=====================================================================

    inline float leadConcentration(const float* fftPre, uint8_t numBins, const Bus& bus) {
        float sum = 0.0f;
        float peak = 0.0f;
        uint8_t count = 0;

        for (uint8_t i = 0; i < numBins; i++) {
            if (bin[i].bus != &bus) continue;
            float val = fftPre[i];
            sum += val;
            if (val > peak) peak = val;
            count++;
        }

        if (count == 0 || sum < 1e-8f) return 0.0f;

        float mean = sum / static_cast<float>(count);
        float crest = peak / (mean + 1e-10f);

        // crest ranges from 1.0 (flat) to count (single bin dominates).
        // With 8 busC bins: a voice or solo typically gives crest 2-4,
        // a cymbal wash gives crest ~1.2-1.5.
        // Map so that crest ≈ 3 → concentration ≈ 1.0
        constexpr float saturation = 3.0f;
        return fl::clamp((crest - 1.0f) / (saturation - 1.0f), 0.0f, 1.0f);
    }

    //=====================================================================
    // updateLeadEnergy — call once per frame after buses are finalized
    //
    // Computes four features, blends them into a composite confidence,
    // and produces a final energy value with the same structure as the
    // old voxApprox = busCSmoothEMA * (1 + scaledVoxConf).
    //=====================================================================

    inline float updateLeadEnergy(float busANorm, float busBNorm, float busCNorm,
                                  const float* fftPre, uint8_t numBins) {
        LeadTracker& lt = lead;

        // --- Compute features ---
        lt.sustainRatio   = leadSustainRatio(lt, busCNorm);
        lt.prominence     = leadProminence(busANorm, busBNorm, busCNorm);
        lt.stability      = leadStability(lt, busCNorm);
        lt.concentration  = leadConcentration(fftPre, numBins, busC);

        // --- Weighted composite ---
        constexpr float wSustain       = 0.35f;
        constexpr float wProminence    = 0.25f;
        constexpr float wStability     = 0.20f;
        constexpr float wConcentration = 0.20f;

        lt.confidence = wSustain       * lt.sustainRatio
                      + wProminence    * lt.prominence
                      + wStability     * lt.stability
                      + wConcentration * lt.concentration;

        // Smooth the composite to avoid frame-to-frame jitter
        constexpr float confAlpha = 0.25f;  // was 0.10 — faster tracking, no triple-smooth concern now
        lt.smoothedConf += confAlpha * (lt.confidence - lt.smoothedConf);

        // --- Final output: busC energy boosted by lead confidence ---
        // Same structure as: voxApprox = busCSmoothEMA * (1 + scaledVoxConf)
        constexpr float busCAlpha = 0.40f;  // was 0.25 — closer to old vocalResponse's 0.5; brightness path no longer stacks
        lt.busCSmooth += busCAlpha * (busCNorm - lt.busCSmooth);

        lt.energy = lt.busCSmooth * (1.0f + 0.5f * lt.smoothedConf);  // was (1 + conf) — caps boost at 1.5x instead of 2x
        return lt.energy;
    }

} // namespace myAudio
