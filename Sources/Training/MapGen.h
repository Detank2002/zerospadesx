#pragma once
// Generated maps: the training course, and the rolling grassland the VR menus
// stand on.
//
// The grassland has hills, a few lakes, sand at the water and dirt on the
// steep faces, an island edge of open water, and trees; the outer quarters
// (x < 128 and x >= 384, between y 128 and 384) are kept as land.
//
// Written here rather than ported: pyspades' classicgen is Ken Silverman's
// genland, whose licence forbids commercial use, which GPL code cannot carry.
#include <cstdint>

namespace host {

class VoxelMap;

// Fill `map` (which should be empty) from `seed`. Same seed, same map.
void generate_map(VoxelMap& map, uint32_t seed);

// The training course (TrainingCourse.h): a walled corridor of stations,
// laid out the same every time. `map` should be empty.
void generate_training_map(VoxelMap& map);

}  // namespace host
