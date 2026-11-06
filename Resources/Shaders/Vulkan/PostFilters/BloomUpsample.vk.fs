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


// Bloom upsample composite pass.
// Blends a smaller bloom level (bilinearly upscaled) into the current level:
//   outColor = mix(large, small, alpha)
// This is equivalent to the GL bloom's SRC_ALPHA blend with PassThrough.

layout(binding = 0) uniform ZS_EYE_SAMPLER largeTexture; // current composite at output resolution
layout(binding = 1) uniform ZS_EYE_SAMPLER smallTexture; // smaller level (upscaled by bilinear)

layout(push_constant) uniform Params {
    float alpha;
} params;

layout(location = 0) in  vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 large = ZS_EYE_TEX(largeTexture, texCoord).rgb;
    vec3 small = ZS_EYE_TEX(smallTexture, texCoord).rgb;
    outColor = vec4(mix(large, small, params.alpha), 1.0);
}
