#pragma once

#include "FastLED.h"
#include <ArduinoJson.h>
#include <string>

#include "componentEnums.h"

inline bool displayOn = true;

typedef void (*BusParamSetterFn)(uint8_t busId, const String& paramId, float value);
inline BusParamSetterFn setBusParam = nullptr;

// Callback to read a bus parameter value by busId and param name
typedef float (*BusParamGetterFn)(uint8_t busId, const String& paramName);
inline BusParamGetterFn getBusParam = nullptr;

extern uint8_t EMITTER;
extern uint8_t FLOW;

// ═══════════════════════════════════════════════════════════════════
// GLOBAL PARAMETERS
// ═══════════════════════════════════════════════════════════════════

const char* const GLOBAL_PARAMS[] PROGMEM = {
   "globalSpeed", "persistence", "persistFine", "colorShift"
};

const uint8_t GLOBAL_PARAM_COUNT = 4;

// ═══════════════════════════════════════════════════════════════════
//  EMITTERS
// ═══════════════════════════════════════════════════════════════════

// Emitter names in PROGMEM
const char orbitaldots_str[] PROGMEM = "orbitaldots";
const char swarmingdots_str[] PROGMEM = "swarmingdots";
const char audiodots_str[] PROGMEM = "audiodots";
const char lissajous_str[] PROGMEM = "lissajous";
const char borderrect_str[] PROGMEM = "borderrect";
const char noisekaleido_str[] PROGMEM = "noisekaleido";
const char cube_str[] PROGMEM = "cube";
const char fluidjet_str[] PROGMEM = "fluidjet";

const char* const EMITTERS[] PROGMEM = {
      orbitaldots_str, swarmingdots_str, audiodots_str, lissajous_str, borderrect_str, noisekaleido_str, cube_str, fluidjet_str
   };

const uint8_t EMITTER_COUNTS[] = {8};

// Emitter params
const char* const ORBITALDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "orbitSpeed", "orbitDiam",
   "modOrbitSpeedRate", "modOrbitSpeedLevel", "modOrbitDiamRate", "modOrbitDiamLevel"
};
const char* const SWARMINGDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "swarmSpeed", "swarmSpread",
   "modSwarmSpeedRate", "modSwarmSpeedLevel",
   "modSwarmSpreadRate", "modSwarmSpreadLevel"
};
const char* const AUDIODOTS_PARAMS[] PROGMEM = {};
const char* const LISSAJOUS_PARAMS[] PROGMEM = {
   "lineSpeed", "lineAmp", "lineClamp",
   "modLineSpeedRate", "modLineSpeedLevel",
   "modLineAmpRate", "modLineAmpLevel"
};
const char* const BORDERRECT_PARAMS[] PROGMEM = {};
const char* const NOISEKALEIDO_PARAMS[] PROGMEM = {
   "driftSpeed", "noiseScale", "noiseBand", "kaleidoGamma"
};
const char* const CUBE_PARAMS[] PROGMEM = {
   "scale", "rotateSpeedX", "rotateSpeedY", "rotateSpeedZ",
   "modScaleRate", "modScaleLevel",
   "modRotateSpeedXRate", "modRotateSpeedXLevel",
   "modRotateSpeedYRate", "modRotateSpeedYLevel",
   "modRotateSpeedZRate", "modRotateSpeedZLevel"
};
const char* const FLUIDJET_PARAMS[] PROGMEM = {
   "jetDensity", "jetForce", "jetRadius", "jetSpread", "jetHueSpeed",
   "modJetForceRate", "modJetForceLevel",
   "modAngleRate", "modAngleLevel"
};

// Struct to hold emitter name and parameter array reference
struct EmitterParamEntry {
   const char* EmitterName;
   const char* const* params;
   uint8_t count;
};

const EmitterParamEntry EMITTER_PARAM_LOOKUP[] PROGMEM = {
   {"orbitaldots", ORBITALDOTS_PARAMS, 8},
   {"swarmingdots", SWARMINGDOTS_PARAMS, 8},
   {"audiodots", AUDIODOTS_PARAMS, 0},
   {"lissajous", LISSAJOUS_PARAMS, 7},
   {"borderrect", BORDERRECT_PARAMS, 0},
   {"noisekaleido", NOISEKALEIDO_PARAMS, 4},
   {"cube", CUBE_PARAMS, 12},
   {"fluidjet", FLUIDJET_PARAMS, 9},
};

