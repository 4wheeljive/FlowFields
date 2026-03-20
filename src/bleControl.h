#pragma once

#include "FastLED.h"

#include <ArduinoJson.h>

/* If you use more than ~4 characteristics, you need to increase numHandles in this file:
C:\Users\...\.platformio\packages\framework-arduinoespressif32\libraries\BLE\src\BLEServer.h
Setting numHandles = 60 has worked for 7 characteristics.  
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <string>

#include "componentEnums.h"

#include <FS.h>
#include "LittleFS.h"
#define FORMAT_LITTLEFS_IF_FAILED true 

bool displayOn = true;

typedef void (*BusParamSetterFn)(uint8_t busId, const String& paramId, float value);
BusParamSetterFn setBusParam = nullptr;

// Callback to read a bus parameter value by busId and param name
typedef float (*BusParamGetterFn)(uint8_t busId, const String& paramName);
BusParamGetterFn getBusParam = nullptr;

extern uint8_t EMITTER;
extern uint8_t FLOW;

// GLOBAL PARAMETERS *******************************

const char* const GLOBAL_PARAMS[] PROGMEM = {
   "persistence", "colorShift", "flipY", "flipX"
};

const uint8_t GLOBAL_PARAM_COUNT = 4;

// EMITTERS ****************************************

// Emitter names in PROGMEM
const char orbitaldots_str[] PROGMEM = "orbitaldots";
const char swarmingdots_str[] PROGMEM = "swarmingdots";
const char lissajous_str[] PROGMEM = "lissajous";
const char borderrect_str[] PROGMEM = "borderrect";

const char* const EMITTERS[] PROGMEM = {
      orbitaldots_str, swarmingdots_str, lissajous_str, borderrect_str
   };

const uint8_t EMITTER_COUNTS[] = {4};

// Emitter params
const char* const ORBITALDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "orbitSpeed",  "orbitDiam"
};
const char* const SWARMINGDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "swarmSpeed", "swarmSpread"
};
const char* const LISSAJOUS_PARAMS[] PROGMEM = {
   "lineSpeed", "lineAmp"
};
const char* const BORDERRECT_PARAMS[] PROGMEM = {};

// Struct to hold emitter name and parameter array reference
struct EmitterParamEntry {
   const char* EmitterName;
   const char* const* params;
   uint8_t count;
};

const EmitterParamEntry EMITTER_PARAM_LOOKUP[] PROGMEM = {
   {"orbitaldots", ORBITALDOTS_PARAMS, 4},
   {"swarmingdots", SWARMINGDOTS_PARAMS, 4},
   {"lissajous", LISSAJOUS_PARAMS, 2},
   {"borderrect", BORDERRECT_PARAMS, 0}
};

static const EmitterParamEntry* getEmitterParams(uint8_t emitterIdx) {
      if (emitterIdx >= EMITTER_COUNT) return nullptr;
      return &EMITTER_PARAM_LOOKUP[emitterIdx];
}

// FLOW FIELDS ****************************************

// Flow names in PROGMEM
const char noise_str[] PROGMEM = "noise";
const char fromcenter_str[] PROGMEM = "fromcenter";
const char directional_str[] PROGMEM = "directional";

const uint8_t FLOW_COUNTS[] = {3};

const char* const FLOWS[] PROGMEM = {
      noise_str, fromcenter_str, directional_str
   };
   
// Flow field params
const char* const NOISE_PARAMS[] PROGMEM = {
   "xSpeed", "ySpeed", "xAmp", "yAmp","xFreq", "yFreq", "xShift", "yShift"
};
const char* const FROM_CENTER_PARAMS[] PROGMEM = {
   "radialStep", "blendFactor"
};
const char* const DIRECTIONAL_PARAMS[] PROGMEM = {
   "windStep", "blendFactor", "rotateSpeed", "waveAmp", "waveFreq", "waveSpeed"
};

// Struct to hold flow field name and parameter array reference
struct FlowParamEntry {
   const char* FlowName;
   const char* const* params;
   uint8_t count;
};

const FlowParamEntry FLOW_PARAM_LOOKUP[] PROGMEM = {
   {"noise", NOISE_PARAMS, 5},
   {"fromcenter", FROM_CENTER_PARAMS, 2},
   {"directional", DIRECTIONAL_PARAMS, 6}
};

static const FlowParamEntry* getFlowParams(uint8_t flowIdx) {
      if (flowIdx >= FLOW_COUNT) return nullptr;
      return &FLOW_PARAM_LOOKUP[flowIdx];
}

// MODULATOR PARAMS **********************************
const char* const MODULATOR_PARAMS[] PROGMEM = {
      "variationIntensity", "variationSpeed", "modulateAmp"
};   

// AUDIO SETTINGS ==================================================

const char* const AUDIO_PARAMS[] PROGMEM = {
"maxBins", "audioFloor", "audioGain",
"avLevelerTarget", "autoFloorAlpha", "autoFloorMin", "autoFloorMax",
"noiseGateOpen", "noiseGateClose",
"threshold", "minBeatInterval",
"rampAttack", "rampDecay", "peakBase", "expDecayFactor"
};

const uint8_t AUDIO_PARAM_COUNT = 15;

   
// Parameter control *************************************************************************************

uint8_t cBright = 75;
uint8_t cMapping = 0;
uint8_t cOverrideMapping = 0;

fl::EaseType getEaseType(uint8_t value) {
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
}

uint8_t cEaseSat = 0;
uint8_t cEaseLum = 0;

// PARAMETER CONTROLS ==================================================================

// Audio
bool maxBins = false;
uint16_t cNoiseGateOpen = 70;
uint16_t cNoiseGateClose = 50;
float cAudioGain = 1.0f;      // Unified gain (internally maps to level × GAIN_SCALE_LEVEL, FFT × GAIN_SCALE_FFT)
float cAudioFloor = 0.0f;     // Unified audio floor (internally maps to level × 0.05, FFT × 0.3)
bool autoFloor = false;
float cAutoFloorAlpha = 0.01f;
float cAutoFloorMin = 0.0f;
float cAutoFloorMax = 0.5f;
bool avLeveler = true;
float cAvLevelerTarget = 0.5f;
float cThreshold = 0.40f;
float cMinBeatInterval = 75.f;
float cRampAttack = 0.f;
float cRampDecay = 100.f;
float cPeakBase = 1.0f;
float cExpDecayFactor = 0.9f;

//ColorTrails
float cPersistence = 14.8f;
float cXFreq = 0.33f;
float cYFreq = 0.33f;
float cOrbitSpeed = 0.35f;
float cXShift = 1.8f;
float cYShift = 1.8f;
float cOrbitDiam = 10.0f;
float cColorSpeed = 0.10f;
uint8_t cNumDots = 3;
float cDotDiam = 1.5f;
float cSwarmSpeed = 0.5f;
float cSwarmSpread = 1.0f;
float cLineSpeed = 0.35f;
float cColorShift = 0.10f;
float cLineAmp = 13.5f;
float cXAmp = 1.0f;
float cYAmp = 1.0f;
float cXSpeed = -1.73f;
float cYSpeed = -1.72f;
float cVariationIntensity = 4.0f;
float cVariationSpeed = 1.0f;
uint8_t cModulateAmp = 1;
bool cFlipY = false;
bool cFlipX = false;
float cRadialStep = 0.18f;
float cBlendFactor = 0.45f;
float cWindStep = 0.95f;
float cRotateSpeed = 0.25f;
float cWaveAmp = 0.0f;
float cWaveFreq = 0.20f;
float cWaveSpeed = 1.20f;

ArduinoJson::JsonDocument sendDoc;
ArduinoJson::JsonDocument receivedJSON;

//*******************************************************************************
//BLE CONFIGURATION *************************************************************

BLEServer* pServer = NULL;
BLECharacteristic* pButtonCharacteristic = NULL;
BLECharacteristic* pCheckboxCharacteristic = NULL;
BLECharacteristic* pNumberCharacteristic = NULL;
BLECharacteristic* pStringCharacteristic = NULL;

bool deviceConnected = false;
bool wasConnected = false;

#define SERVICE_UUID                  	"19b10000-e8f2-537e-4f6c-d104768a1214"
#define BUTTON_CHARACTERISTIC_UUID     "19b10001-e8f2-537e-4f6c-d104768a1214"
#define CHECKBOX_CHARACTERISTIC_UUID   "19b10002-e8f2-537e-4f6c-d104768a1214"
#define NUMBER_CHARACTERISTIC_UUID     "19b10003-e8f2-537e-4f6c-d104768a1214"
#define STRING_CHARACTERISTIC_UUID     "19b10004-e8f2-537e-4f6c-d104768a1214"

BLEDescriptor pButtonDescriptor(BLEUUID((uint16_t)0x2902));
BLEDescriptor pCheckboxDescriptor(BLEUUID((uint16_t)0x2902));
BLEDescriptor pNumberDescriptor(BLEUUID((uint16_t)0x2902));
BLEDescriptor pStringDescriptor(BLEUUID((uint16_t)0x2902));

//*******************************************************************************
// CONTROL FUNCTIONS ************************************************************

// UI update functions ***********************************************

void sendReceiptButton(uint8_t receivedValue) {
   pButtonCharacteristic->setValue(String(receivedValue).c_str());
   pButtonCharacteristic->notify();
   if (debug) {
      Serial.print("Button value received: ");
      Serial.println(receivedValue);
   }
}

void sendReceiptCheckbox(String receivedID, bool receivedValue) {
  
   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pCheckboxCharacteristic->setValue(jsonString);
   
   pCheckboxCharacteristic->notify();
   
   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }

}

void sendReceiptNumber(String receivedID, float receivedValue) {

   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pNumberCharacteristic->setValue(jsonString);
   
   pNumberCharacteristic->notify();
   
   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }
}

void sendReceiptString(String receivedID, String receivedValue) {

   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pStringCharacteristic->setValue(jsonString);

   pStringCharacteristic->notify();
   
   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }
}

//***********************************************************************
// PARAMETER/PRESET MANAGEMENT SYSTEM ("PPMS")
// X-Macro table 
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
   X(float, Persistence, 14.8f) \
   X(float, XShift, 1.8f) \
   X(float, YShift, 1.8f) \
   X(float, OrbitDiam, 10.0f) \
   X(float, ColorSpeed, 0.10f) \
   X(uint8_t, NumDots, 3) \
   X(float, DotDiam, 1.5f) \
   X(float, SwarmSpeed, 0.5f) \
   X(float, SwarmSpread, 1.0f) \
   X(float, LineSpeed, 0.35f) \
   X(float, ColorShift, 0.10f) \
   X(float, LineAmp, 13.5f) \
   X(float, XFreq, 0.33f) \
   X(float, YFreq, 0.33f) \
   X(float, XSpeed, -1.73f) \
   X(float, YSpeed, -1.72f) \
   X(float, XAmp, 1.0f) \
   X(float, YAmp, 1.0f) \
   X(float, VariationIntensity, 4.0f) \
   X(float, VariationSpeed, 1.0f) \
   X(uint8_t, ModulateAmp, 1) \
   X(float, RadialStep, 0.18f) \
   X(float, BlendFactor, 0.45f) \
   X(float, WindStep, 0.95f) \
   X(float, RotateSpeed, 0.25f) \
   X(float, WaveAmp, 0.0f) \
   X(float, WaveFreq, 0.20f) \
   X(float, WaveSpeed, 1.20f)


// Auto-generated helper functions using X-macros
void captureCurrentParameters(ArduinoJson::JsonObject& params) {
    #define X(type, parameter, def) params[#parameter] = c##parameter;
    PARAMETER_TABLE
    #undef X
}

void applyCurrentParameters(const ArduinoJson::JsonObjectConst& params) {
    #define X(type, parameter, def) \
        if (!params[#parameter].isNull()) { \
            auto newValue = params[#parameter].as<type>(); \
            if (c##parameter != newValue) { \
                c##parameter = newValue; \
                sendReceiptNumber("in" #parameter, c##parameter); \
            } \
        }
    PARAMETER_TABLE
    #undef X
}

/*
// Preset file persistence functions with JSON structure
bool savePreset(int presetNumber) {
    String filename = "/preset_";
    filename += presetNumber;
    filename += ".json";
    
    ArduinoJson::JsonDocument preset;
    preset["programNum"] = PROGRAM;
    if (MODE_COUNTS[PROGRAM] > 0) { 
      preset["modeNum"] = MODE;
    }    
    ArduinoJson::JsonObject params = preset["parameters"].to<ArduinoJson::JsonObject>();
    captureCurrentParameters(params);
    
    File file = LittleFS.open(filename, "w");
    if (!file) {
        Serial.print("Failed to save preset: ");
        Serial.println(filename);
        return false;
    }
    
    serializeJson(preset, file);
    file.close();
    
    Serial.print("Preset saved: ");
    Serial.println(filename);
    return true;
}

bool loadPreset(int presetNumber) {
    String filename = "/preset_";
    filename += presetNumber;
    filename += ".json";
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.print("Failed to load preset: ");
        Serial.println(filename);
        return false;
    }
    
    ArduinoJson::JsonDocument preset;
    ArduinoJson::DeserializationError error = deserializeJson(preset, file);
    file.close();
    
    if (preset["programNum"].isNull() || preset["parameters"].isNull()) {
        Serial.print("Invalid preset format: ");
        Serial.println(filename);
        return false;
    }

    PROGRAM = (uint8_t)preset["programNum"];
    if (!preset["modeNum"].isNull()) {
      MODE = (uint8_t)preset["modeNum"];
    }
    //pauseAnimation = true;
    applyCurrentParameters(preset["parameters"]);
    //pauseAnimation = false;
    
    Serial.print("Preset loaded: ");
    Serial.println(filename);
    return true;
}*/

