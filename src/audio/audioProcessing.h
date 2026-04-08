#pragma once

// =====================================================
// audioProcessing.h — Audio pipeline management.
// Transforms raw audio into normalized, per-band frames
// for visualization. Single entry point for programs.
// =====================================================

#include "audioCapture.h"   // transitively includes audioTypes.h, audioInput.h
#include "avHelpers.h"
#include "parameterSchema.h"

namespace myAudio {

    //=====================================================================
    // Visualization config — map BLE controls to internal values
    //=====================================================================

    inline void updateVizConfig() {
        vizConfig.audioFloorLevel = cAudioFloor * FLOOR_SCALE_LEVEL;
        vizConfig.audioFloorFft   = cAudioFloor * FLOOR_SCALE_FFT;
        vizConfig.gainLevel       = cAudioGain * GAIN_SCALE_LEVEL;
        vizConfig.gainFft         = cAudioGain * GAIN_SCALE_FFT;
        vizConfig.avLevelerTarget = cAvLevelerTarget;
        vizConfig.autoFloorAlpha  = cAutoFloorAlpha;
        vizConfig.autoFloorMin    = cAutoFloorMin;
        vizConfig.autoFloorMax    = cAutoFloorMax;
        vizConfig.avLeveler       = avLeveler;
        vizConfig.autoFloor       = autoFloor;
    }

    //=====================================================================
    // Auto AV leveling  — Robbins-Monro P90 ceiling estimation
    //=====================================================================

    void updateAvLeveler(float level) {
        if (!vizConfig.avLeveler) {
            avLevelerValue = 1.0f;
            return;
        }

        // Percentile-ceiling approach (preserves dynamic range) ---
        // Estimate the 90th percentile of recent input levels using
        // Robbins-Monro stochastic quantile estimation. Gain is set so
        // the ceiling maps to avLevelerTarget in the final rms_norm output,
        // while quieter signals remain proportionally lower.
        static float ceilingEstimate = 0.02f;  // initial guess for P90 of [rmsNormRaw]
        static bool prevGateOpen = false;

        // On gate-open transition: reset to clean defaults.
        // The smoothed RMS is still near zero at this point, so we
        // can't seed from the current level — use known-good values.
        if (noiseGateOpen && !prevGateOpen) {
            ceilingEstimate = 0.02f;
            avLevelerValue = 1.0f;
        }
        prevGateOpen = noiseGateOpen;

        // Freeze avLeveling gain when noise gate is closed.
        // Prevents ceiling decay and gain buildup during silence.
        if (!noiseGateOpen) return;

        constexpr float targetPercentile = 0.90f;
        constexpr float alpha = 0.12f;  // upward effective: 0.108, downward effective: 0.012

        // Asymmetric proportional update: converges to the target quantile.
        // Above-P90 samples (rare, ~10%) push up with weight p to compensate;
        // below-P90 samples (common, ~90%) push down with weight (1-p).
        if (level > ceilingEstimate) {
            ceilingEstimate += alpha * targetPercentile * (level - ceilingEstimate);
            // was: alpha * (1.0f - targetPercentile) — weights were swapped (tracked ~P10)
        } else {
            ceilingEstimate += alpha * (1.0f - targetPercentile) * (level - ceilingEstimate);
            // was: alpha * targetPercentile — weights were swapped (tracked ~P10)
        }
        ceilingEstimate = FL_MAX(ceilingEstimate, 0.0005f);  // prevent collapse
        lastAutoGainCeil = ceilingEstimate;

        // Solve for autoGainValue:
        float desired = vizConfig.avLevelerTarget / (ceilingEstimate * vizConfig.gainLevel);
        desired = fl::clamp(desired, 0.1f, 8.0f);
        lastAutoGainDesired = desired;  

        // Asymmetric smoothing: fast up (react quickly to crescendos),
        // slow down (ride smoothly through quiet passages).
        constexpr float levelerAttack  = 0.35f;  // fast rise
        constexpr float levelerRelease = 0.10f;  // slow decay
        float levelerAlpha = (desired > avLevelerValue) ? levelerAttack : levelerRelease;
        avLevelerValue += levelerAlpha * (desired - avLevelerValue);
        
    }

