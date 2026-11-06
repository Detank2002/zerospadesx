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

// Vulkan port of Shaders/OpenGL/PostFilters/FXAA.fs.
// Same algorithm; `texCoord` arrives in normalized [0,1] from the shared
// fullscreen-triangle vertex shader, so neighbour samples shift by
// inverseVP (= 1 / framebufferSize) directly instead of the GL flow that
// kept fragment coords in pixel space.

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

layout(set = 0, binding = 0) uniform ZS_EYE_SAMPLER mainTexture;

layout(push_constant) uniform PushConstants {
    vec2 inverseVP;
} pc;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

#define FXAA_REDUCE_MIN   (1.0 / 128.0)
#define FXAA_REDUCE_MUL   (1.0 / 8.0)
#define FXAA_SPAN_MAX     8.0

vec4 applyFXAA(vec2 uv, ZS_EYE_SAMPLER tex, vec2 invVP) {
    vec3 rgbNW = ZS_EYE_TEX(tex, uv + vec2(-1.0, -1.0) * invVP).xyz;
    vec3 rgbNE = ZS_EYE_TEX(tex, uv + vec2( 1.0, -1.0) * invVP).xyz;
    vec3 rgbSW = ZS_EYE_TEX(tex, uv + vec2(-1.0,  1.0) * invVP).xyz;
    vec3 rgbSE = ZS_EYE_TEX(tex, uv + vec2( 1.0,  1.0) * invVP).xyz;
    vec3 rgbM  = ZS_EYE_TEX(tex, uv).xyz;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) *
                          (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(FXAA_SPAN_MAX),
              max(vec2(-FXAA_SPAN_MAX),
                  dir * rcpDirMin)) * invVP;

    vec3 rgbA = 0.5 * (
        ZS_EYE_TEX(tex, uv + dir * (1.0 / 3.0 - 0.5)).xyz +
        ZS_EYE_TEX(tex, uv + dir * (2.0 / 3.0 - 0.5)).xyz);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        ZS_EYE_TEX(tex, uv + dir * -0.5).xyz +
        ZS_EYE_TEX(tex, uv + dir *  0.5).xyz);

    float lumaB = dot(rgbB, luma);
    return ((lumaB < lumaMin) || (lumaB > lumaMax))
        ? vec4(rgbA, 1.0) : vec4(rgbB, 1.0);
}

void main() {
    outColor = applyFXAA(texCoord, mainTexture, pc.inverseVP);
}
