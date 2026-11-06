/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec4 color;           // xyz = linearized vertex color, w = sun lambert
layout(location = 1) in vec3 ambientLight;     // hemisphere ambient fallback (unused, kept for VS↔FS compat)
layout(location = 2) in vec3 fogDensity;
layout(location = 3) in vec3 inFogColor;
layout(location = 4) in vec3 shadowCoord;      // shadow map coordinates
layout(location = 5) in vec3 aoCoord;          // 3D coords into AO texture
layout(location = 6) in vec3 radiosityTextureCoord; // 3D coords into radiosity textures
layout(location = 7) in vec3 normalVarying;    // surface normal in world space
layout(location = 8) in vec2 ambientOcclusionCoord; // 2D coords into AO atlas
layout(location = 9) in vec3 modelShadowCoord0;     // light-clip coords per cascade
layout(location = 10) in vec3 modelShadowCoord1;
layout(location = 11) in vec3 modelShadowCoord2;
layout(location = 12) in float shadowViewDepth;     // camera-axis depth, for cascade choice
// World-space position, where the dynamic lights are evaluated
layout(location = 13) in vec3 worldPosition;

layout(location = 0) out vec4 fragColor;

#include "SceneView.glsl"
#include "SceneLight/Lights.glsl"
#include "SceneLight/MapLight.glsl"
#include "SceneAmbientOcclusion.glsl"
#include "SceneLight/ModelShadow.glsl"

// In a headset (vr_brightness), the brightest surfaces roll off toward this
// peak instead of clipping at full white: white blocks filling the view, as on
// a snow map, tired testers' eyes. 0: off (a monitor). Before the fog, so the
// far map still fades into the sky. See VulkanRenderer::GetVRVoxelPeak.
layout(constant_id = 1) const float VOXEL_PEAK = 0.0;
vec3 VoxelRollOff(vec3 c) {
	if (VOXEL_PEAK <= 0.0)
		return c;
	// Below the knee nothing changes; above it the brightest channel eases
	// toward the peak with the same slope at the knee, and the others keep
	// their ratio to it, so a colour keeps its hue.
	float knee = VOXEL_PEAK * 0.55;
	float m = max(max(c.r, c.g), c.b);
	if (m <= knee)
		return c;
	float span = VOXEL_PEAK - knee;
	return c * ((knee + span * (1.0 - exp((knee - m) / span))) / m);
}

void main() {
	// Map shadow (matches GL Map.fs: EvaluateMapShadow)
	float shadow = MapShadowVisibility(shadowCoord);

	// Fold in dynamic model shadows (GL: VisibilityOfSunLight = map * model).
	shadow *= ModelShadowVisibility(modelShadowCoord0, modelShadowCoord1, modelShadowCoord2,
	                                shadowViewDepth);

	vec3 nrm = normalize(normalVarying);
	vec3 vertexColor = color.xyz;
	float sunLambert = color.w;

	// Sun contribution — matches GL Common.fs EvaluateSunLight() * color.w
	vec3 sun = vec3(0.6 * sceneSunSky.sunlight) * sunLambert * shadow;

	// The map's and the sky's light, through the surface's own ambient
	// occlusion from the 2D atlas (GL BasicBlock.fs EvaluateAmbientLight)
	float detailAO = texture(ambientOcclusionAtlas, ambientOcclusionCoord).x;
	vec3 diffuse = MapAmbientLight(radiosityTextureCoord, aoCoord, nrm, detailAO,
	                                SceneAmbientOcclusion()) + sun;

	fragColor = vec4(vertexColor * diffuse, 1.0);
	fragColor.xyz += vertexColor * EvaluateDynamicLights(worldPosition, nrm);

	fragColor.xyz = VoxelRollOff(fragColor.xyz);

	// Fog fade
	fragColor.xyz = mix(fragColor.xyz, inFogColor, fogDensity);
}
