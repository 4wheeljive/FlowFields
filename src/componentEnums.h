#pragma once
#include <stdint.h>

enum Emitter : uint8_t {
    EMITTER_ORBITALDOTS = 0,
    EMITTER_SWARMINGDOTS,
    EMITTER_LISSAJOUS,
    EMITTER_BORDERRECT,
    EMITTER_AUDIODOTS,
    // future: EMITTER_TRIANGLE, ...
    EMITTER_COUNT
};

enum Flow : uint8_t {
    FLOW_NOISE = 0,
    FLOW_FROMCENTER,
    FLOW_DIRECTIONAL,
    // future: FLOW_TOCENTER, FLOW_SPIRAL, FLOW_POLARWARP, ...
    FLOW_COUNT
};
