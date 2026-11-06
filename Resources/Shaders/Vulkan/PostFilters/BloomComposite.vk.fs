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
#ifdef ZS_MULTIVIEW
// Multiview: both eyes' layers in one pass; each fragment reads its own.
#extension GL_EXT_multiview : require
#define ZS_EYE_SAMPLER sampler2DArray
#define ZS_EYE_TEX(t, uv) texture(t, vec3((uv), float(gl_ViewIndex)))
#else
#define ZS_EYE_SAMPLER sampler2D
#define ZS_EYE_TEX(t, uv) texture(t, (uv))
#endif


// Bloom final composite — Vulkan port of GL's LensDust.fs (the real
// r_bloom composite). Linear framebuffer, so GL's linearize/sqrt round
// trip is dropped; the dust texture still gets squared to linearize it
// (it is a plain sRGB-ish JPG).

layout(binding = 0) uniform ZS_EYE_SAMPLER inputTexture;
layout(binding = 1) uniform ZS_EYE_SAMPLER blurTexture1;
layout(binding = 2) uniform sampler2D dustTexture;
layout(binding = 3) uniform sampler2D noiseTexture;

layout(push_constant) uniform LensDustPC {
	vec4 noiseTexCoordFactor;
} pc;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
	// As GL has it, the dust's first row at the bottom of the screen, where
	// `texCoord` is the top
	vec3 dust1 = texture(dustTexture, vec2(texCoord.x, 1.0 - texCoord.y)).xyz;
	dust1 *= dust1; // linearize

	vec3 blur1 = ZS_EYE_TEX(blurTexture1, texCoord).xyz;

	vec3 sum = dust1 * blur1;

	vec3 final = ZS_EYE_TEX(inputTexture, texCoord).xyz;

	final *= 0.95;
	final += sum * 2.0;

	// film grain
	vec4 noiseTexCoord = texCoord.xyxy * pc.noiseTexCoordFactor;
	float grain = texture(noiseTexture, noiseTexCoord.xy).x;
	grain += texture(noiseTexture, noiseTexCoord.zw).x;
	grain = fract(grain) - 0.5;
	final += grain * 0.003;

	outColor = vec4(max(final, 0.0), 1.0);
}
