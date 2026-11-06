/*
 Copyright (c) 2026 ZeroSpades developers.

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

#pragma once

#include <string>
#include <vector>

#include <Client/IImage.h>
#include <Core/RefCountedObject.h>
#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace client {
		class ClientUI;

		/** The in-game menu's Controls, in a headset: the cheat sheet's slides
		 * (Resources/Gfx/VR/Controls, made by tools/vr-controls), starting at
		 * the one for the controllers in use, then the body's (holsters, wrist).
		 * Previous and Next page through all of them; Back or Escape returns to
		 * the menu. */
		class VRControlsWindow : public gui::ui::UIElement {
			ClientUI* ui; // weak
			std::vector<std::string> pages;
			std::vector<Handle<IImage>> images; // loaded as first shown
			std::vector<bool> tried;
			int page = 0;
			AABB2 imageRect;

			void Turn(int by);
			void Back();

		public:
			VRControlsWindow(ClientUI* ui);
			void HotKey(const std::string& key) override;
			void Render() override;
		};

		/** The slide for the controllers in use (VR::IVRSystem's interaction
		 * profile and headset name), as a path under Gfx/VR/Controls. */
		std::string VRControlsSlideForControllers();
	} // namespace client
} // namespace spades
