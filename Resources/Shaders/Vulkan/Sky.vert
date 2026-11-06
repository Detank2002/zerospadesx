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

#define ZS_VIEWS_SET 0
#include "Multiview.glsl"

layout(push_constant) uniform PushConstants {
	vec3 fogColor;
	float _pad0;
	vec3 viewAxisFront;
	float _pad1;
	vec3 viewAxisUp;
	float _pad2;
	vec3 viewAxisSide;
	float _pad3;
	// The view's edges as tangents: left and down negative.
	float tanLeft;
	float tanRight;
	float tanDown;
	float tanUp;
} pushConstants;

layout(location = 0) in vec2 positionAttribute;

layout(location = 0) out vec3 viewDir;

void main() {
	// Position is in normalized device coordinates [-1, 1]
	// Set depth to 1.0 (far plane in Vulkan standard depth)
	gl_Position = vec4(positionAttribute, 1.0, 1.0);

	// Compute view direction for this screen position: the edge tangents
	// interpolated across the screen, so an off-centre view (a headset eye)
	// gets its own rays.
	vec2 at = positionAttribute * 0.5 + 0.5;
	float tanX = mix(pushConstants.tanLeft, pushConstants.tanRight, at.x);
	// With flipped viewport, positionAttribute.y is already inverted, so +1 is up
	float tanY = mix(pushConstants.tanDown, pushConstants.tanUp, at.y);

	vec3 front = pushConstants.viewAxisFront;
	vec3 right = pushConstants.viewAxisSide;
	vec3 up = pushConstants.viewAxisUp;

	viewDir = normalize(front + right * tanX + up * tanY);
	gl_Position = ZS_CORRECT(gl_Position);
}
