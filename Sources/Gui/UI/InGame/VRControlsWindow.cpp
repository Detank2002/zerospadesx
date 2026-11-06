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

#include "VRControlsWindow.h"

#include <algorithm>

#include <Client/ClientUI.h>
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Client/VRState.h>
#include <Core/Debug.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::SetColorNP;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			const char* const kSlides[] = {"Quest2", "Quest3", "Index", "Vive", "WMR", "Body"};
			std::string SlidePath(const std::string& name) {
				return "Gfx/VR/Controls/" + name + ".png";
			}
		} // namespace

		std::string VRControlsSlideForControllers() {
			std::string profile, headset;
			if (IVRSystem* vr = GetVRSystem()) {
				profile = vr->GetControllerProfile();
				headset = vr->GetHeadsetName();
			}
			if (profile.find("valve/index") != std::string::npos)
				return "Index";
			if (profile.find("htc/vive") != std::string::npos)
				return "Vive";
			if (profile.find("microsoft/motion") != std::string::npos)
				return "WMR";
			// Touch, or none known yet: a Quest 3 or 3S has Touch Plus, without
			// the ring (the runtime names it "Meta Quest 3"; a Quest 2 is
			// "Oculus Quest2").
			std::string h = ToLowerCase(headset);
			h.erase(std::remove(h.begin(), h.end(), ' '), h.end());
			if (h.find("quest3") != std::string::npos)
				return "Quest3";
			return "Quest2";
		}

		VRControlsWindow::VRControlsWindow(ClientUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui) {
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());

			// This headset's controllers first, then the body, then the rest.
			const std::string mine = VRControlsSlideForControllers();
			pages.push_back(mine);
			pages.push_back("Body");
			for (const char* s : kSlides)
				if (std::find(pages.begin(), pages.end(), s) == pages.end())
					pages.push_back(s);
			images.resize(pages.size());
			tried.resize(pages.size());

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			{
				Handle<Label> overlay = Handle<Label>::New(manager);
				overlay->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.85F);
				overlay->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				AddChild(overlay.GetPointerOrNull());
			}

			// The slides are 16:9, as large as fits over a row of buttons.
			const float buttonH = 30.0F, margin = 10.0F;
			float iw = sw - margin * 2.0F;
			float ih = iw * 9.0F / 16.0F;
			if (ih > sh - buttonH - margin * 3.0F) {
				ih = sh - buttonH - margin * 3.0F;
				iw = ih * 16.0F / 9.0F;
			}
			imageRect = AABB2((sw - iw) * 0.5F, margin, iw, ih);
			const float by = imageRect.GetMaxY() + margin;
			const float bx = imageRect.GetMinX();

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Previous");
				button->SetBounds(AABB2(bx, by, 150.0F, buttonH));
				button->activated = [this](UIElement&) { Turn(-1); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Next");
				button->SetBounds(AABB2(bx + 160.0F, by, 150.0F, buttonH));
				button->activated = [this](UIElement&) { Turn(1); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Back");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(imageRect.GetMaxX() - 150.0F, by, 150.0F, buttonH));
				button->activated = [this](UIElement&) { Back(); };
				AddChild(button.GetPointerOrNull());
			}
		}

		void VRControlsWindow::Turn(int by) {
			const int n = static_cast<int>(pages.size());
			page = ((page + by) % n + n) % n;
		}

		void VRControlsWindow::Back() { ui->EnterClientMenu(); }

		void VRControlsWindow::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape")
				Back();
			else
				UIElement::HotKey(key);
		}

		void VRControlsWindow::Render() {
			UIElement::Render();

			client::IRenderer& r = GetManager().GetRenderer();
			IFont& font = ui->GetFontManager().GetGuiFont();
			Vector2 pos = GetScreenPosition();

			// Tried once a page: a renderer may give no image for a file that
			// is missing (paks older than the game) rather than throw.
			if (!tried[page]) {
				tried[page] = true;
				try {
					images[page] = r.RegisterImage(SlidePath(pages[page]).c_str());
				} catch (const std::exception& ex) {
					SPLog("Controls slide %s: %s", pages[page].c_str(), ex.what());
				}
			}
			if (images[page]) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
				r.DrawImage(*images[page], imageRect.Translated(pos));
			} else {
				std::string s = _Tr("Client", "Missing {0}", SlidePath(pages[page]));
				font.Draw(s, pos + imageRect.min + MakeVector2(20.0F, 20.0F), 1.0F,
				          MakeVector4(1.0F, 0.6F, 0.6F, 1.0F));
			}

			// Which page of how many, between the buttons.
			std::string s = _Tr("Client", "{0} of {1}", page + 1, pages.size());
			Vector2 sz = font.Measure(s);
			Vector2 at = pos + MakeVector2(imageRect.GetMinX() + 320.0F,
			                               imageRect.GetMaxY() + 10.0F + (30.0F - sz.y) * 0.5F);
			font.Draw(s, at, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.8F));
		}
	} // namespace client
} // namespace spades
