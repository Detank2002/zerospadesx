#version 450
// The level 1 water drawn inside the scene's pass (r_waterInScene, a
// headset's mobile settings): no copy of the scene to see through, so the
// pass is not broken for one, which on a tiled GPU was the costliest part
// of the frame. See WaterBasic.glsl.
#extension GL_GOOGLE_include_directive : require

#define ZS_VIEWS_SET 1 // in place of the empty set before the lights
#include "Multiview.glsl"
#define ZS_WATER_IN_PASS 1
#include "WaterBasic.glsl"