    //=====================================================================
    // Auto-floor — adaptive noise floor
    //=====================================================================

    void updateAutoFloor(float level) {
        if (!vizConfig.autoFloor) {
            return;
        }

        // Only adapt when near the existing floor to avoid chasing loud signals.
        if (level < (vizConfig.audioFloorLevel + 0.02f)) {
            float nf = vizConfig.audioFloorLevel * (1.0f - vizConfig.autoFloorAlpha)
                       + level * vizConfig.autoFloorAlpha;
            vizConfig.audioFloorLevel = fl::clamp(nf, vizConfig.autoFloorMin, vizConfig.autoFloorMax);
        }
    }

    //=====================================================================
    // updateBus — per-band spectral flattening
    //=====================================================================

    inline void updateBus(const AudioFrame& frame, const binConfig& b, Bus& bus) {
        bus.isActive = false;
        bus.newBeat = false;

        if (!frame.valid || !frame.fft_norm_valid) {
            bus.norm = 0.0f;
            bus.factor = 0.0f;
            return;
        }

        float sum = 0.0f;
        uint8_t count = 0;
        for (uint8_t i = 0; i < b.NUM_FFT_BINS; i++) {
            if (bin[i].bus == &bus) {
                sum += frame.fft_pre[i];
                count++;
            }
        }

        if (count > 0) { bus.isActive = true; }

        float avg = (count > 0) ? (sum / static_cast<float>(count)) : 0.0f;

        constexpr float eqAlpha = 0.02f;  // ~1-2 sec half-life
        bus.avgLevel += eqAlpha * (avg - bus.avgLevel);
        bus.avgLevel = FL_MAX(bus.avgLevel, 0.0001f);  // linear scale floor; tuned for FFT_MAX_FREQ=4000 (was 0.0001 at 5000/8000, 0.001 at 16000)

        // Store spectrally-flattened value (cross-cal and gain applied later)
        bus.norm = avg / bus.avgLevel;
    }

    //=====================================================================
    // finalizeBus — beat detection + cross-calibration + gain
    //=====================================================================

