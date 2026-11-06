#pragma once
// A position or a direction in the protocol's axes: x and y across the map, z
// down. Plain floats, as the packets carry them.

struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};
