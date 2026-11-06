/*
 Copyright (c) 2026 ZeroSpades X contributors

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

// Multiview (vr_multiview): both eyes in one pass. A shader that includes this
// is compiled a second time with ZS_MULTIVIEW (Resources/CMakeLists.txt, which
// looks for ZS_VIEWS_SET or ZS_MULTIVIEW in the shader itself), and the
// renderer loads that variant only in multiview; without it every macro here
// is the plain single-view form.
//
// Everything a scene shader works out is in eye 0's view; ZS_CORRECT takes
// eye 0's clip position to eye gl_ViewIndex's (VulkanRendererVR.cpp,
// "Multiview"). The screen's images (its depth, its copy for the water, the
// mirror) have a layer an eye, read through ZS_SCREEN_*.
//
// Include it first, right after the include directive, with ZS_VIEWS_SET and
// ZS_VIEWS_BINDING set to where the pipeline has the views: a binding of a set
// it binds anyway (the scene view's, 3/2; the sprite's, 3/3), or a set of their
// own where one is free (the water's 1, the sky's and lines' 0). Never a fifth
// set: a Quest 2's Adreno 650 binds only four.

#ifndef ZS_VIEWS_SET
#define ZS_VIEWS_SET 0
#endif
#ifndef ZS_VIEWS_BINDING
#define ZS_VIEWS_BINDING 0
#endif

#ifdef ZS_MULTIVIEW
#extension GL_EXT_multiview : require

layout(set = ZS_VIEWS_SET, binding = ZS_VIEWS_BINDING, std140) uniform ZsViews {
	mat4 correction[2];
	mat4 view[2];
	mat4 projection[2];
	vec4 eyeOrigin[2];
	// The water's (right, -up, left, -down) tangents of each eye's field of view
	vec4 fovTan[2];
} zsViews;

#define ZS_CORRECT(clip) (zsViews.correction[gl_ViewIndex] * (clip))
#define ZS_SCREEN_SAMPLER sampler2DArray
#define ZS_SCREEN_USAMPLER usampler2DArray
#define ZS_SCREEN_TEX(t, uv) texture(t, vec3((uv), float(gl_ViewIndex)))
#define ZS_SCREEN_TEXLOD(t, uv, lod) textureLod(t, vec3((uv), float(gl_ViewIndex)), (lod))
#define ZS_SCREEN_FETCH(t, p, lod) texelFetch(t, ivec3((p), gl_ViewIndex), (lod))
#define ZS_SCREEN_SIZE(t, lod) textureSize(t, (lod)).xy
#else
#define ZS_CORRECT(clip) (clip)
#define ZS_SCREEN_SAMPLER sampler2D
#define ZS_SCREEN_USAMPLER usampler2D
#define ZS_SCREEN_TEX(t, uv) texture(t, (uv))
#define ZS_SCREEN_TEXLOD(t, uv, lod) textureLod(t, (uv), (lod))
#define ZS_SCREEN_FETCH(t, p, lod) texelFetch(t, (p), (lod))
#define ZS_SCREEN_SIZE(t, lod) textureSize(t, (lod))
#endif
