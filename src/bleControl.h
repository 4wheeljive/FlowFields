#pragma once

#include <NimBLEDevice.h>
#include "parameterSchema.h"

#if __has_include("hosted_ble_bridge.h")
    #include "hosted_ble_bridge.h"
#else
    static inline bool hostedBlePrepare() { return true; }
#endif

//#include <FS.h>
//#include "LittleFS.h"
//#define FORMAT_LITTLEFS_IF_FAILED true

ArduinoJson::JsonDocument sendDoc;
ArduinoJson::JsonDocument receivedJSON;

//*************************************************************************************
//BLE CONFIGURATION *******************************************************************

NimBLEServer* pServer = NULL;
NimBLECharacteristic* pButtonCharacteristic = NULL;
NimBLECharacteristic* pCheckboxCharacteristic = NULL;
NimBLECharacteristic* pNumberCharacteristic = NULL;
NimBLECharacteristic* pStringCharacteristic = NULL;
NimBLEAdvertising* pAdvertising = NULL;

bool deviceConnected = false;
bool wasConnected = false;

#define SERVICE_UUID                  	"19b10000-e8f2-537e-4f6c-d104768a1214"
#define BUTTON_CHARACTERISTIC_UUID     "19b10001-e8f2-537e-4f6c-d104768a1214"
#define CHECKBOX_CHARACTERISTIC_UUID   "19b10002-e8f2-537e-4f6c-d104768a1214"
#define NUMBER_CHARACTERISTIC_UUID     "19b10003-e8f2-537e-4f6c-d104768a1214"
#define STRING_CHARACTERISTIC_UUID     "19b10004-e8f2-537e-4f6c-d104768a1214"


//*************************************************************************************
// CONTROL FUNCTIONS ******************************************************************

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

//*****************************************************************************
// PARAMETER/PRESET MANAGEMENT SYSTEM ("PPMS")

void startingPalette() {
   if (gGradientPaletteCount == 0) {
      return;
   }

   gCurrentPaletteNumber = random(0, gGradientPaletteCount);
   gTargetPaletteNumber = gCurrentPaletteNumber;
   gCurrentPalette = gGradientPalettes[gCurrentPaletteNumber];
   gTargetPalette = gCurrentPalette;
}

void setTargetPalette(uint8_t paletteNumber) {
   if (gGradientPaletteCount == 0) {
      return;
   }

   gTargetPaletteNumber = paletteNumber % gGradientPaletteCount;
   gTargetPalette = gGradientPalettes[gTargetPaletteNumber];
}

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

   //if (receivedValue == 160) { Trigger = true; }

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


   if (receivedID == "inPalNum") {
      uint8_t newPalNum = receivedValue;
      setTargetPalette(newPalNum);
      if(debug) {
         Serial.print("newPalNum: ");
         Serial.println(newPalNum);
      }
   };

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

   if (receivedID == "cx21") {cAxisFreezeX = receivedValue;};
   if (receivedID == "cx22") {cAxisFreezeY = receivedValue;};
   if (receivedID == "cx23") {cAxisFreezeZ = receivedValue;};

   if (receivedID == "cx31") {cOutward = receivedValue;};
   if (receivedID == "cx32") {cUseRainbow = receivedValue;};
   if (receivedID == "cx33") {cPaletteMode = receivedValue;};
   if (receivedID == "cx34") {cRotatePalette = receivedValue;};

}

void processString(String receivedID, String receivedValue ) {
   sendReceiptString(receivedID, receivedValue);
}

//*******************************************************************************
// CALLBACKS ********************************************************************

   class MyServerCallbacks: public NimBLEServerCallbacks {
   void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
      deviceConnected = true;
      wasConnected = true;
      Serial.println("[ble] connected");
      if (debug) {Serial.println("Device Connected");}
   };

   void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      deviceConnected = false;
      wasConnected = true;
      Serial.printf("[ble] disconnected reason=%d\n", reason);
   }
   };

   class ButtonCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         NimBLEAttValue value = pCharacteristic->getValue();
         if (value.size() > 0) {

            uint8_t receivedValue = value[0];

            if (debug) {
               Serial.print("Button value received: ");
               Serial.println(receivedValue);
            }

            processButton(receivedValue);

         }
      }
   };

   class CheckboxCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

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

   class NumberCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

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

   class StringCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

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

      if (!hostedBlePrepare()) {
         Serial.println("[ble] Hosted BLE not ready; BLE setup aborted.");
         return;
      }

      NimBLEDevice::init("Flow Fields");
      NimBLEDevice::setMTU(517);  // Request max MTU for larger JSON payloads

      pServer = NimBLEDevice::createServer();
      pServer->setCallbacks(new MyServerCallbacks());

      NimBLEService *pService = pServer->createService(SERVICE_UUID);

      pButtonCharacteristic = pService->createCharacteristic(
                        BUTTON_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pButtonCharacteristic->setCallbacks(new ButtonCharacteristicCallbacks());

      pCheckboxCharacteristic = pService->createCharacteristic(
                        CHECKBOX_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pCheckboxCharacteristic->setCallbacks(new CheckboxCharacteristicCallbacks());

      pNumberCharacteristic = pService->createCharacteristic(
                        NUMBER_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pNumberCharacteristic->setCallbacks(new NumberCharacteristicCallbacks());

      pStringCharacteristic = pService->createCharacteristic(
                        STRING_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pStringCharacteristic->setCallbacks(new StringCharacteristicCallbacks());


      //**********************************************************

      pAdvertising = NimBLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(SERVICE_UUID);

      // Set up advertisement data with device name for Web Bluetooth compatibility
      NimBLEAdvertisementData advertisementData;
      advertisementData.setName("Flow Fields");
      advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
      pAdvertising->setAdvertisementData(advertisementData);

      // Set up scan response data
      NimBLEAdvertisementData scanResponseData;
      scanResponseData.setName("Flow Fields");
      pAdvertising->setScanResponseData(scanResponseData);

      pAdvertising->start();
      if (debug) {Serial.println("Waiting a client connection to notify...");}
}
