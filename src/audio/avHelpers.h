#pragma once

#include "audioTypes.h"
#include "leadEnergy.h"
#include "parameterSchema.h"
#include "fl/time_alpha.h"

namespace myAudio {

    // basicPulse: avResponse decays exponentially from 1.0
    void basicPulse(Bus& bus){
        if (bus.newBeat) { bus.avResponse = 1.0f;}
        if (bus.avResponse > .1f) {
            bus.avResponse = bus.avResponse * bus.expDecayFactor;  // Exponential decay
        } else {
            bus.avResponse = 0.f;
        }
    }


    // dynamicPulse: louder beat = faster rise, higher peak and longer fallTime
    void dynamicPulse(Bus& bus, uint32_t now) {
        // Each bus gets its own TimeRamp instance (static = persists across calls)
        static fl::TimeRamp ramps[NUM_BUSES] = {
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0)
        };
        static float peaks[NUM_BUSES] = {0.0f, 0.0f, 0.0f};
        fl::TimeRamp& ramp = ramps[bus.id];
        float& peak = peaks[bus.id];

        if (bus.newBeat) {
            float intensity = fl::clamp(bus.relativeIncrease - bus.threshold, 0.0f, 100.0f);
            // Soft saturation: hyperbolic pre-normalize to [0,1), then easeOutCubic.
            // k=2 → 50% saturation at intensity=2; good dynamic range up to ~1.0,
            // barely noticeable above ~5. Both peak and fallingTime use the same
            // eased value so louder beats are brighter AND last longer.
            //
            float t    = intensity / (intensity + 2.0f);                        // [0, 1)
            float ease = 1.0f - (1.0f - t) * (1.0f - t); // <--easeOutQuad    * (1.0f - t);       //<--easeOutCubic
            peak = bus.peakBase + ease ; // * 0.4f                             // ~[0.0, 2.0)
            uint32_t risingTime = (uint32_t)(ease * bus.rampAttack);           // bus.rampAttack
            uint32_t fallingTime = (uint32_t)(10.0f + ease * bus.rampDecay);   // bus.rampDecay
            ramp = fl::TimeRamp(0, risingTime, fallingTime);
            ramp.trigger(now);
        }

        uint8_t currentAlpha = ramp.update8(now);
        bus.avResponse = peak * currentAlpha / 255.0f;
    }


    void ehancedTrend(Bus& bus, uint32_t now) {
        // Each bus gets its own TimeRamp instance (static = persists across calls)
        static fl::TimeRamp ramps[NUM_BUSES] = {
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0)
        };
        static float peaks[NUM_BUSES] = {0.0f, 0.0f, 0.0f};
        fl::TimeRamp& ramp = ramps[bus.id];
        float& peak = peaks[bus.id];

        if (bus.newBeat) {
            float intensity = fl::clamp(bus.relativeIncrease - bus.threshold, 0.0f, 100.0f);
            // Soft saturation: hyperbolic pre-normalize to [0,1), then easeOutCubic.
            // k=2 → 50% saturation at intensity=2; good dynamic range up to ~1.0,
            // barely noticeable above ~5. Both peak and fallingTime use the same
            // eased value so louder beats are brighter AND last longer.
            //
            float t    = intensity / (intensity + 2.0f);                        // [0, 1)
            float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);           // easeOutCubic
            peak = bus.peakBase + ease * 0.4f;            // bus.peakBase     // ~[0.0, 2.0)
            uint32_t risingTime = (uint32_t)(ease * bus.rampAttack);           // bus.rampAttack
            uint32_t fallingTime = (uint32_t)(30.0f + ease * bus.rampDecay);   // bus.rampDecay
            ramp = fl::TimeRamp(0, risingTime, fallingTime);
            ramp.trigger(now);
        }

        uint8_t currentAlpha = ramp.update8(now);
        bus.avResponse = bus.energyEMA + peak * currentAlpha / 255.0f;

    }


    // normEnvelope: avResponse tracks normalized energy with fast attack / slow release.
    // Rises quickly on spikes, decays smoothly — no beat trigger required.
    void normEnvelope(Bus& bus) {
        bus.avResponse = bus.normEMA;
    }


    // leadResponse: envelope follower on lead.energy with asymmetric attack/release.
    // Provides visual sustain for lead/vocal channel — rises quickly with new energy,
    // decays slowly so the visual doesn't drop out between phrases.
    void leadResponse(Bus& bus) {
        static float envelope = 0.0f;

        float input = lead.energy;
        constexpr float attack  = 0.35f;  // fast rise — ~2-frame half-life
        constexpr float release = 0.06f;  // slow decay — ~11-frame half-life, visual sustain

        float alpha = (input > envelope) ? attack : release;
        envelope += alpha * (input - envelope);

        bus.avResponse = envelope;
    }


    /*float smoothVoxConf(float vC) {
        constexpr float attack  = 0.8f;  // fast rise on spikes (orig 0.35)
        constexpr float release = 0.4f;  // slow decay (orig 0.04)
        float alpha  = (vC > voxConfEMA) ? attack : release;
        voxConfEMA += alpha * (vC - voxConfEMA);
        return voxConfEMA;
    }*/

    /*float vocalResponse() {
        static float busCSmoothEMA = 0.0f;
        constexpr float busC_alpha = 0.5f;  // was 0.15f "symmetric, ~6-frame half-life"
        busCSmoothEMA += busC_alpha * (busC.norm - busCSmoothEMA);
        //smoothedVoxConf = smoothVoxConf(voxConf);
        //scaledVoxConf = fl::map_range_clamped<float, float>(smoothedVoxConf, 0.2f, 0.7f, 0.0f, 1.0f);
        //voxApprox = busCSmoothEMA * (1.0f + scaledVoxConf);   
        voxApprox = busCSmoothEMA * (1.0f + busC.norm); // range [0, ~2*busC]
        return voxApprox;
    }*/

    /*float spinner(Bus& bus, uint32_t now) {
       // Each bus gets its own TimeRamp instance (static = persists across calls)
        static fl::TimeRamp ramps[NUM_BUSES] = {
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0),
            fl::TimeRamp(0, 0, 0)
        };
        static float peaks[NUM_BUSES] = {0.0f, 0.0f, 0.0f};
        fl::TimeRamp& ramp = ramps[bus.id];
        float& peakSpinRate = peaks[bus.id];

        if (fancyTrigger) {
            float t = random8(50,200)/255.f;  // placeholder generator of float values ~[0.2, 0.8]  
            float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);           // easeOutCubic
            peakSpinRate = 10.f + ease * 40.f;      // setting the fastest spin rate; test range: 10 - 50
            uint32_t risingTime = (uint32_t)(ease * 15.f + bus.rampAttack);   // need to add appropriate ease factor // goal: // use bus.rampAttack as modifier to fast, eased base 
            uint32_t fallingTime = (uint32_t)(50.0f + ease * 500.f * bus.rampDecay);   
            ramp = fl::TimeRamp(0, risingTime, fallingTime);
            ramp.trigger(now);
        }

        uint8_t currentAlpha = ramp.update8(now);
        //bus.spinRate = peakSpinRate * currentAlpha / 255.0f;
    }*/
 
} // myAudio