    inline void finalizeBus(const AudioFrame& frame, Bus& bus, float crossCalRatio, float gainApplied) {
        // Capture pre-finalize norm (spectrally-flattened, before cross-cal/gain)
        bus.preNorm = bus.norm;

        if (!bus.isActive) return;

        // Absolute energy guard: suppress beat detection when the raw bin energy
        // is negligible. rawAvg recovers the actual mean fft_pre across this bus's
        // bins before whitening: rawAvg = (avg/avgLevel) * avgLevel = avg.
        // This prevents weak harmonic bleed from triggering a bus whose primary
        // frequency range isn't active. Tune minRawEnergy as needed.
        // fft_pre is now bins_raw/32768 (linear), so typical music signals
        // are ~0.01-0.10; harmonic bleed on quiet bins is <0.002.
        float rawAvg = bus.preNorm * bus.avgLevel;
        constexpr float minRawEnergy = 0.0002f;  // tuned for FFT_MAX_FREQ=4000 (was 0.0002 at 5000/8000, 0.002 at 16000)

        // --- Beat detection on pre-finalize norm so onset shape isn't distorted ---
        // Compare current energy against EMA baseline (check BEFORE updating
        // EMA so the onset spike isn't yet blended into the baseline).
        // Skip beat detection until EMA has warmed up: avoids spurious beats at
        // startup and after silence (where avgLevel decays slower than energyEMA,
        // causing preNorm to recover to ~1.0 while EMA is still near zero).
        constexpr float emaAlpha = 0.15f;   // ~6-7 frame half-life
        constexpr float emaWarmupFloor = 0.005f;  // tuned for FFT_MAX_FREQ=4000 (was 0.005 at 5000/8000, 0.05 at 16000)
        if (bus.energyEMA >= emaWarmupFloor && rawAvg >= minRawEnergy) {
            float increase = bus.preNorm - bus.energyEMA;
            bus.relativeIncrease = increase / bus.energyEMA;

            uint32_t now = frame.timestamp;
            if (bus.relativeIncrease > bus.threshold && (now - bus.lastBeat) > bus.minBeatInterval) {
                bus.newBeat = true;
                bus.lastBeat = now;
            }
        } else {
            bus.relativeIncrease = 0.0f;
        }

        // Update EMA after beat check (always runs so baseline tracks signal)
        bus.energyEMA += emaAlpha * (bus.preNorm - bus.energyEMA);

        // --- Apply cross-cal and gain for visualization ---
        bus.norm = fl::clamp(bus.norm * crossCalRatio * gainApplied, 0.0f, 1.0f);

        constexpr float gamma = 0.5754f; // ln(0.5)/ln(0.3)
        bus.factor = 2.0f * fl::powf(bus.norm, gamma);

        // --- Asymmetric EMA of normalized value (envelope follower) ---
        constexpr float normAttack  = 0.35f;  //fast rise on spikes (orig 0.35)
        constexpr float normRelease = 0.04f;  //slow decay (orig 0.04)
        float normAlpha = (bus.norm > bus.normEMA) ? normAttack : normRelease;
        bus.normEMA += normAlpha * (bus.norm - bus.normEMA);
    }

    //=====================================================================
    // captureAudioFrame — main per-frame pipeline orchestrator
    //=====================================================================