//***********************************************************************

//void sendDeviceState() {

void sendEmitterState() { 
   if (debug) {
      Serial.println("Sending emitter state...");
   }
   
   ArduinoJson::JsonDocument stateDoc;
   stateDoc["emitter"] = EMITTER;
   
   // Get parameter list for current visualizer
   const EmitterParamEntry* emitterParams = getEmitterParams(EMITTER);

   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   if (debug) {
       Serial.print("Current emitter: ");
       Serial.println(EMITTER);
       Serial.print("Found params: ");
       Serial.println(emitterParams != nullptr ? "YES" : "NO");
       if (emitterParams != nullptr) {
           Serial.print("Param count: ");
           Serial.println(emitterParams->count);
       }
   }
   
   if (emitterParams != nullptr) {
       // Loop through parameters for current emitter
       for (uint8_t i = 0; i < emitterParams->count; i++) {
           char paramName[32];
           ::strcpy(paramName, (char*)pgm_read_ptr(&emitterParams->params[i]));
           
           if (debug) {
               Serial.print("Processing parameter: ");
               Serial.println(paramName);
           }
       }
   }

   // Add parameter values to JSON based on visualizer params
   for (uint8_t i = 0; i < emitterParams->count; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&emitterParams->params[i]));
       
       bool paramFound = false;
       // Use X-macro to match parameter names and add values
       // Handle case-insensitive comparison for parameter names
       #define X(type, parameter, def) \
           if (strcasecmp(paramName, #parameter) == 0) { \
               params[paramName] = c##parameter; \
               if (debug) { \
                   Serial.print("Added parameter "); \
                   Serial.print(paramName); \
                   Serial.print(": "); \
                   Serial.println(c##parameter); \
               } \
               paramFound = true; \
           }
       PARAMETER_TABLE
       #undef X
       
       if (!paramFound) {
           Serial.print("Warning: Parameter not found in X-macro table: ");
           Serial.println(paramName);
       }
   }
   
   // Send as a single JSON doc with nested val object (avoids double-encoding
   // that would exceed BLE MTU when string-escaping the inner JSON).
   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "emitterState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   val["emitter"] = EMITTER;
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("emitterState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}

  // -----------------------------------

void sendFlowState() { 
   if (debug) {
      Serial.println("Sending flow state...");
   }
   
   ArduinoJson::JsonDocument stateDoc;
   stateDoc["flow"] = FLOW;
   
   // Get parameter list for current flow
   const FlowParamEntry* flowParams = getFlowParams(FLOW);

   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   if (debug) {
       Serial.print("Current emitter: ");
       Serial.println(FLOW);
       Serial.print("Found params: ");
       Serial.println(flowParams != nullptr ? "YES" : "NO");
       if (flowParams != nullptr) {
           Serial.print("Param count: ");
           Serial.println(flowParams->count);
       }
   }
   
   if (flowParams != nullptr) {
       // Loop through parameters for current flow
       for (uint8_t i = 0; i < flowParams->count; i++) {
           char paramName[32];
           ::strcpy(paramName, (char*)pgm_read_ptr(&flowParams->params[i]));
           
           if (debug) {
               Serial.print("Processing parameter: ");
               Serial.println(paramName);
           }
       }
   }

   // Add parameter values to JSON based on visualizer params
   for (uint8_t i = 0; i < flowParams->count; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&flowParams->params[i]));
       
       bool paramFound = false;
       // Use X-macro to match parameter names and add values
       // Handle case-insensitive comparison for parameter names
       #define X(type, parameter, def) \
           if (strcasecmp(paramName, #parameter) == 0) { \
               params[paramName] = c##parameter; \
               if (debug) { \
                   Serial.print("Added parameter "); \
                   Serial.print(paramName); \
                   Serial.print(": "); \
                   Serial.println(c##parameter); \
               } \
               paramFound = true; \
           }
       PARAMETER_TABLE
       #undef X
       
       if (!paramFound) {
           Serial.print("Warning: Parameter not found in X-macro table: ");
           Serial.println(paramName);
       }
   }
   
   // Send as a single JSON doc with nested val object (avoids double-encoding
   // that would exceed BLE MTU when string-escaping the inner JSON).
   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "flowState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   val["flow"] = FLOW;
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("flowState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}


void sendGlobalState() {
   if (debug) { Serial.println("Sending global state..."); }

   ArduinoJson::JsonDocument stateDoc;
   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   for (uint8_t i = 0; i < GLOBAL_PARAM_COUNT; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&GLOBAL_PARAMS[i]));

       bool paramFound = false;
       #define X(type, parameter, def) \
           if (strcasecmp(paramName, #parameter) == 0) { \
               params[paramName] = c##parameter; \
               paramFound = true; \
           }
       PARAMETER_TABLE
       #undef X

       if (!paramFound) {
           Serial.print("Warning: Global param not found: ");
           Serial.println(paramName);
       }
   }

   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "globalState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("globalState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}

  // -----------------------------------

void sendAudioState() {
   if (debug) { Serial.println("Sending audio state..."); }

   ArduinoJson::JsonDocument stateDoc;
   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   // Iterate AUDIO_PARAMS and match against PARAMETER_TABLE via X-macro
   for (uint8_t i = 0; i < AUDIO_PARAM_COUNT; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&AUDIO_PARAMS[i]));

       #define X(type, parameter, def) \
           if (strcasecmp(paramName, #parameter) == 0) { \
               params[paramName] = c##parameter; \
           }
       PARAMETER_TABLE
       #undef X
   }

   String stateJson;
   serializeJson(stateDoc, stateJson);
   sendReceiptString("audioState", stateJson);
}

void sendBusState() {
   if (debug) { Serial.println("Sending bus state..."); }
   if (!getBusParam) { Serial.println("getBusParam not set"); return; }

   // Bus params live on Bus structs (outside X-macro system).
   // Send one message per bus to stay within BLE MTU limits.
   const char* busParamNames[] = {"threshold", "minBeatInterval", "expDecayFactor",
                                   "rampAttack", "rampDecay", "peakBase"};
   const uint8_t busParamCount = 6;

   for (uint8_t busId = 0; busId < 3; busId++) {
       ArduinoJson::JsonDocument stateDoc;
       stateDoc["bus"] = busId;
       ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();
       for (uint8_t p = 0; p < busParamCount; p++) {
           params[busParamNames[p]] = getBusParam(busId, busParamNames[p]);
       }

       String stateJson;
       serializeJson(stateDoc, stateJson);
       sendReceiptString("busState", stateJson);
   }
}


// Handle UI request functions ***********************************************

std::string convertToStdString(const String& flStr) {
   return std::string(flStr.c_str());
}

void processButton(uint8_t receivedValue) {

   sendReceiptButton(receivedValue);
      
   if (receivedValue < 20) { // Emitter selection
      EMITTER = receivedValue;
      displayOn = true;
   }
   
   if (receivedValue >= 20 && receivedValue < 40) { // Flow selection
      FLOW = receivedValue - 20;
      displayOn = true;
   }

   if (receivedValue == 91) { sendGlobalState(); }
   if (receivedValue == 92) { sendEmitterState(); }
   if (receivedValue == 93) { sendAudioState(); }
   if (receivedValue == 94) { sendBusState(); }
   //if (receivedValue == 95) { resetAll(); }
   if (receivedValue == 96) { sendFlowState(); }
   
   if (receivedValue == 98) { displayOn = true; }
   if (receivedValue == 99) { displayOn = false; }

   /*if (receivedValue >= 101 && receivedValue <= 120) { 
      uint8_t savedPreset = receivedValue - 100;  
      //savePreset(savedPreset); 
   }*/

   /*if (receivedValue >= 121 && receivedValue <= 140) {
       uint8_t presetToLoad = receivedValue - 120;
       if (loadPreset(presetToLoad)) {
           Serial.print("Loaded preset: ");
           Serial.println(presetToLoad);
       }
   }*/

   //fxWave2d, animartrix
   //if (receivedValue == 160) { fancyTrigger = true; }
   
}

//*****************************************************************************

void processNumber(String receivedID, float receivedValue, int8_t busId = -1) {

   sendReceiptNumber(receivedID, receivedValue);

   if (busId >= 0 && busId < 3 && setBusParam != nullptr) {
      setBusParam((uint8_t)busId, receivedID, receivedValue);
      return;
   }
  
   if (receivedID == "inBright") {
      cBright = receivedValue;
      BRIGHTNESS = cBright;
      FastLED.setBrightness(BRIGHTNESS);
   };


   /*
   if (receivedID == "inPalNum") {
      uint8_t newPalNum = receivedValue;
      gTargetPalette = gGradientPalettes[ newPalNum ];
      if(debug) {
         Serial.print("newPalNum: ");
         Serial.println(newPalNum);
      }
   };
   */
  
   //-------------------------------------------------------
   // Auto-generated custom parameter handling using X-macros
   #define X(type, parameter, def) \
       if (receivedID == "in" #parameter) { c##parameter = receivedValue; return; }
   PARAMETER_TABLE
   #undef X

}

void processCheckbox(String receivedID, bool receivedValue ) {
   
   sendReceiptCheckbox(receivedID, receivedValue);
   
   if (receivedID == "cx5") {audioEnabled = receivedValue;};
   if (receivedID == "cx6") {avLeveler = receivedValue;};
   if (receivedID == "cx7") {autoFloor = receivedValue;};
   
   if (receivedID == "cx11") {mappingOverride = receivedValue;};

   if (receivedID == "cx30") {cFlipY = receivedValue;};
   if (receivedID == "cx31") {cFlipX = receivedValue;};

}

void processString(String receivedID, String receivedValue ) {
   sendReceiptString(receivedID, receivedValue);
}

//*******************************************************************************
// CALLBACKS ********************************************************************

   class MyServerCallbacks: public BLEServerCallbacks {
   void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      wasConnected = true;
      if (debug) {Serial.println("Device Connected");}
   };

   void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      wasConnected = true;
   }
   };

   class ButtonCharacteristicCallbacks : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic *characteristic) {

         String value = characteristic->getValue();
         if (value.length() > 0) {
            
            uint8_t receivedValue = value[0];
            
            if (debug) {
               Serial.print("Button value received: ");
               Serial.println(receivedValue);
            }
            
            processButton(receivedValue);
         
         }
      }
   };

   class CheckboxCharacteristicCallbacks : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic *characteristic) {
   
         String receivedBuffer = characteristic->getValue();
   
         if (receivedBuffer.length() > 0) {
                     
            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }
         
            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            bool receivedValue = receivedJSON["val"];
         
            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }
         
            processCheckbox(receivedID, receivedValue);
         
         }
      }
   };

   class NumberCharacteristicCallbacks : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic *characteristic) {
         
         String receivedBuffer = characteristic->getValue();
         
         if (receivedBuffer.length() > 0) {
         
            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }
         
            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            float receivedValue = receivedJSON["val"];
            int8_t receivedBus = -1;
            if (!receivedJSON["bus"].isNull()) {
               receivedBus = receivedJSON["bus"].as<int8_t>();
            }

            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }

            processNumber(receivedID, receivedValue, receivedBus);
         }
      }
   };

   class StringCharacteristicCallbacks : public BLECharacteristicCallbacks {
      void onWrite(BLECharacteristic *characteristic) {
         
         String receivedBuffer = characteristic->getValue();
         
         if (receivedBuffer.length() > 0) {
         
            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }
         
            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            String receivedValue = receivedJSON["val"];
         
            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }
         
            processString(receivedID, receivedValue);
         }
      }
   };


