/*
 Copyright (c) 2013 yvt

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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include "ScriptFunction.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		class ScriptIThirdPersonToolSkin {
			asIScriptObject* obj;

		public:
			ScriptIThirdPersonToolSkin(asIScriptObject* obj);
			void SetOriginMatrix(Matrix4 m);
		};

		/** A gun's skin drawn in a VR player's own hand (optional: a skin without
		 * it draws there as it does for everyone). With VRHand set it draws the
		 * first-person gun and its sights, and tells where they are, in the world,
		 * for the origin matrix it was last given. */
		class ScriptIVRWeaponSkin {
			asIScriptObject* obj;

		public:
			ScriptIVRWeaponSkin(asIScriptObject* obj);
			bool ImplementsInterface();
			void SetVRHand(bool);
			/** The iron sights' line: through the rear notch, to the front sight */
			Vector3 GetVRSightRear();
			Vector3 GetVRSightFront();
			/** The reflex sight's window, and its radius (0: no reflex sight) */
			Vector3 GetVRReflexCenter();
			float GetVRReflexRadius();
		};
	} // namespace client
} // namespace spades