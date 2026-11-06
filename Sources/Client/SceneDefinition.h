/*
 Copyright (c) 2021 yvt

 This file is part of OpenSpades.

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

#pragma once

#include <Core/Math.h>

namespace spades {
	namespace client {
		/**
		 * A view's edges as the tangents of their angles from the view axis: left
		 * and down are negative, right and up positive. A centred view has
		 * left = -right and down = -up; a headset's eyes are not centred (each
		 * sees further to its own side than towards the nose).
		 */
		struct FovTangents {
			float left, right, down, up;
			float Width() const { return right - left; }
			float Height() const { return up - down; }
		};

		struct SceneDefinition {
			int viewportLeft, viewportTop;
			int viewportWidth, viewportHeight;
			/** The full angles. With `asymmetric` set they are the smallest centred
			 * view that covers `tangents`, kept for code that only needs a bound. */
			float fovX, fovY;
			/** When set, `tangents` is the view; otherwise it is centred on fovX/fovY. */
			bool asymmetric = false;
			FovTangents tangents{};
			/** The view's edges, whichever way it was given. Everything that turns a
			 * screen position into a ray, or a ray into a screen position, goes
			 * through this. */
			FovTangents GetFovTangents() const;
			Vector3 viewOrigin;
			Vector3 viewAxis[3];
			float zNear, zFar;
			bool skipWorld;

			/** Whether the frame may draw models flagged `xray` through the world. The
			 * renderer skips the whole pass when this is clear, so it has to be set on
			 * any frame where a model asks to be revealed. */
			bool allowPlayerXRay;

			float depthOfFieldFocalLength;
			float depthOfFieldNearBlurStrength;
			float depthOfFieldFarBlurStrength;

			unsigned int time;

			bool denyCameraBlur;

			float blurVignette;
			float globalBlur;
			float saturation;
			float radialBlur;

			/** One eye of a headset view, in world space. */
			struct VREye {
				Vector3 origin;
				Vector3 axis[3];
				float fovX, fovY;
				/** The eye's own frustum, as the runtime gives it, unless the
				 * symmetric one is drawn (vr_symmetricFrustum). */
				bool asymmetric = false;
				FovTangents tangents{};
			};

			/** 2 when the frame goes to a headset: a renderer that can draws each of
			 * `vrEyes` instead of the single view above, which then stands for the
			 * head (sound, culling of the 2D overlay). 0 otherwise. Renderers that
			 * know nothing of VR ignore it and draw the single view. */
			int numVREyes = 0;
			VREye vrEyes[2];
			/** The factor the sun's light is drawn with, in `[0, 1]`. At `0` the sun
			 * casts no light and no shadow. */
			float sunlight;

			/**
			 * The factor the rest of the world's lighting, the fog and the sky are drawn
			 * with, in `[0, 1]`. Dynamic lights are not affected. `1` is full daylight.
			 */
			float daylight;

			SceneDefinition() {
				viewportLeft = viewportTop = 0;
				viewportWidth = viewportHeight = 0;
				fovX = fovY = 0.0F;
				viewOrigin = MakeVector3(0, 0, 0);
				viewAxis[0] = MakeVector3(1, 0, 0);
				viewAxis[1] = MakeVector3(0, 1, 0);
				viewAxis[2] = MakeVector3(0, 0, 1);
				zNear = zFar = 0.0F;
				skipWorld = false;
				allowPlayerXRay = false;
				depthOfFieldFocalLength = 0.0F;
				depthOfFieldNearBlurStrength = 1.0F;
				depthOfFieldFarBlurStrength = 0.0F;
				denyCameraBlur = true;
				time = 0;
				blurVignette = 0.0F;
				globalBlur = 0.0F;
				saturation = 1.0F;
				radialBlur = 0.0F;
				sunlight = 1.0F;
				daylight = 1.0F;
			}

			Matrix4 ToOpenGLProjectionMatrix() const;
			Matrix4 ToVulkanProjectionMatrix() const;
			Matrix4 ToViewMatrix() const;
		};
	} // namespace client
} // namespace spades