    inline const AudioFrame& captureAudioFrame(binConfig& b) {
        static AudioFrame frame;
        static uint32_t lastFftTimestamp = 0;

        // *** STAGE: set current AudioVizConfig parameters
        updateVizConfig();

        // *** STAGE: capture filtered audio sample
        sampleAudio();

        // getVocalConfidence()->update() runs inside audioProcessor.update() (called by sampleAudio),
        // so getConfidence() is already current for this frame.
        // audioProcessor.getVocalConfidence() outputs significant positive values even during silence;
        //   so need to shut off getVocalConfidence() input when noiseGate is closed          
        // FL vocal detector not used:   
        // voxConf = noiseGateOpen ? audioProcessor.getVocalConfidence() : 0.0f;

        // Gate-open transition: reset per-bus EMA state so that avgLevel (alpha=0.02,
        // very slow) doesn't produce inflated _norm on the first beats after silence.
        // Without this, avgLevel decays to ~0.01 during gate-closed silence, causing
        // avg/avgLevel to spike to 10-40x for all buses equally when music resumes.
        {
            static bool prevGateForBus = false;
            if (noiseGateOpen && !prevGateForBus) {
                const float kResetLevel = 0.001f;  // linear FFT scale; tuned for FFT_MAX_FREQ=4000 (was 0.001 at 5000/8000, 0.01 at 16000)
                busA.avgLevel = kResetLevel;  busA.energyEMA = 0.0f;
                busB.avgLevel = kResetLevel;  busB.energyEMA = 0.0f;
                busC.avgLevel = kResetLevel;  busC.energyEMA = 0.0f;
            }
            prevGateForBus = noiseGateOpen;
        }

        frame.valid = filteredSample.isValid();
        frame.timestamp = currentSample.timestamp();
        frame.pcm = filteredSample.pcm();

        // *** STAGE: Run FFT engine once per timestamp
        static const fl::audio::fft::Bins* lastFft = nullptr;
        const fl::audio::fft::Bins* fftForBeat = nullptr;
        float rmsNormFast = 0.0f;
        float timeEnergy = 0.0f;
        float beatBins[MAX_FFT_BINS] = {0.0f};
        bool beatBinsValid = false;
        float rmsPostFloor = 0.0f;
        float rmsPostFloorFast = 0.0f; 
        float gainAppliedLevel = 1.0f;
        if (frame.valid) {
            if (frame.timestamp != lastFftTimestamp) {
                fftForBeat = getFFT(b);
                lastFftTimestamp = frame.timestamp;
                lastFft = fftForBeat;
            } else {
                fftForBeat = lastFft;
            }
            frame.rms_raw = filteredSample.rms(); // no temporal smoothing
            rmsNormFast = frame.rms_raw / 32768.0f;
            rmsNormFast = fl::clamp(rmsNormFast, 0.0f, 1.0f);
        } else {
            frame.rms_raw = 0.0f;
        }
        frame.fft = fftForBeat;

        // *** STAGE: Get frame RMS and calculate _norm and _factor values
        frame.rms = getRMS(); // with temporal smoothing; currently used only for diagnostics
        
        if (frame.valid) {

            // Normalize RMS/peak and update auto-calibration
            float rmsNormRaw = frame.rms / 32768.0f; // with temporal smoothing
            rmsNormFast = frame.rms_raw / 32768.0f; // no temporal smoothing
            float peakNormRaw = frame.peak / 32768.0f;
            rmsNormRaw = fl::clamp(rmsNormRaw, 0.0f, 1.0f);
            rmsNormFast = fl::clamp(rmsNormFast, 0.0f, 1.0f);
            peakNormRaw = fl::clamp(peakNormRaw, 0.0f, 1.0f);

            updateAutoFloor(rmsNormRaw);
            updateAvLeveler(rmsNormRaw);
            // rmsPostFloor is currently computed but unused. You could remove it and the getRMS() call if nothing else
            // references them, but keeping them costs essentially nothing and they're useful for diagnostics.
            rmsPostFloor = FL_MAX(0.0f, rmsNormRaw - vizConfig.audioFloorLevel);

            timeEnergy = FL_MAX(0.0f, rmsNormFast - vizConfig.audioFloorLevel);

            // Fast RMS for bus cross-calibration: single asymmetric EMA of
            // unsmoothed RMS, bypassing the median+EMA cascade in getRMS().
            // Gives bus.norm/factor/normEMA ~2 frames less latency on onsets.
            static float rmsCrossCalEMA = 0.0f;
            constexpr float rmsCcAttack  = 0.6f;   // ~1 frame to 60% of onset
            constexpr float rmsCcRelease = 0.15f;  // ~4 frame half-life for decay
            float rmsCcAlpha = (rmsNormFast > rmsCrossCalEMA) ? rmsCcAttack : rmsCcRelease;
            rmsCrossCalEMA += rmsCcAlpha * (rmsNormFast - rmsCrossCalEMA);
            rmsPostFloorFast = FL_MAX(0.0f, rmsCrossCalEMA - vizConfig.audioFloorLevel);

            gainAppliedLevel = vizConfig.gainLevel * avLevelerValue;
            float gainAppliedFft = vizConfig.gainFft * avLevelerValue;

            frame.rms_norm = rmsNormRaw;
            frame.rms_norm = fl::clamp(FL_MAX(0.0f, frame.rms_norm - vizConfig.audioFloorLevel) * gainAppliedLevel, 0.0f, 1.0f);

            // rmsFactor: 0.0–2.0 multiplicative scale, 1.0 at neutralPoint
            constexpr float neutralPoint = 0.3f;
            constexpr float gamma = 0.5754f; // ln(0.5)/ln(0.3)
            frame.rms_factor = 2.0f * fl::powf(frame.rms_norm, gamma);

            // *** STAGE: Derive busses/bands from FFT bins (band boundaries set in binConfig),
            //            calculate _norm and _factor values
            frame.fft_norm_valid = false;
            //if (frame.fft && frame.fft->bins_db.size() > 0) {  // pre-FastLED API change
            if (frame.fft && frame.fft->db().size() > 0) {
                for (uint8_t i = 0; i < b.NUM_FFT_BINS; i++) {
                    // --- Visualization path: dB-linear scale (perceptually uniform for display) ---
                    float mag_db = 0.0f;
                    //if (i < frame.fft->bins_db.size()) {  // pre-FastLED API change
                    //    mag_db = frame.fft->bins_db[i] / 100.0f;
                    if (i < frame.fft->db().size()) {
                        mag_db = frame.fft->db()[i] / 100.0f;
                    }
                    mag_db = FL_MAX(0.0f, mag_db - vizConfig.audioFloorFft);
                    frame.fft_norm[i] = fl::clamp(mag_db * gainAppliedFft, 0.0f, 1.0f);

                    // --- Bus beat detection path: true linear amplitude ---
                    // raw() is the Q15 linear magnitude; /32768 normalizes to [0, ~1].
                    // A harmonic 30 dB below its fundamental is ~3% of it here,
                    // vs ~30% in the dB-linear (/100) domain — far better harmonic
                    // rejection for per-bus frequency discrimination.
                    float mag_lin = 0.0f;
                    //if (i < frame.fft->bins_raw.size()) {  // pre-FastLED API change
                    //    mag_lin = frame.fft->bins_raw[i] / 32768.0f;
                    if (i < frame.fft->raw().size()) {
                        mag_lin = frame.fft->raw()[i] / 32768.0f;
                    }
                    frame.fft_pre[i] = fl::clamp(mag_lin, 0.0f, 1.0f);
                }
                for (uint8_t i = b.NUM_FFT_BINS; i < MAX_FFT_BINS; i++) {
                    frame.fft_pre[i] = 0.0f;
                    frame.fft_norm[i] = 0.0f;
                }
                frame.fft_norm_valid = true;

            } else { // if no valid fft data

                for (uint8_t i = 0; i < b.NUM_FFT_BINS; i++) {
                    frame.fft_pre[i] = 0.0f;
                    frame.fft_norm[i] = 0.0f;
                }
                frame.fft_norm_valid = false;
            }
        } else { // if frame not valid
            frame.fft = nullptr;
            frame.fft_norm_valid = false;
            frame.rms_norm = 0.0f;
            //frame.peak_norm = 0.0f;
            for (uint8_t i = 0; i < b.NUM_FFT_BINS; i++) {
                frame.fft_pre[i] = 0.0f;
                frame.fft_norm[i] = 0.0f;
            }
        }

        if (b.busBased) {
        
            // Update bus outputs (phase 1: compute spectrally-flattened values)
            updateBus(frame, b, busA);
            updateBus(frame, b, busB);
            updateBus(frame, b, busC);

            // Phase 2: Apply RMS-domain cross-calibration and gain for visualization.
            // Uses rmsPostFloorFast (asymmetric EMA of unsmoothed RMS) so bus
            // envelopes track onsets ~2 frames faster than the old median+EMA path.
            // In steady state, whitened _norm ≈ 1.0, so bus._norm ≈ crossCal * gain ≈ rms_norm.
            finalizeBus(frame, busA, rmsPostFloorFast, gainAppliedLevel);
            finalizeBus(frame, busB, rmsPostFloorFast, gainAppliedLevel);
            finalizeBus(frame, busC, rmsPostFloorFast, gainAppliedLevel);

            frame.busA = busA;
            frame.busB = busB;
            frame.busC = busC;

            // Lead energy: compute features before vocalResponse() uses lead.confidence
            updateLeadEnergy(busA.norm, busB.norm, busC.norm,
                             frame.fft_pre, b.NUM_FFT_BINS);
            frame.voxApprox = lead.energy;

            // Vocal response: smooth, scale, and blend with busC energy
            // NOTE: test hook to FL vocal detector disabled; current "vocal respose"  
            //   voxApprox = busCSmoothEMA * (1.0f + busC.norm)     
            //frame.voxConf = voxConf;
            //frame.smoothedVoxConf = smoothedVoxConf;
            //frame.scaledVoxConf = scaledVoxConf;
            //frame.voxApprox = voxApprox;

        
        } // if busBased

        return frame;

    } // captureAudioFrame()

