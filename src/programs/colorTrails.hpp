#pragma once

#include "colorTrails_detail.hpp"

namespace colorTrails {
    extern bool colorTrailsInstance;

    void initColorTrails(uint16_t (*xy_func)(uint8_t, uint8_t));
    void runColorTrails();
}
