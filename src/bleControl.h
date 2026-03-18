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

#include <FS.h>
#include "LittleFS.h"
#define FORMAT_LITTLEFS_IF_FAILED true 

bool displayOn = true;

typedef void (*BusParamSetterFn)(uint8_t busId, const String& paramId, float value);
BusParamSetterFn setBusParam = nullptr;

// Callback to read a bus parameter value by busId and param name
typedef float (*BusParamGetterFn)(uint8_t busId, const String& paramName);
BusParamGetterFn getBusParam = nullptr;

uint8_t dummy = 1;

extern uint8_t PROGRAM;
extern uint8_t MODE;

// PROGRAM/MODE FRAMEWORK ****************************************

  enum Program : uint8_t {
      COLORTRAILS = 0,
      PROGRAM_COUNT
  };

  // Program names in PROGMEM
  const char colortrails_str[] PROGMEM = "colortrails";

  const char* const PROGRAM_NAMES[] PROGMEM = {
      colortrails_str
  };

  // Mode names in PROGMEM
   const char orbital_str[] PROGMEM = "orbital";
   const char lissajous_str[] PROGMEM = "lissajous";
   const char borderrect_str[] PROGMEM = "borderrect";

   const char* const COLORTRAILS_MODES[] PROGMEM = {
         orbital_str, lissajous_str, borderrect_str
      };

   const uint8_t MODE_COUNTS[] = {3};

   // Visualizer parameter mappings - PROGMEM arrays for memory efficiency
   // Individual parameter arrays for each visualizer
   const char* const COLORTRAILS_PARAMS[] PROGMEM = {
       "fadeRate", "orbitSpeed", "colorSpeed", "circleDiam", "orbitDiam",
       "endpointSpeed", "colorShift", "lineAmplitude",
       "xSpeed", "ySpeed", "xAmplitude", "yAmplitude",
       "xFrequency", "yFrequency", "xShift", "yShift",
       "variationIntensity", "variationSpeed", "modulateAmplitude"
   };

   // Struct to hold visualizer name and parameter array reference
   struct VisualizerParamEntry {
      const char* visualizerName;
      const char* const* params;
      uint8_t count;
   };

   // String-based lookup table - mirrors JavaScript VISUALIZER_PARAMS
   // Can number values be replace by an array element count?
   const VisualizerParamEntry VISUALIZER_PARAM_LOOKUP[] PROGMEM = {
      {"colortrails-orbital", COLORTRAILS_PARAMS, 19},
      {"colortrails-lissajous", COLORTRAILS_PARAMS, 19},
      {"colortrails-borderrect", COLORTRAILS_PARAMS, 19}
   };

  class VisualizerManager {
  public:
      static String getVisualizerName(int programNum, int mode = -1) {
          if (programNum < 0 || programNum > PROGRAM_COUNT-1) return "";

          // Get program name from flash memory
          char progName[16];
          ::strcpy(progName,(char*)pgm_read_ptr(&PROGRAM_NAMES[programNum]));

          if (mode < 0 || MODE_COUNTS[programNum] == 0) {
              return String(progName);
          }

          // Get mode name
          const char* const* modeArray = nullptr;
          switch (programNum) {
              case COLORTRAILS: modeArray = COLORTRAILS_MODES; break;
              default: return String(progName);
          }

          if (mode >= MODE_COUNTS[programNum]) return String(progName);

          char modeName[20];
          ::strcpy(modeName,(char*)pgm_read_ptr(&modeArray[mode]));

         String result = "";
         result += String(progName);
         result += "-";
         result += String(modeName);
         return result;
      }
      
      // Get parameter list based on visualizer name
      static const VisualizerParamEntry* getVisualizerParams(const String& visualizerName) {
          const int LOOKUP_SIZE = sizeof(VISUALIZER_PARAM_LOOKUP) / sizeof(VisualizerParamEntry);
          
          for (int i = 0; i < LOOKUP_SIZE; i++) {
              char entryName[32];
              ::strcpy(entryName, (char*)pgm_read_ptr(&VISUALIZER_PARAM_LOOKUP[i].visualizerName));
              
              if (visualizerName.equals(entryName)) {
                  return &VISUALIZER_PARAM_LOOKUP[i];
              }
          }
          return nullptr;
      }
  };  // class VisualizerManager


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
float cFadeRate = 0.99922f;
float cXFrequency = 0.33f;
float cYFrequency = 0.33f;
float cOrbitSpeed = 0.35f;
float cXShift = 1.8f;
float cYShift = 1.8f;
float cOrbitDiam = 10.0f;
float cColorSpeed = 0.10f;
float cCircleDiam = 1.5f;
float cEndpointSpeed = 0.35f;
float cColorShift = 0.10f;
float cLineAmplitude = 13.5f;
float cXAmplitude = 1.0f;
float cYAmplitude = 1.0f;
float cXSpeed = -1.73f;
float cYSpeed = -1.72f;
float cVariationIntensity = 4.0f;
float cVariationSpeed = 1.0f;
uint8_t cModulateAmplitude = 1;


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