static const EmitterParamEntry* getEmitterParams(uint8_t emitterIdx) {
      if (emitterIdx >= EMITTER_COUNT) return nullptr;
      return &EMITTER_PARAM_LOOKUP[emitterIdx];
}

// ═══════════════════════════════════════════════════════════════════
//  FLOWS
// ═══════════════════════════════════════════════════════════════════

// Flow names in PROGMEM
const char noise_str[] PROGMEM = "noise";
const char radial_str[] PROGMEM = "radial";
const char directional_str[] PROGMEM = "directional";
const char rings_str[] PROGMEM = "rings";
const char spiral_str[] PROGMEM = "spiral";
const char fluid_str[] PROGMEM = "fluid";

const uint8_t FLOW_COUNTS[] = {6};

const char* const FLOWS[] PROGMEM = {
      noise_str, radial_str, directional_str, rings_str, spiral_str, fluid_str
   };

// Flow field params
const char* const NOISE_PARAMS[] PROGMEM = {
   "xSpeed", "ySpeed", "xAmp", "yAmp","xFreq", "yFreq", "xShift", "yShift",
   "modAmpRate", "modAmpLevel", "modSpeedRate", "modSpeedLevel",
   "modShiftRate", "modShiftLevel"
};
const char* const RADIAL_PARAMS[] PROGMEM = {
   "radialStep", "blendFactor"
};
const char* const DIRECTIONAL_PARAMS[] PROGMEM = {
   "windStep", "blendFactor", "rotateSpeed", "waveAmp", "waveFreq", "waveSpeed"
};
const char* const RINGS_PARAMS[] PROGMEM = {
   "innerSwirl", "outerSwirl", "midDrift",
   "modBreatheRate", "modBreatheLevel"
};
const char* const SPIRAL_PARAMS[] PROGMEM = {
   "angularStep", "radialStep", "blendFactor",
   "modAngularStepRate", "modAngularStepLevel",
   "modRadialStepRate", "modRadialStepLevel",
   "modBlendFactorRate", "modBlendFactorLevel"
};
const char* const FLUID_PARAMS[] PROGMEM = {
   "viscosity", "diffusion", "velocityDissipation", "dyeDissipation",
   "vorticity", "gravity", "solverIterations",
   "modVelDissipRate", "modVelDissipLevel",
   "modDyeDissipRate", "modDyeDissipLevel"
};
// Note: spiral reuses shared cVars radialStep and blendFactor

// Struct to hold flow field name and parameter array reference
struct FlowParamEntry {
   const char* FlowName;
   const char* const* params;
   uint8_t count;
};

const FlowParamEntry FLOW_PARAM_LOOKUP[] PROGMEM = {
   {"noise", NOISE_PARAMS, 14},
   {"radial", RADIAL_PARAMS, 2},
   {"directional", DIRECTIONAL_PARAMS, 6},
   {"rings", RINGS_PARAMS, 5},
   {"spiral", SPIRAL_PARAMS, 9},
   {"fluid", FLUID_PARAMS, 11}
};

static const FlowParamEntry* getFlowParams(uint8_t flowIdx) {
      if (flowIdx >= FLOW_COUNT) return nullptr;
      return &FLOW_PARAM_LOOKUP[flowIdx];
}

// ═══════════════════════════════════════════════════════════════════
// AUDIO SETTINGS
// ═══════════════════════════════════════════════════════════════════

const char* const AUDIO_PARAMS[] PROGMEM = {
"maxBins", "audioFloor", "audioGain",
"avLevelerTarget", "autoFloorAlpha", "autoFloorMin", "autoFloorMax",
"noiseGateOpen", "noiseGateClose",
"threshold", "minBeatInterval",
"rampAttack", "rampDecay", "peakBase", "expDecayFactor"
};

const uint8_t AUDIO_PARAM_COUNT = 15;

// ═══════════════════════════════════════════════════════════════════
//  MISCELLANEOUS CONTROLS
// ═══════════════════════════════════════════════════════════════════

inline uint8_t cBright = 35;
inline uint8_t cMapping = 0;
inline uint8_t cOverrideMapping = 0;

