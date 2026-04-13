#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Board / Hardware Configuration
//  All pin assignments, matrix dimensions, and driver selection
//  live here — keeping main.cpp focused on application logic.
// ═══════════════════════════════════════════════════════════════════

//#define BIG_BOARD
#undef BIG_BOARD

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    
    #ifdef BIG_BOARD
        
        // --- 32x48, 3 strips ---
        #include "reference/matrixMap_32x48_3pin.h"
        #define PIN0 2
        #define PIN1 3
        #define PIN2 4
        #define HEIGHT 32
        #define WIDTH 48
        #define NUM_STRIPS 3
        #define NUM_LEDS_PER_STRIP 512
        #define LED_DRIVER "RMT"

    #else

        // --- 22x22 matrix ---
        #include "reference/matrixMap_22x22.h"
        #define PIN0 2
        #define HEIGHT 22
        #define WIDTH 22
        #define NUM_STRIPS 1
        #define NUM_LEDS_PER_STRIP 484
        #define LED_DRIVER "RMT"

    #endif

#else

   /*
    // --- 48x64, 6 strips (P4-WIFI6 / Waveshare) ---
    #include "reference/matrixMap_48x64_6pin.h"
    #define PIN0 5
    #define PIN1 49
    #define PIN2 50
    #define PIN3 4
    #define PIN4 3
    #define PIN5 2
    #define HEIGHT 48
    #define WIDTH 64
    #define NUM_STRIPS 6
    #define NUM_LEDS_PER_STRIP 512
    #define LED_DRIVER "PARLIO"
    */
    
    // --- 48x64, 12 strips (P4-WIFI6 / Waveshare) ---
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