    //=====================================================================
    // Frame cache — one snapshot per loop iteration
    //=====================================================================

    AudioFrame gAudioFrame;
    bool gAudioFrameInitialized = false;
    uint32_t gAudioFrameLastMs = 0;
    //bool audioLatencyDiagnostics = true;

    inline uint32_t getAudioSampleRate() {
        uint32_t sampleRate = fl::audio::fft::Args::DefaultSampleRate();
        if (config.is<fl::audio::ConfigI2S>()) {
            sampleRate = static_cast<uint32_t>(config.get<fl::audio::ConfigI2S>().mSampleRate);
        } else if (config.is<fl::audio::ConfigPdm>()) {
            sampleRate = static_cast<uint32_t>(config.get<fl::audio::ConfigPdm>().mSampleRate);
        }
        return sampleRate;
    }

    inline const AudioFrame& updateAudioFrame(binConfig& b) {
        uint32_t now = fl::millis();

        if (gAudioFrameInitialized && now == gAudioFrameLastMs) {
            if (gAudioFrame.fft_norm_valid) {
                return gAudioFrame;
            }
        }

        const AudioFrame& frame = captureAudioFrame(b);

        if (audioLatencyDiagnostics) {
            struct LatencyStats {
                bool epochSet = false;
                int32_t epochOffsetMs = 0;
                uint32_t windowStartMs = 0;
                uint32_t lastFrameMs = 0;
                uint32_t frameCount = 0;
                uint64_t sumFrameMs = 0;
                uint32_t minFrameMs = 0xFFFFFFFFu;
                uint32_t maxFrameMs = 0;

                uint32_t sampleCount = 0;
                int32_t minLatencyMs = 0x7FFFFFFF;
                int32_t maxLatencyMs = -0x7FFFFFFF;
                int64_t sumLatencyMs = 0;

                uint32_t lastPcmSamples = 0;
                uint32_t lastSampleRate = 0;
                uint32_t invalidCount = 0;
            };

            static LatencyStats stats;

            if (stats.windowStartMs == 0) {
                stats.windowStartMs = now;
            }

            if (!frame.valid) {
                stats.invalidCount++;
            } else {
                if (!stats.epochSet) {
                    stats.epochOffsetMs = static_cast<int32_t>(now) - static_cast<int32_t>(frame.timestamp);
                    stats.epochSet = true;
                }
                int32_t alignedTimestampMs =
                    static_cast<int32_t>(frame.timestamp) + stats.epochOffsetMs;
                int32_t latencyMs = static_cast<int32_t>(now) - alignedTimestampMs;

                stats.sampleCount++;
                stats.sumLatencyMs += latencyMs;
                if (latencyMs < stats.minLatencyMs) stats.minLatencyMs = latencyMs;
                if (latencyMs > stats.maxLatencyMs) stats.maxLatencyMs = latencyMs;
            }

            if (stats.lastFrameMs != 0) {
                uint32_t frameDt = now - stats.lastFrameMs;
                stats.frameCount++;
                stats.sumFrameMs += frameDt;
                if (frameDt < stats.minFrameMs) stats.minFrameMs = frameDt;
                if (frameDt > stats.maxFrameMs) stats.maxFrameMs = frameDt;
            }
            stats.lastFrameMs = now;

            stats.lastPcmSamples = static_cast<uint32_t>(frame.pcm.size());
            stats.lastSampleRate = getAudioSampleRate();

            if ((now - stats.windowStartMs) >= 2000) {
                const uint32_t windowMs = now - stats.windowStartMs;
                const int32_t avgLatencyMs =
                    (stats.sampleCount > 0)
                        ? static_cast<int32_t>(stats.sumLatencyMs / stats.sampleCount)
                        : 0;
                const uint32_t avgFrameMs =
                    (stats.frameCount > 0)
                        ? static_cast<uint32_t>(stats.sumFrameMs / stats.frameCount)
                        : 0;
                const float fps =
                    (windowMs > 0)
                        ? (stats.frameCount * 1000.0f / static_cast<float>(windowMs))
                        : 0.0f;
                const uint32_t pcmMs =
                    (stats.lastSampleRate > 0)
                        ? static_cast<uint32_t>((stats.lastPcmSamples * 1000ULL) / stats.lastSampleRate)
                        : 0;

                FASTLED_DBG("Audio latency ms avg " << avgLatencyMs
                               << " min " << stats.minLatencyMs
                               << " max " << stats.maxLatencyMs
                               << " | frame ms avg " << avgFrameMs
                               << " max " << stats.maxFrameMs
                               << " | fps " << fps
                               << " | pcm " << stats.lastPcmSamples
                               << " (" << pcmMs << " ms) sr " << stats.lastSampleRate
                               << " | gate " << (noiseGateOpen ? 1 : 0)
                               << " | invalid " << stats.invalidCount);

                stats = LatencyStats();
                stats.windowStartMs = now;
            }
        } // if (audioLatencyDiagnostics)

        gAudioFrame = frame;
        gAudioFrameInitialized = true;
        gAudioFrameLastMs = now;

        return gAudioFrame;
    }