/*fl::EaseType getEaseType(uint8_t value) {
    switch (value) {
        case 0: return fl::EASE_NONE;
        case 1: return fl::EASE_IN_QUAD;
        case 2: return fl::EASE_OUT_QUAD;
        case 3: return fl::EASE_IN_OUT_QUAD;
        case 4: return fl::EASE_IN_CUBIC;
        case 5: return fl::EASE_OUT_CUBIC;
        case 6: return fl::EASE_IN_OUT_CUBIC;
        case 7: return fl::EASE_IN_SINE;
        case 8: return fl::EASE_OUT_SINE;
        case 9: return fl::EASE_IN_OUT_SINE;
    }
    FL_ASSERT(false, "Invalid ease type");
    return fl::EASE_NONE;
}*/

inline uint8_t cEaseSat = 0;
inline uint8_t cEaseLum = 0;

// ═══════════════════════════════════════════════════════════════════
//  PARAMETER DECLARATIONS
// ═══════════════════════════════════════════════════════════════════

// GLOBAL -------------------------
inline float cGlobalSpeed = 1.0f;
inline float cPersistence = 0.0f;
inline float cPersistFine = 0.05f;
inline float cColorShift = 0.10f;
inline bool cUseRainbow = false;

// EMITTERS -----------------------

// Dot family shared ------
inline float cNumDots = 3.0f;
inline float cDotDiam = 1.5f;
// orbitalDots
inline float cOrbitSpeed = 0.15f;
inline float cOrbitDiam = 10.0f;
inline float cModOrbitSpeedRate = 0.00005f;
inline float cModOrbitSpeedLevel = 1.0f;
inline float cModOrbitDiamRate = 0.0005f;
inline float cModOrbitDiamLevel = 1.0f;
// swarmingDots
inline float cSwarmSpeed = 0.5f;
inline float cSwarmSpread = 0.5f;
inline float cModSwarmSpeedRate = 1.0f;
inline float cModSwarmSpeedLevel = 0.0f;
inline float cModSwarmSpreadRate = 1.0f;
inline float cModSwarmSpreadLevel = 1.0f;
// lissajous line
inline float cLineSpeed = 0.35f;
inline float cLineAmp = 13.5f;
inline float cLineClamp = 0.0f;
inline float cModLineSpeedRate = 1.0f;
inline float cModLineSpeedLevel = 0.0f;
inline float cModLineAmpRate = 0.5f;
inline float cModLineAmpLevel = 0.0f;
//noiseKaleido
inline float cDriftSpeed = 0.35f;
inline float cNoiseScale = 0.0375f;
inline float cNoiseBand = 0.1f;
inline float cKaleidoGamma = 0.65f;
// cube
inline float cScale = 1.f;
inline float cRotateSpeedX = 0.6f;
inline float cRotateSpeedY = 0.9f;
inline float cRotateSpeedZ = 0.3f;
inline bool cAxisFreezeX = false;
inline bool cAxisFreezeY = false;
inline bool cAxisFreezeZ = false;
inline float cModScaleRate = 0.5f;
inline float cModScaleLevel = 0.0f;
inline float cModRotateSpeedXRate = 0.5f;
inline float cModRotateSpeedXLevel = 0.0f;
inline float cModRotateSpeedYRate = 0.5f;
inline float cModRotateSpeedYLevel = 0.0f;
inline float cModRotateSpeedZRate = 0.5f;
inline float cModRotateSpeedZLevel = 0.0f;
// fluidJet
inline float cJetDensity = 60.0f;
inline float cJetForce = 0.35f;
inline float cJetRadius = 2.0f;
inline float cJetSpread = 1.0f;
inline float cJetAngle = 0.0f;
inline float cJetHueSpeed = 0.69f;
inline float cModJetForceRate = 0.5f;
inline float cModJetForceLevel = 0.0f;
inline float cModAngleRate = 0.5f;
inline float cModAngleLevel = 0.0f;

