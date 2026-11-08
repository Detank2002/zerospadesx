/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "UpdateNotice.h"

#include <algorithm>
#include <utility>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/ShellApi.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/SelfUpdater.h>
#include <Gui/UpdateChecker.h>

// Only the startup prompt can be turned off; the banner always announces a new
// release. Prefixed `zs` because the client shares SPConfig.cfg with OpenSpades.
DEFINE_SPADES_SETTING(cl_zsUpdatePrompt, "1");

namespace spades {
	namespace gui {
		namespace {
			// Warm accent so the notice reads apart from the grey menu chrome.
			const Vector3 kAccent = MakeVector3(1.0F, 0.72F, 0.2F);

			// Quiet confirmation colour; it should not compete with the menu.
			const Vector4 kUpToDateColor = MakeVector4(0.45F, 0.9F, 0.45F, 0.85F);
			constexpr float kCheckMarkSize = 10.0F;
			constexpr float kCheckMarkThickness = 2.0F;
			constexpr float kCheckMarkGap = 6.0F;

			constexpr float kPromptHeight = 170.0F;

			/**
			 * Opens the release page and, once the browser has it, runs `opened`
			 * (which closes the application). If the browser could not be launched
			 * the game stays open, so the user is not left with neither.
			 */
			void OpenReleasePage(const std::string& url, const std::function<void()>& opened) {
				if (!OpenURLInBrowser(url)) {
					SPLog("[!] Could not open the release page: %s", url.c_str());
					return;
				}
				if (opened)
					opened();
			}
		} // namespace

		// -- UpdateNotice --

		UpdateNotice::UpdateNotice(ui::UIManager* manager, std::function<void()> releasePageOpened,
		                           bool showUpToDate)
		    : ButtonBase(manager),
		      showUpToDate(showUpToDate),
		      releasePageOpened(std::move(releasePageOpened)) {
			isMouseInteractive = false;
			Refresh();
		}

		void UpdateNotice::Refresh() {
			// A found update is final for this process; resolve it only once.
			if (releaseUrl.empty()) {
				UpdateChecker::Result result = UpdateChecker::Get().GetResult();
				upToDate = result.state == UpdateChecker::State::UpToDate;
				if (result.state != UpdateChecker::State::UpdateAvailable)
					return;
				releaseUrl = result.releaseUrl;
				releaseTag = result.latestTag;
				latestVersion = result.latestVersion.ToString();
			}

			isMouseInteractive = true;
			progressFraction = -1.0F;
			const std::string current = ReleaseVersion::Current().ToString();
			if (!SelfUpdater::IsSupported()) {
				caption = _Tr("MainScreen",
				              "ZeroSpades X {0} is available (you have {1}). Click to open the "
				              "release page.",
				              latestVersion, current);
				return;
			}
			const SelfUpdater::Progress p = SelfUpdater::Get().GetProgress();
			switch (p.state) {
				case SelfUpdater::State::Idle:
					caption = _Tr("MainScreen",
					              "ZeroSpades X {0} is available (you have {1}). Click to update.",
					              latestVersion, current);
					break;
				case SelfUpdater::State::Working:
					caption = p.status;
					progressFraction = p.fraction;
					isMouseInteractive = false;
					break;
				case SelfUpdater::State::Ready:
					caption = _Tr("MainScreen",
					              "ZeroSpades X {0} is ready. Click to restart and install it.",
					              latestVersion);
					progressFraction = 1.0F;
					break;
				case SelfUpdater::State::Failed: {
					std::string why = p.error;
					while (!why.empty() && (why.back() == '.' || why.back() == ' '))
						why.pop_back();
					caption = _Tr("MainScreen",
					              "The update failed ({0}). Click to open the release page.", why);
					break;
				}
			}
		}

		void UpdateNotice::OnActivated() {
			if (!releaseUrl.empty()) {
				if (!SelfUpdater::IsSupported()) {
					OpenReleasePage(releaseUrl, releasePageOpened);
				} else {
					switch (SelfUpdater::Get().GetProgress().state) {
						case SelfUpdater::State::Idle: SelfUpdater::Get().Start(releaseTag); break;
						case SelfUpdater::State::Ready: SelfUpdater::Get().InstallAndRestart(); break;
						case SelfUpdater::State::Failed:
							OpenReleasePage(releaseUrl, releasePageOpened);
							break;
						case SelfUpdater::State::Working: break;
					}
				}
			}
			ButtonBase::OnActivated();
		}

		void UpdateNotice::RenderUpToDate() {
			client::IFont* font = GetFont();
			if (!font)
				return;

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();

			std::string text =
			  _Tr("MainScreen", "Up to date ({0})", ReleaseVersion::Current().ToString());
			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos =
			  MakeVector2(pos.x + size.x - txtSize.x, pos.y + (size.y - txtSize.y) * 0.5F);
			font->DrawShadow(text, txtPos, 1.0F, kUpToDateColor,
			                 MakeVector4(0.0F, 0.0F, 0.0F, 0.4F));

			// A check mark drawn as two strokes, so it does not depend on the font
			// carrying a check glyph.
			const float s = kCheckMarkSize;
			const float x = txtPos.x - kCheckMarkGap - s;
			const float y = pos.y + (size.y - s) * 0.5F;
			const Vector2 start = MakeVector2(x, y + s * 0.55F);
			const Vector2 corner = MakeVector2(x + s * 0.38F, y + s * 0.9F);
			const Vector2 end = MakeVector2(x + s, y + s * 0.1F);
			ui::SetColorNP(r, kUpToDateColor);
			r.DrawLine(start, corner, kCheckMarkThickness);
			r.DrawLine(corner, end, kCheckMarkThickness);
		}