    inline const AudioFrame& getAudioFrame() {
        return gAudioFrame;
    }

    //=====================================================================
    // Bus parameter handler (called from BLE callbacks)
    //=====================================================================

    static void handleBusParam(uint8_t busId, const String& paramId, float value) {
        Bus* bus = (busId == 0) ? &busA : (busId == 1) ? &busB : &busC;
        if (!bus) return;
        if      (paramId == "inThreshold")      bus->threshold = value;
        else if (paramId == "inMinBeatInterval")  bus->minBeatInterval = value;
        else if (paramId == "inExpDecayFactor") bus->expDecayFactor = value;
        else if (paramId == "inRampAttack")     bus->rampAttack = value;
        else if (paramId == "inRampDecay")      bus->rampDecay = value;
        else if (paramId == "inPeakBase")       bus->peakBase = value;
    }

    static float handleGetBusParam(uint8_t busId, const String& paramName) {
        Bus* bus = (busId == 0) ? &busA : (busId == 1) ? &busB : &busC;
        if      (paramName == "threshold")      return bus->threshold;
        else if (paramName == "minBeatInterval") return bus->minBeatInterval;
        else if (paramName == "expDecayFactor") return bus->expDecayFactor;
        else if (paramName == "rampAttack")     return bus->rampAttack;
        else if (paramName == "rampDecay")      return bus->rampDecay;
        else if (paramName == "peakBase")       return bus->peakBase;
        return 0.0f;
    }