// FLOWS -----------------------
// shared
inline float cBlendFactor = 0.45f;
// noiseFlow
inline float cXFreq = 0.33f;
inline float cYFreq = 0.32f;
inline float cXShift = 1.5f;
inline float cYShift = 1.5f;
inline float cXAmp = 1.0f;
inline float cYAmp = 1.0f;
inline float cXSpeed = 0.15f;
inline float cYSpeed = 0.15f;
inline float cModAmpRate = 0.5f;
inline float cModAmpLevel = 0.5f;
inline float cModSpeedRate = 0.1f;
inline float cModSpeedLevel = 0.1f;
inline float cModShiftRate = 0.5f;
inline float cModShiftLevel = 0.5f;
//radialFlow
inline float cRadialStep = 0.15f;
// directionalFlow
inline float cWindStep = 0.95f;
inline float cRotateSpeed = 0.25f;
inline float cWaveAmp = 0.0f;
inline float cWaveFreq = 0.20f;
inline float cWaveSpeed = 1.20f;
// ringFlow
inline float cInnerSwirl = -0.2f;
inline float cOuterSwirl = 0.2f;
inline float cMidDrift = 0.3f;
inline float cModBreatheRate = 1.0f;
inline float cModBreatheLevel = 1.0f;
// spiral
inline float cAngularStep = 0.28f;
inline bool cOutward = false;
inline float cModAngularStepRate = 0.5f;
inline float cModAngularStepLevel = 0.5f;
inline float cModRadialStepRate = 0.5f;
inline float cModRadialStepLevel = 0.5f;
inline float cModBlendFactorRate = 0.5f;
inline float cModBlendFactorLevel = 0.5f;
// fluid
inline float cViscosity = 0.0f;
inline float cDiffusion = 0.0f;
inline float cVelocityDissipation = 0.5f;
inline float cDyeDissipation = 0.5f;
inline float cVorticity = 7.0f;
inline float cGravity = 0.3f;
inline float cSolverIterations = 5.0f;
inline float cModVelDissipRate = 0.5f;
inline float cModVelDissipLevel = 0.0f;
inline float cModDyeDissipRate = 0.5f;
inline float cModDyeDissipLevel = 0.0f;

// AUDIO -----------------------
inline bool maxBins = false;
inline uint16_t cNoiseGateOpen = 70;
inline uint16_t cNoiseGateClose = 50;
inline float cAudioGain = 1.0f;
inline float cAudioFloor = 0.0f;
inline bool autoFloor = false;
inline float cAutoFloorAlpha = 0.01f;
inline float cAutoFloorMin = 0.0f;
inline float cAutoFloorMax = 0.5f;
inline bool avLeveler = true;
inline float cAvLevelerTarget = 0.5f;
inline float cThreshold = 0.40f;
inline float cMinBeatInterval = 75.f;
inline float cRampAttack = 0.f;
inline float cRampDecay = 100.f;
inline float cPeakBase = 1.0f;
inline float cExpDecayFactor = 0.9f;

// ═══════════════════════════════════════════════════════════════════
//  X-MACRO PARAMETER TABLE
// ═══════════════════════════════════════════════════════════════════