		void UpdateNotice::Render() {
			Refresh();
			if (releaseUrl.empty()) {
				if (showUpToDate && upToDate && !(upToDateCovered && upToDateCovered()))
					RenderUpToDate();
				return;
			}

			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			float fill = 0.25F;
			if (pressed && hover)
				fill = 0.55F;
			else if (hover)
				fill = 0.4F;

			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, fill));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));
			// A download's progress, as a brighter fill from the left
			if (progressFraction > 0.0F) {
				ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, 0.35F));
				r.DrawImage(nullptr,
				            AABB2(pos.x, pos.y, sz.x * std::min(progressFraction, 1.0F), sz.y));
			}
			ui::SetColorNP(r, MakeVector4(kAccent.x, kAccent.y, kAccent.z, 0.7F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			client::IFont* font = GetFont();
			if (!font)
				return;

			Vector2 txtSize = font->Measure(caption);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;
			font->DrawShadow(caption, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F),
			                 MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
		}

		// -- UpdatePromptScreen --

		void UpdatePromptScreen::ShowIfPending(ui::UIElement* owner,
		                                       std::function<void()> releasePageOpened) {
			// Wait for the owner to be free, so the prompt never stacks on top of
			// another dialog (e.g. the first-launch profile prompt).
			if (!owner->IsEnabled())
				return;
			if (!static_cast<bool>(cl_zsUpdatePrompt))
				return;
			if (!UpdateChecker::Get().ClaimStartupPrompt())
				return;

			UpdateChecker::Result result = UpdateChecker::Get().GetResult();
			const bool selfUpdate = SelfUpdater::IsSupported();
			std::string text =
			  _Tr("MainScreen", "A new version of ZeroSpades X is available!\n\n"
			                    "Installed version: {0}\nLatest version: {1}",
			      ReleaseVersion::Current().ToString(), result.latestVersion.ToString());
			// What it brings, from the release's notes: a reason to update now.
			float height = kPromptHeight;
			if (!result.changes.empty()) {
				text += "\n\n" + _Tr("MainScreen", "What's new:");
				for (const std::string& change : result.changes)
					text += "\n- " + change;
				height += 22.0F * static_cast<float>(result.changes.size() + 2);
			}
			text += "\n\n";
			text += selfUpdate
			          ? _Tr("MainScreen",
			                "Update now? It downloads while you stay in the menu; then click the "
			                "banner at the top to restart into it.")
			          : _Tr("MainScreen",
			                "Open the release page to download it? ZeroSpades X will close so the "
			                "update can be installed.");

			Handle<UpdatePromptScreen> prompt = Handle<UpdatePromptScreen>::New(
			  owner, text, result.releaseUrl, result.latestTag, selfUpdate,
			  std::move(releasePageOpened), height);
			prompt->Run();
		}

		UpdatePromptScreen::UpdatePromptScreen(ui::UIElement* owner, const std::string& text,
		                                       const std::string& releaseUrl,
		                                       const std::string& releaseTag, bool selfUpdate,
		                                       std::function<void()> releasePageOpened,
		                                       float height)
		    : MessageBoxScreen(owner, text,
		                       {selfUpdate ? _Tr("MainScreen", "Update Now")
		                                   : _Tr("MainScreen", "Open Release Page"),
		                        _Tr("MainScreen", "Later")},
		                       height, true) {
			closed = [releaseUrl, releaseTag, selfUpdate,
			          releasePageOpened](ui::UIElement& sender) {
				if (static_cast<UpdatePromptScreen&>(sender).resultIndex != 0)
					return;
				if (selfUpdate)
					SelfUpdater::Get().Start(releaseTag);
				else
					OpenReleasePage(releaseUrl, releasePageOpened);
			};
		}

		UpdatePromptScreen::~UpdatePromptScreen() {
			// Dropped without an answer: a resize rebuilds the main screen and
			// detaches every dialog. Let the rebuilt menu show the prompt again.
			if (resultIndex < 0)
				UpdateChecker::Get().ReleaseStartupPrompt();
		}

		void UpdatePromptScreen::HotKey(const std::string& key) {
			// Enter is swallowed rather than bound to Open Release Page: the prompt
			// appears on its own, so an Enter meant for the menu (joining a server)
			// would otherwise open the browser and quit the game.
			if (key == "Enter")
				return;
			if (IsEnabled() && key == "Escape") {
				EndDialog(1);
			} else {
				MessageBoxScreen::HotKey(key);
			}
		}
	} // namespace gui
} // namespace spades