    //====================================================================================================
    // Initialize audio processing
    //====================================================================================================

    void initAudioProcessing() {

        if (audioProcessingInitialized) { return; }

        // Initialize bin/bus configurations
        setBinConfig();
        initBus(busA);
        initBus(busB);
        initBus(busC);
        initBins();
        setBusParam = handleBusParam;
        getBusParam = handleGetBusParam;

        // Disable FastLED AudioProcessor's internal signal conditioning — we already
        // handle spike filtering, DC correction, and noise gating in sampleAudio().
        // Double-processing can cause the conditioner to reject our cleaned signal.
        // Ability to disable FastLED autoGain through AudioProcessor API was removed.      
        // To disable, need to override in lib\FastLED\src\fl\audio\auto_gain.h and audio_processor.cpp.hpp   
        audioProcessor.setSignalConditioningEnabled(false);
        audioProcessor.setNoiseFloorTrackingEnabled(false);
        //audioProcessor.setAutoGainEnabled(false);

        // Force early creation of detectors so they're registered in
        // mActiveDetectors before the first audioProcessor.update() call.
        //audioProcessor.getVocalConfidence();

        Serial.println("AudioProcessor initialized");
        audioProcessingInitialized = true;
    }

    //=====================================================================
    // Debug output
    //=====================================================================