//*******************************************************************************
// BLE SETUP FUNCTION ***********************************************************

uint8_t dummy = 1;

void bleSetup() {

      BLEDevice::init("Color Trails");
      BLEDevice::setMTU(517);  // Request max MTU for larger JSON payloads

      pServer = BLEDevice::createServer();
      pServer->setCallbacks(new MyServerCallbacks());

      BLEService *pService = pServer->createService(SERVICE_UUID);

      pButtonCharacteristic = pService->createCharacteristic(
                        BUTTON_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY
                     );
      pButtonCharacteristic->setCallbacks(new ButtonCharacteristicCallbacks());
      pButtonCharacteristic->setValue(String(dummy).c_str());

      pCheckboxCharacteristic = pService->createCharacteristic(
                        CHECKBOX_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY
                     );
      pCheckboxCharacteristic->setCallbacks(new CheckboxCharacteristicCallbacks());
      pCheckboxCharacteristic->setValue(String(dummy).c_str());
      
      pNumberCharacteristic = pService->createCharacteristic(
                        NUMBER_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY
                     );
      pNumberCharacteristic->setCallbacks(new NumberCharacteristicCallbacks());
      pNumberCharacteristic->setValue(String(dummy).c_str());

      pStringCharacteristic = pService->createCharacteristic(
                        STRING_CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_NOTIFY
                     );
      pStringCharacteristic->setCallbacks(new StringCharacteristicCallbacks());
      pStringCharacteristic->setValue(String(dummy).c_str());
      

      //**********************************************************

      pService->start();

      BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(SERVICE_UUID);
      pAdvertising->setScanResponse(true);  // Enable scan response for longer name
      pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
      BLEDevice::startAdvertising();
      if (debug) {Serial.println("Waiting a client connection to notify...");}
}
