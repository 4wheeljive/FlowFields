//================================================================================================================
// CREDITS:
//  - FlowFields based on visualizer by Stefan Petrick first introduced here:
// 		https://www.reddit.com/r/FastLED/comments/1rny5j3/i_used_codex_for_the_first_time/
//===============================================================================================================
// cli command for P4 build/upload: 
// 	C:/Users/Jeff/.platformio/penv/Scripts/pio.exe run -c platformio_p4.ini -t upload
//=====================================================================================

#include <Arduino.h>

//#define FASTLED_OVERCLOCK 1.2
#include <FastLED.h>

//#include <FS.h>
//#include "LittleFS.h"
//#define FORMAT_LITTLEFS_IF_FAILED true

bool debug = false;
bool audioEnabled = false;
bool audioLatencyDiagnostics = false;

#include "profiler.h"
#ifdef PROFILING_ENABLED
	FrameProfiler profiler;
#endif

#include "boardConfig.h"

#include "reference/palettes.h"
extern const TProgmemRGBGradientPaletteRef gGradientPalettes[];
extern const uint8_t gGradientPaletteCount;
uint8_t gCurrentPaletteNumber;
uint8_t gTargetPaletteNumber;
fl::CRGBPalette16 gCurrentPalette;
fl::CRGBPalette16 gTargetPalette;

fl::CRGB leds[NUM_LEDS];
uint16_t ledNum = 0;

// ***************************************************************************************
// elements that must be set before #include "bleControl.h"

uint8_t EMITTER = 0;
uint8_t FLOW = 0;  // FLOW_NOISE; declared before enum is in scope
uint8_t BRIGHTNESS = 35;

uint8_t defaultMapping = 0;
bool mappingOverride = false;

#include "audio/audioInput.h"
#include "audio/audioProcessing.h"
#include "bleControl.h"
#include "flowFieldsEngine.hpp"

using namespace fl;

// **********************************************************************************

void setup() {

	Serial.begin(115200);
	#if defined(CONFIG_IDF_TARGET_ESP32S3)
		Serial.setTxTimeoutMs(1);  // S3-only: avoids unsigned underflow on USB CDC
	#endif
	delay(1000);

	FastLED.setExclusiveDriver(LED_DRIVER);

	#ifndef S3_15x38_2PIN 

		FastLED.addLeds<WS2812B, PIN0, GRB>(leds, 0, NUM_LEDS_PER_STRIP)
			.setCorrection(TypicalLEDStrip);

		#ifdef PIN1
			FastLED.addLeds<WS2812B, PIN1, GRB>(leds, NUM_LEDS_PER_STRIP, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN2
			FastLED.addLeds<WS2812B, PIN2, GRB>(leds, NUM_LEDS_PER_STRIP * 2, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN3
			FastLED.addLeds<WS2812B, PIN3, GRB>(leds, NUM_LEDS_PER_STRIP * 3, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN4
			FastLED.addLeds<WS2812B, PIN4, GRB>(leds, NUM_LEDS_PER_STRIP * 4, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN5
			FastLED.addLeds<WS2812B, PIN5, GRB>(leds, NUM_LEDS_PER_STRIP * 5, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif
		
		#ifdef PIN6
			FastLED.addLeds<WS2812B, PIN6, GRB>(leds, NUM_LEDS_PER_STRIP * 6, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN7
			FastLED.addLeds<WS2812B, PIN7, GRB>(leds, NUM_LEDS_PER_STRIP * 7, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN8
			FastLED.addLeds<WS2812B, PIN8, GRB>(leds, NUM_LEDS_PER_STRIP * 8, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN9
			FastLED.addLeds<WS2812B, PIN9, GRB>(leds, NUM_LEDS_PER_STRIP * 9, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN10
			FastLED.addLeds<WS2812B, PIN10, GRB>(leds, NUM_LEDS_PER_STRIP * 10, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

		#ifdef PIN11
			FastLED.addLeds<WS2812B, PIN11, GRB>(leds, NUM_LEDS_PER_STRIP * 11, NUM_LEDS_PER_STRIP)
				.setCorrection(TypicalLEDStrip);
		#endif

	#else // ifdef S3_15x38_2PIN

		FastLED.addLeds<WS2812B, PIN0, GRB>(leds, 0, 304)
			.setCorrection(TypicalLEDStrip);

		FastLED.addLeds<WS2812B, PIN1, GRB>(leds, 304, 266)
			.setCorrection(TypicalLEDStrip);

	#endif

	#if defined(S3_22x22) || defined(S3_15x38_2PIN)
		FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);
	#endif
	
	FastLED.setBrightness(BRIGHTNESS);

	FastLED.clear();
	FastLED.show();

	bleSetup();

	/*
	if (!LittleFS.begin(true)) {
		Serial.println("LittleFS mount failed!");
		return;
	}
	Serial.println("LittleFS mounted successfully.");
	*/

	if (audioEnabled){
		myAudio::initAudioInput();
		myAudio::initAudioProcessing();
	}

}

//*****************************************************************************************

void loop() {

	PROFILE_FRAME_BEGIN();

	// Hybrid audio pipeline: capture + FFT/bus processing happens inside
	// myAudio::updateAudioFrame() (called by audio-enabled emitters/programs).
	// Avoid draining the I2S queue here, otherwise the program-stage update
	// sees readAll()==0 and the audio analysis freezes.
#if 0
	if (audioEnabled) {
		if (myAudio::audioInputInitialized) {
			//PROFILE_START("audio_capture");
			myAudio::sampleAudio();
			//PROFILE_END();
		}
	}
#endif

	/*
	EVERY_N_SECONDS(3) {
		uint8_t fps = FastLED.getFPS();
		FASTLED_DBG(fps << " fps");
	}
	*/

	
	EVERY_N_SECONDS(10) {
		PROFILE_REPORT();
		PROFILE_RESET();
	}
	

	if (!displayOn){
		FastLED.clear();
	}

	else {

		mappingOverride ? cMapping = cOverrideMapping : cMapping = defaultMapping;
		defaultMapping = Mapping::TopDownProgressive;

		if (!flowFields::flowFieldsInstance) {
			flowFields::initFlowFields(myXY);
		}
		flowFields::runFlowFields();
	}

	PROFILE_START("led_show");
	FastLED.show();
	PROFILE_END();

	if (!deviceConnected && wasConnected) {
		if (debug) {Serial.println("Device disconnected.");}
		delay(500); // give the bluetooth stack the chance to get things ready
		pAdvertising->start();
		if (debug) {Serial.println("Start advertising");}
		wasConnected = false;
	}

	PROFILE_FRAME_END();

} // loop()