#define PARAMETER_TABLE \
   X(uint8_t, OverrideMapping, 0) \
   X(float, AudioGain, 1.0f) \
   X(float, AvLevelerTarget, 0.5f) \
   X(float, AudioFloor, 0.05f) \
   X(float, AutoFloorAlpha, 0.05f) \
   X(float, AutoFloorMin, 0.0f) \
   X(float, AutoFloorMax, 0.05f) \
   X(uint16_t, NoiseGateOpen, 70) \
   X(uint16_t, NoiseGateClose, 50) \
   X(float, Threshold, 0.25f) \
   X(float, MinBeatInterval, 75.0f) \
   X(float, RampAttack, 0.f) \
   X(float, RampDecay, 150.f) \
   X(float, PeakBase, 1.0f) \
   X(float, ExpDecayFactor, 1.0f) \
   X(float, OrbitSpeed, 0.35f) \
   X(float, GlobalSpeed, 1.0f) \
   X(float, Persistence, 0.0f) \
   X(float, PersistFine, 0.05f) \
   X(float, XShift, 1.8f) \
   X(float, YShift, 1.8f) \
   X(float, OrbitDiam, 10.0f) \
   X(float, ModOrbitSpeedRate, 0.00005f) \
   X(float, ModOrbitSpeedLevel, 1.0f) \
   X(float, ModOrbitDiamRate, 0.0005f) \
   X(float, ModOrbitDiamLevel, 1.0f) \
   X(float, NumDots, 3.0f) \
   X(float, DotDiam, 1.5f) \
   X(float, SwarmSpeed, 0.5f) \
   X(float, SwarmSpread, 0.5f) \
   X(float, ModSwarmSpeedRate, 1.0f) \
   X(float, ModSwarmSpeedLevel, 0.0f) \
   X(float, ModSwarmSpreadRate, 1.0f) \
   X(float, ModSwarmSpreadLevel, 1.0f) \
   X(float, LineSpeed, 0.35f) \
   X(float, LineClamp, 0.0f) \
   X(float, ModLineSpeedRate, 1.0f) \
   X(float, ModLineSpeedLevel, 0.0f) \
   X(float, ModLineAmpRate, 0.5f) \
   X(float, ModLineAmpLevel, 0.0f) \
   X(float, DriftSpeed, 0.35f) \
   X(float, NoiseScale, 0.0375f) \
   X(float, NoiseBand, 0.1f) \
   X(float, KaleidoGamma, 0.65f) \
   X(float, ColorShift, 0.10f) \
   X(float, LineAmp, 13.5f) \
   X(float, XFreq, 0.33f) \
   X(float, YFreq, 0.32f) \
   X(float, XSpeed, -0.25f) \
   X(float, YSpeed, -0.25f) \
   X(float, XAmp, 1.0f) \
   X(float, YAmp, 1.0f) \
   X(float, ModAmpRate, 1.0f) \
   X(float, ModAmpLevel, 1.0f) \
   X(float, ModSpeedRate, 1.0f) \
   X(float, ModSpeedLevel, 0.0f) \
   X(float, ModShiftRate, 1.0f) \
   X(float, ModShiftLevel, 0.0f) \
   X(float, RadialStep, 0.18f) \
   X(float, BlendFactor, 0.45f) \
   X(float, WindStep, 0.95f) \
   X(float, RotateSpeed, 0.25f) \
   X(float, WaveAmp, 0.0f) \
   X(float, WaveFreq, 0.20f) \
   X(float, WaveSpeed, 1.20f) \
   X(float, InnerSwirl, -0.26f) \
   X(float, OuterSwirl, 0.24f) \
   X(float, MidDrift, 0.42f) \
   X(float, ModBreatheRate, 1.0f) \
   X(float, ModBreatheLevel, 1.0f) \
   X(float, Scale, 1.0f) \
   X(float, RotateSpeedX, 0.6f) \
   X(float, RotateSpeedY, 0.9f) \
   X(float, RotateSpeedZ, 0.3f) \
   X(bool, AxisFreezeX, false) \
   X(bool, AxisFreezeY, false) \
   X(bool, AxisFreezeZ, false) \
   X(float, ModScaleRate, 0.5f) \
   X(float, ModScaleLevel, 0.0f) \
   X(float, ModRotateSpeedXRate, 0.5f) \
   X(float, ModRotateSpeedXLevel, 0.0f) \
   X(float, ModRotateSpeedYRate, 0.5f) \
   X(float, ModRotateSpeedYLevel, 0.0f) \
   X(float, ModRotateSpeedZRate, 0.5f) \
   X(float, ModRotateSpeedZLevel, 0.0f) \
   X(float, AngularStep, 0.28f) \
   X(bool, Outward, false) \
   X(float, ModAngularStepRate, 0.5f) \
   X(float, ModAngularStepLevel, 0.5f) \
   X(float, ModRadialStepRate, 0.5f) \
   X(float, ModRadialStepLevel, 0.5f) \
   X(float, ModBlendFactorRate, 0.5f) \
   X(float, ModBlendFactorLevel, 0.5f) \
   X(float, JetDensity, 50.0f) \
   X(float, JetForce, 0.25f) \
   X(float, JetRadius, 2.0f) \
   X(float, JetSpread, 1.0f) \
   X(float, JetAngle, 0.0f) \
   X(float, JetHueSpeed, 0.7f) \
   X(float, ModJetForceRate, 0.3f) \
   X(float, ModJetForceLevel, 0.1f) \
   X(float, ModAngleRate, 0.3f) \
   X(float, ModAngleLevel, 2.0f) \
   X(float, Viscosity, 0.0005f) \
   X(float, Diffusion, 0.0005f) \
   X(float, VelocityDissipation, 0.75f) \
   X(float, DyeDissipation, 0.25f) \
   X(float, Vorticity, 7.0f) \
   X(float, Gravity, 0.3f) \
   X(float, SolverIterations, 5.0f) \
   X(float, ModVelDissipRate, 0.5f) \
   X(float, ModVelDissipLevel, 0.0f) \
   X(float, ModDyeDissipRate, 0.5f) \
   X(float, ModDyeDissipLevel, 0.0f)