    void printDiagnostics() {
        const auto& f = gAudioFrame;
        uint8_t limit = maxBins ? bin32.NUM_FFT_BINS : bin16.NUM_FFT_BINS;
        
        /*
        FASTLED_DBG("rmsRaw " << (f.rms_raw / 32768.0f)
                               << " rmsSm " << (f.rms / 32768.0f)
                               << " gate " << (noiseGateOpen ? 1 : 0));
        FASTLED_DBG("blockRms(raw) " << (int)lastBlockRms
                               << " openAt " << (int)cNoiseGateOpen
                               << " closeAt " << (int)cNoiseGateClose
                               << " valid " << lastValidSamples << "/" << lastClampedSamples);
        FASTLED_DBG("pcmMin " << lastPcmMin << " pcmMax " << lastPcmMax);
        */
        /*
        FASTLED_DBG("autoGain " << (autoGain ? 1 : 0)
                               << " agCeil " << lastAutoGainCeil
                               << " agDesired " << lastAutoGainDesired
                               << " agVal " << autoGainValue
                               << " cAudioGain " << cAudioGain
                               << " gainLevel " << vizConfig.gainLevel);
        FASTLED_DBG("agCeilx1000 " << (lastAutoGainCeil * 1000.0f));
        */
        
        FASTLED_DBG("rmsNorm " << f.rms_norm);
        FASTLED_DBG("busA.norm " << f.busA.norm
                    << " normEMA " << f.busA.normEMA
        );
        FASTLED_DBG("busB: norm " << f.busB.norm
                    << " normEMA " << f.busB.normEMA
        );
        FASTLED_DBG("voxConf: " << f.voxConf 
                << " smoothed " << f.smoothedVoxConf
                << " scaled " << f.scaledVoxConf
                << " approx " << f.voxApprox
                << " busC.normEMA " << f.busC.normEMA
        );
        FASTLED_DBG("---------- ");

    }

    void printBusSettings () {
        const auto& f = gAudioFrame;
        FASTLED_DBG("busA.thresh " << f.busA.threshold);
        FASTLED_DBG("busA.minBeatInt " << f.busA.minBeatInterval);
        FASTLED_DBG("busA.peakBase " << f.busA.peakBase);
        FASTLED_DBG("busA.attack " << f.busA.rampAttack);
        FASTLED_DBG("busA.decay " << f.busA.rampDecay);
        FASTLED_DBG("busA.expDecay " << f.busA.expDecayFactor);
        FASTLED_DBG("---");
        FASTLED_DBG("busB.thresh " << f.busB.threshold);
        FASTLED_DBG("busB.minBeatInt " << f.busB.minBeatInterval);
        FASTLED_DBG("busB.peakBase " << f.busB.peakBase);
        FASTLED_DBG("busB.attack " << f.busB.rampAttack);
        FASTLED_DBG("busB.decay " << f.busB.rampDecay);
        FASTLED_DBG("busB.expDecay " << f.busB.expDecayFactor);
        FASTLED_DBG("---");
        FASTLED_DBG("busC.thresh " << f.busC.threshold);
        FASTLED_DBG("busC.minBeatInt " << f.busC.minBeatInterval);
        FASTLED_DBG("busC.peakBase " << f.busC.peakBase);
        FASTLED_DBG("busC.attack " << f.busC.rampAttack);
        FASTLED_DBG("busC.decay " << f.busC.rampDecay);
        FASTLED_DBG("busC.expDecay " << f.busC.expDecayFactor);
        FASTLED_DBG("---");
    }

} // namespace myAudio
