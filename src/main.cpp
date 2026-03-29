//================================================================================================================
/*
CREDITS:
 - colortrails based on visualizer by Stefan Petrick first introduced here:
			https://www.reddit.com/r/FastLED/comments/1rny5j3/i_used_codex_for_the_first_time/
*/
//===============================================================================================================

#include <Arduino.h>

//#define FASTLED_OVERCLOCK 1.2
#include <FastLED.h>

#include <FS.h>
#include "LittleFS.h"
#define FORMAT_LITTLEFS_IF_FAILED true 

bool debug = false;
bool audioEnabled = false;
bool audioLatencyDiagnostics = false;

/*#include "profiler.h"
#ifdef PROFILING_ENABLED
FrameProfiler profiler;
#endif*/

#define BIG_BOARD
//#undef BIG_BOARD

#define PIN0 2

//*********************************************

#ifdef BIG_BOARD 
	
	#include "reference/matrixMap_32x48_3pin.h" 
	#define PIN1 3
    #define PIN2 4
    #define HEIGHT 32 
    #define WIDTH 48
    #define NUM_STRIPS 3
    #define NUM_LEDS_PER_STRIP 512
			
#else 
	
	#include "reference/matrixMap_22x22.h"
	#define HEIGHT 22 
    #define WIDTH 22
    #define NUM_STRIPS 1
    #define NUM_LEDS_PER_STRIP 484

#endif

//*********************************************

#define NUM_LEDS ( WIDTH * HEIGHT )
const uint16_t MIN_DIMENSION = FL_MIN(WIDTH, HEIGHT);
const uint16_t MAX_DIMENSION = FL_MAX(WIDTH, HEIGHT);

fl::CRGB leds[NUM_LEDS];
uint16_t ledNum = 0;

//bleControl variables ***********************************************************************
//elements that must be set before #include "bleControl.h" 

uint8_t EMITTER = 0;
uint8_t FLOW = 0;  // FLOW_NOISE; declared before enum is in scope
uint8_t BRIGHTNESS = 35;

uint8_t defaultMapping = 0;
bool mappingOverride = false;

#include "audio/audioInput.h"
#include "audio/audioProcessing.h"
#include "bleControl.h"
#include "colorTrails.hpp"

using namespace fl;

// MAPPINGS **********************************************************************************

extern const uint16_t progTopDown[NUM_LEDS] PROGMEM;
extern const uint16_t progBottomUp[NUM_LEDS] PROGMEM;
extern const uint16_t serpTopDown[NUM_LEDS] PROGMEM;
extern const uint16_t serpBottomUp[NUM_LEDS] PROGMEM;

enum Mapping {
	TopDownProgressive = 0,
	TopDownSerpentine,
	BottomUpProgressive,
	BottomUpSerpentine
};

uint16_t myXY(uint8_t x, uint8_t y) {
		if (x >= WIDTH || y >= HEIGHT) return 0;
		uint16_t i = ( y * WIDTH ) + x;
		switch(cMapping){
			case 0:	 ledNum = progTopDown[i]; break;
			case 1:	 ledNum = progBottomUp[i]; break;
			case 2:	 ledNum = serpTopDown[i]; break;
			case 3:	 ledNum = serpBottomUp[i]; break;
		}
		return ledNum;
}

//XYMap myXYmap = XYMap::constructWithLookUpTable(WIDTH, HEIGHT, progBottomUp);
//XYMap xyRect = XYMap::constructRectangularGrid(WIDTH, HEIGHT);

// **********************************************************************************

void setup() {
		
	Serial.begin(115200);
	Serial.setTxTimeoutMs(1);  // 1ms timeout — avoids unsigned underflow
	delay(1000);

	FastLED.setExclusiveDriver("RMT");

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
	
	#ifndef BIG_BOARD
		FastLED.setMaxPowerInVoltsAndMilliamps(5, 750);
	#endif
	
	FastLED.setBrightness(BRIGHTNESS);

	FastLED.clear();
	FastLED.show();

	bleSetup();

	if (!LittleFS.begin(true)) {
		Serial.println("LittleFS mount failed!");
		return;
	}
	Serial.println("LittleFS mounted successfully.");

	if (audioEnabled){
		myAudio::initAudioInput();
		myAudio::initAudioProcessing();
	}

}

//*****************************************************************************************

void loop() {

	//PROFILE_FRAME_BEGIN();

	// Capture audio as early as possible each iteration to minimize
	// the delay between DMA buffer availability and processing.
	// sampleAudio() drains all pending DMA buffers, keeping only the
	// newest. When captureAudioFrame() calls it again later (inside
	// the pattern), readAll() returns 0 and the already-captured data
	// is preserved and reused for FFT/bus processing.
	if (audioEnabled) {
		if (myAudio::audioInputInitialized) {
			//PROFILE_START("audio_capture");
			myAudio::sampleAudio();
			//PROFILE_END();
		}
	}
	
	EVERY_N_SECONDS(3) {
		uint8_t fps = FastLED.getFPS();
		FASTLED_DBG(fps << " fps");
	}
	
	/*EVERY_N_SECONDS(10) {
		PROFILE_REPORT();
		PROFILE_RESET();
	}*/

	if (!displayOn){
		FastLED.clear();
	}
	
	else {

		mappingOverride ? cMapping = cOverrideMapping : cMapping = defaultMapping;
		defaultMapping = Mapping::TopDownProgressive;

		if (!colorTrails::colorTrailsInstance) {
			colorTrails::initColorTrails(myXY);
		}
		colorTrails::runColorTrails();
	}

	//PROFILE_START("led_show");
	FastLED.show();
	//PROFILE_END();
	
	// upon BLE disconnect
	if (!deviceConnected && wasConnected) {
		if (debug) {Serial.println("Device disconnected.");}
		delay(500); // give the bluetooth stack the chance to get things ready
		pServer->startAdvertising();
		if (debug) {Serial.println("Start advertising");}
		wasConnected = false;
	}

	//PROFILE_FRAME_END();

} // loop()
