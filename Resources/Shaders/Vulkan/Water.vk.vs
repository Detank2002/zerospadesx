#version 450
#extension GL_GOOGLE_include_directive : require

#define ZS_VIEWS_SET 1 // in place of the empty set before the lights
#include "Multiview.glsl"

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

layout(std140, binding = 5) uniform WaterMatricesUBO {
	mat4 projectionViewModelMatrix;
	mat4 modelMatrix;
	mat4 viewModelMatrix;
	mat4 viewMatrix; // For Water3 SSR
	mat4 projectionViewMatrix; // Used by Water2/3 only; declared here so the
	                           // shared UBO layout stays in sync.
	vec4 viewOriginVector; // use .xyz
	float fogDistance;
	vec3 _pad0;
} waterMat;

// [x, y]
layout(location = 0) in vec2 positionAttribute;

layout(location = 0) out vec3 v_fogDensity;
layout(location = 1) out vec3 v_screenPosition;
layout(location = 2) out vec3 v_viewPosition;
layout(location = 3) out vec3 v_worldPosition;

#ifdef ZS_MULTIVIEW
#define ZS_VIEW_MODEL (zsViews.view[gl_ViewIndex] * waterMat.modelMatrix)
#define ZS_EYE_ORIGIN zsViews.eyeOrigin[gl_ViewIndex]
#else
#define ZS_VIEW_MODEL waterMat.viewModelMatrix
#define ZS_EYE_ORIGIN waterMat.viewOriginVector
#endif
// Fog computation from OpenGL Fog.vs
vec4 ComputeFogDensity(float poweredLength) {
	return vec4(min(poweredLength / (waterMat.fogDistance * waterMat.fogDistance), 1.0));
}

void main() {
	vec4 vertexPos = vec4(positionAttribute.xy, 0.0, 1.0);

	// projectionViewModelMatrix and viewModelMatrix already include M, so
	// apply them to object-space vertexPos (matches GL Water.vs).
	v_worldPosition = (waterMat.modelMatrix * vertexPos).xyz;
	v_viewPosition = (ZS_VIEW_MODEL * vertexPos).xyz;

	gl_Position = waterMat.projectionViewModelMatrix * vertexPos;
	v_screenPosition = gl_Position.xyw;
	// Vulkan texture coordinates: y=0 at top, y=1 at bottom (opposite of OpenGL)
	v_screenPosition.x = (v_screenPosition.x + v_screenPosition.z) * 0.5;
	v_screenPosition.y = (v_screenPosition.z - v_screenPosition.y) * 0.5;

	vec2 horzRelativePos = v_worldPosition.xy - ZS_EYE_ORIGIN.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	v_fogDensity = ComputeFogDensity(horzDistance).xyz;
#ifdef ZS_MULTIVIEW
	gl_Position = ZS_CORRECT(gl_Position);
	// The screen position this eye samples the scene copy at: from its own
	// clip position, not eye 0's. Taken before the correction, the right eye
	// read the scene copy where the left eye sees the water, and its water
	// showed a shifted copy of its own view, gun and all (on the Quest).
	v_screenPosition = gl_Position.xyw;
	v_screenPosition.x = (v_screenPosition.x + v_screenPosition.z) * 0.5;
	v_screenPosition.y = (v_screenPosition.z - v_screenPosition.y) * 0.5;
#endif
}