/*void startingPalette() {
   gCurrentPaletteNumber = random(0,gGradientPaletteCount-1);
   fl::CRGBPalette16 gCurrentPalette( gGradientPalettes[gCurrentPaletteNumber] );
   gTargetPaletteNumber = addmod8( gCurrentPaletteNumber, 1, gGradientPaletteCount);
   gTargetPalette = gGradientPalettes[ gCurrentPaletteNumber ];
}*/

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
   X(float, FadeRate, 0.99922f) \
   X(float, XShift, 1.8f) \
   X(float, YShift, 1.8f) \
   X(float, OrbitDiam, 10.0f) \
   X(float, ColorSpeed, 0.10f) \
   X(float, CircleDiam, 1.5f) \
   X(float, EndpointSpeed, 0.35f) \
   X(float, ColorShift, 0.10f) \
   X(float, LineAmplitude, 13.5f) \
   X(float, XFrequency, 0.33f) \
   X(float, YFrequency, 0.33f) \
   X(float, XSpeed, -1.73f) \
   X(float, YSpeed, -1.72f) \
   X(float, XAmplitude, 1.0f) \
   X(float, YAmplitude, 1.0f) \
   X(float, VariationIntensity, 4.0f) \
   X(float, VariationSpeed, 1.0f) \
   X(uint8_t, ModulateAmplitude, 1)


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
}

//***********************************************************************

//void sendDeviceState() {
void sendVisualizerState() { 
   if (debug) {
      Serial.println("Sending visualizer state...");
   }
   
   ArduinoJson::JsonDocument stateDoc;
   stateDoc["program"] = PROGRAM;
   stateDoc["mode"] = MODE;
   
   String currentVisualizer = VisualizerManager::getVisualizerName(PROGRAM, MODE); 
   
   // Get parameter list for current visualizer
   const VisualizerParamEntry* visualizerParams = VisualizerManager::getVisualizerParams(currentVisualizer);

   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   if (debug) {
       String currentVisualizer = VisualizerManager::getVisualizerName(PROGRAM, MODE);
       Serial.print("Current visualizer: ");
       Serial.println(currentVisualizer);
       Serial.print("Found params: ");
       Serial.println(visualizerParams != nullptr ? "YES" : "NO");
       if (visualizerParams != nullptr) {
           Serial.print("Param count: ");
           Serial.println(visualizerParams->count);
       }
   }
   
   if (visualizerParams != nullptr) {
       // Loop through parameters for current visualizer
       for (uint8_t i = 0; i < visualizerParams->count; i++) {
           char paramName[32];
           ::strcpy(paramName, (char*)pgm_read_ptr(&visualizerParams->params[i]));
           
           if (debug) {
               Serial.print("Processing parameter: ");
               Serial.println(paramName);
           }
       }
   }

   // Add parameter values to JSON based on visualizer params
   for (uint8_t i = 0; i < visualizerParams->count; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&visualizerParams->params[i]));
       
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
   envelope["id"] = "visualizerState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   val["program"] = PROGRAM;
   val["mode"] = MODE;
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("visualizerState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}


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
      
   if (receivedValue < 20) { // Program selection
      PROGRAM = receivedValue;
      MODE = 0;
      displayOn = true;
   }
   
   if (receivedValue >= 20 && receivedValue < 40) { // Mode selection
      MODE = receivedValue - 20;
      //cFxIndex = MODE;
      displayOn = true;
   }

   if (debug) {
      Serial.print("Current visualizer: ");
      Serial.println(VisualizerManager::getVisualizerName(PROGRAM, MODE));
   }

   if (receivedValue == 92) { sendVisualizerState(); }
   if (receivedValue == 93) { sendAudioState(); }
   if (receivedValue == 94) { sendBusState(); }
   //if (receivedValue == 95) { resetAll(); }
   
   if (receivedValue == 98) { displayOn = true; }
   if (receivedValue == 99) { displayOn = false; }

   if (receivedValue >= 101 && receivedValue <= 120) { 
      uint8_t savedPreset = receivedValue - 100;  
      savePreset(savedPreset); 
   }

   if (receivedValue >= 121 && receivedValue <= 140) {
       uint8_t presetToLoad = receivedValue - 120;
       if (loadPreset(presetToLoad)) {
           Serial.print("Loaded preset: ");
           Serial.println(presetToLoad);
       }
   }

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
