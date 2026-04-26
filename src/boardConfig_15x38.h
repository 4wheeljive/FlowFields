#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Board / Hardware Configuration
//  All pin assignments, matrix dimensions, and driver selection
//  live here — keeping main.cpp focused on application logic.
// ═══════════════════════════════════════════════════════════════════

#include "reference/matrixMap_15x38_2pin.h"
#define PIN0 2
#define PIN1 3
#define HEIGHT 15
#define WIDTH 38
#define NUM_STRIPS 2
//#define NUM_LEDS_PER_STRIP 512
#define LED_DRIVER "RMT"

#define NUM_LEDS (WIDTH * HEIGHT)
