#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Board / Hardware Configuration
//  All pin assignments, matrix dimensions, and driver selection
//  live here — keeping main.cpp focused on application logic.
// ═══════════════════════════════════════════════════════════════════

//#define S3_32x48_3PIN
#undef S3_32x48_3PIN

#define S3_22x22
//#undef S3_22x22

//#define S3_15x38_2PIN
#undef S3_15x38_2PIN

//#define P4_64x48_12PIN
#undef P4_64x48_12PIN

#ifdef S3_32x48_3PIN
    #include "reference/matrixMap_32x48_3pin.h"
    #define PIN0 2
    #define PIN1 3
    #define PIN2 4
    #define HEIGHT 32
    #define WIDTH 48
    #define NUM_STRIPS 3
    #define NUM_LEDS_PER_STRIP 512
    #define LED_DRIVER "RMT"
#endif

#ifdef S3_22x22
    #include "reference/matrixMap_22x22.h"
    #define PIN0 2
    #define HEIGHT 22
    #define WIDTH 22
    #define NUM_STRIPS 1
    #define NUM_LEDS_PER_STRIP 484
    #define LED_DRIVER "RMT"
#endif

#ifdef S3_15x38_2PIN
    #include "reference/matrixMap_15x38_2pin.h"
    #define PIN0 2
    #define PIN1 3
    #define HEIGHT 15
    #define WIDTH 38
    #define NUM_STRIPS 2
    #define LED_DRIVER "RMT"
#endif

#ifdef P4_64x48_12PIN
    #include "reference/matrixMap_48x64_12pin.h"
    #define PIN0  2
    #define PIN1  27
    #define PIN2  3
    #define PIN3  26
    #define PIN4  4
    #define PIN5  23
    #define PIN6  5
    #define PIN7  22
    #define PIN8  49
    #define PIN9  21
    #define PIN10 50
    #define PIN11 20
    #define HEIGHT 48
    #define WIDTH  64
    #define NUM_STRIPS 12
    #define NUM_LEDS_PER_STRIP 256
    #define LED_DRIVER "PARLIO"
#endif

#define NUM_LEDS (WIDTH * HEIGHT)

const uint16_t MIN_DIMENSION = FL_MIN(WIDTH, HEIGHT);
const uint16_t MAX_DIMENSION = FL_MAX(WIDTH, HEIGHT);

// MAPPINGS *****************************************************************************

extern const uint16_t progTopDown[NUM_LEDS] PROGMEM;
extern const uint16_t progBottomUp[NUM_LEDS] PROGMEM;
extern const uint16_t serpTopDown[NUM_LEDS] PROGMEM;
extern const uint16_t serpBottomUp[NUM_LEDS] PROGMEM;

extern uint8_t cMapping;
extern uint16_t ledNum;

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