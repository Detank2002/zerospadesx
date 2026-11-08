/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include <Training/TrainingCourse.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Client.h"

#include <Core/Bitmap.h>
#include <Core/ConcurrentDispatch.h>
#include <Core/FileManager.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Draw/SW/SWRenderer.h>

#include "CTFGameMode.h"
#include "CenterMessageView.h"
#include "ChatWindow.h"
#include "ClientPlayer.h"
#include "ClientUI.h"
#include "Fonts.h"
#include "GameProperties.h"
#include "HitTestDebugger.h"
#include "HurtRingView.h"
#include "IFont.h"
#include "IGameMode.h"
#include "LimboView.h"
#include "MapView.h"
#include "PaletteView.h"
#include "PieMenuView.h"
#include "ScreenShot.h"
#include "ScoreboardView.h"
#include "TCGameMode.h"
#include "TCProgressView.h"
#include "Teamplay.h"
#include "TeamplayMarker.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"
#include "VRState.h"

DEFINE_SPADES_SETTING(cg_hitIndicator, "1");
SPADES_SETTING(vr_wristHud);
SPADES_SETTING(cg_reflexScope);
SPADES_SETTING(vr_showFps);
SPADES_SETTING(vr_heldMap);
// Where a headset shows the kill feed: 1 in front of you, at its corner of the
// panel the chat comes up on; 2 on the wrist with the rest of the HUD; 0 not
// at all. Testers took it off the crowded wrist, which left it nowhere.
DEFINE_SPADES_SETTING(vr_killfeed, "1");
DEFINE_SPADES_SETTING(cg_debugAim, "0");
SPADES_SETTING(cg_keyReloadWeapon);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyAttack);
SPADES_SETTING(cg_keyAltAttack);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keyLimbo);
SPADES_SETTING(cg_keyToggleSpectatorNames);
SPADES_SETTING(cg_keySpectatorZoom);
SPADES_SETTING(cg_keyStaffSpectating);
SPADES_SETTING(cg_keyDemoPlayPause);
SPADES_SETTING(cg_keyDemoSeekForward);
SPADES_SETTING(cg_keyDemoSeekBackward);
SPADES_SETTING(cg_keyDemoRecord);
SPADES_SETTING(cg_keyDemoSpeedUp);
SPADES_SETTING(cg_keyDemoSlowDown);
SPADES_SETTING(cg_keyDemoToggleHud);
DEFINE_SPADES_SETTING(cg_stats, "0");
DEFINE_SPADES_SETTING(cg_statsSmallFont, "0");
DEFINE_SPADES_SETTING(cg_playerStats, "0");
DEFINE_SPADES_SETTING(cg_playerStatsShowPlacedBlocks, "0");
DEFINE_SPADES_SETTING(cg_playerStatsHeight, "70");
DEFINE_SPADES_SETTING(cg_hideHud, "0");
DEFINE_SPADES_SETTING(cg_hudColor, "0");
DEFINE_SPADES_SETTING(cg_hudColorR, "255");
DEFINE_SPADES_SETTING(cg_hudColorG, "255");
DEFINE_SPADES_SETTING(cg_hudColorB, "255");
DEFINE_SPADES_SETTING(cg_hudAmmoStyle, "0");
DEFINE_SPADES_SETTING(cg_hudSafezoneX, "1");
DEFINE_SPADES_SETTING(cg_hudSafezoneY, "1");
DEFINE_SPADES_SETTING(cg_hudCompassBar, "1");
DEFINE_SPADES_SETTING(cg_hudPlayerCount, "0");
DEFINE_SPADES_SETTING(cg_hudHealthBar, "1");
DEFINE_SPADES_SETTING(cg_hudHealthAnimation, "1");
DEFINE_SPADES_SETTING(cg_hudReloadIndicator, "1");
DEFINE_SPADES_SETTING(cg_playerNames, "2");
DEFINE_SPADES_SETTING(cg_playerNameX, "0");
DEFINE_SPADES_SETTING(cg_playerNameY, "0");
DEFINE_SPADES_SETTING(cg_playerNamesDead, "1");
DEFINE_SPADES_SETTING(cg_debugHitTestSize, "128");
DEFINE_SPADES_SETTING(cg_debugHitTestFadeTime, "10");
// 0: off, 1: the numbers of bullet hits, 2: grenade hits too. On a server with the
// Damage Markers extension every number comes from the server, which does not say
// what dealt the damage, so any non-zero value shows them all, melee included.
DEFINE_SPADES_SETTING(cg_damageIndicators, "1");
SPADES_SETTING(vr_reloadGesture);
SPADES_SETTING(vr_toolButtons);
DEFINE_SPADES_SETTING(cg_hurtScreenEffects, "1");
DEFINE_SPADES_SETTING(cg_healScreenEffects, "1");

DEFINE_SPADES_SETTING(cg_hudHotbar, "1");
SPADES_SETTING(cg_keyToolSpade);
SPADES_SETTING(cg_keyToolBlock);
SPADES_SETTING(cg_keyToolWeapon);
SPADES_SETTING(cg_keyToolGrenade);

SPADES_SETTING(cg_hudPalette);
SPADES_SETTING(cg_keyCaptureColor);
SPADES_SETTING(cg_keyPaletteLeft);
SPADES_SETTING(cg_keyPaletteRight);
SPADES_SETTING(cg_keyPaletteUp);
SPADES_SETTING(cg_keyPaletteDown);
SPADES_SETTING(cg_keyExtendedPalette);

SPADES_SETTING(cg_minimapSize);

namespace spades {
	namespace client {

		namespace {
			std::string TrKey(const std::string& name) {
				if (name.empty()) {
					return _Tr("Client", "Unbound");
				} else if (name == "LeftMouseButton") {
					return "LMB";
				} else if (name == "RightMouseButton") {
					return "RMB";
				} else if (name == "MiddleMouseButton") {
					return "MMB";
				} else if (name == "MouseButton4") {
					return "MB4";
				} else if (name == "MouseButton5") {
					return "MB5";
				} else if (name == "Control") {
					return "CTRL";
				} else if (name == "Left Ctrl") {
					return "LCTRL";
				} else if (name == "Right Ctrl") {
					return "RCTRL";
				} else {
					return ToUpperCase(name);
				}
			}
		} // namespace

		void Client::TakeScreenShot(bool sceneOnly, bool scoreboardOnly) {
			SceneDefinition sceneDef = CreateSceneDefinition();
			lastSceneDef = sceneDef;
			UpdateMatrices();

			// render scene
			flashDlights = flashDlightsOld;
			DrawScene();

			if (scoreboardOnly) {
				scoreboard->Draw();
			} else if (!sceneOnly) {
				// draw 2d
				Draw2D();
			}

			// Well done!
			renderer->FrameDone();

			try {
				auto name = SaveScreenShot(*renderer);

				std::string msg = sceneOnly
					? _Tr("Client", "Sceneshot saved: {0}", name)
					: _Tr("Client", "Screenshot saved: {0}", name);
				ShowAlert(msg, AlertType::Notice);

				PlayScreenshotSound();
			} catch (const Exception& ex) {
				auto msg = _Tr("Client", "Screenshot failed: ");
				msg += ex.GetShortMessage();
				ShowAlert(msg, AlertType::Error);
				SPLog("Screenshot failed: %s", ex.what());
			} catch (const std::exception& ex) {
				auto msg = _Tr("Client", "Screenshot failed: ");
				msg += ex.what();
				ShowAlert(msg, AlertType::Error);
				SPLog("Screenshot failed: %s", ex.what());
			}
		}

#pragma mark - HUD Drawings

		void Client::DrawSplash() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));

			Handle<IImage> img = renderer->RegisterImage("Gfx/Title/Logo.png");

			Vector2 size = {img->GetWidth(), img->GetHeight()};
			size *= std::min(1.0F, sw / size.x);
			size *= std::min(1.0F, sh / size.y);

			float pulse = fabsf(sinf(time));
			size *= 1.0F - (pulse * (pulse * 0.25F));

			Vector2 pos = (MakeVector2(sw, sh) - size) * 0.5F;

			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawImage(img, AABB2(pos.x, pos.y, size.x, size.y));
		}

		void Client::DrawStartupScreen() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			DrawSplash();

			IFont& font = fontManager->GetGuiFont();
			std::string str = _Tr("Client", "NOW LOADING");
			Vector2 size = font.Measure(str);
			Vector2 pos = (MakeVector2(sw, sh) - 16.0F) - size;
			font.DrawShadow(str, pos, 1.0F, MakeVector4(1, 1, 1, 1), MakeVector4(0, 0, 0, 0.5));
		}

		void Client::DrawDemoPlaybackHUD() {
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			const float duration = demoNet->GetDuration();
			const float demoTime = demoNet->GetTime();
			const float demoSpeed = demoNet->GetSpeed();
			const float progress = (duration > 0.0F) ? (demoTime / duration) : 0.0F;

			bool isPaused = demoNet->IsPaused();

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.5F);

			Vector4 barColor = isPaused
				? MakeVector4(0.7F, 0.7F, 0.7F, 1)
				: MakeVector4(0.2F, 0.8F, 1.0F, 1);
			Vector4 barBackgroundColor = MakeVector4(0, 0, 0, 1) * 0.6F;
			Vector4 barOutlineColor = MakeVector4(1, 1, 1, 1) * 0.1F;

			// minimal HUD: just a 2px progress bar at the bottom
			if (!demoHudVisible) {
				const float barH = 2.0F;
				demoTimelineRect = AABB2(0.0F, sh - barH, sw, barH);
				demoTimelineVisible = true;
				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.4F));
				renderer->DrawFilledRect(0, sh - barH, sw, sh);
				renderer->SetColorAlphaPremultiplied(barColor);
				renderer->DrawFilledRect(0, sh - barH, sw * progress, sh);
				return;
			}

			auto fmtTime = [](float t) -> std::string {
				int mins = static_cast<int>(t) / 60;
				int secs = static_cast<int>(t) % 60;
				char buf[16];
				snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
				return buf;
			};

			const std::string curStr = fmtTime(demoTime);
			const std::string durStr = fmtTime(duration);

			char spdBuf[16];
			snprintf(spdBuf, sizeof(spdBuf), (demoSpeed == 1.0F) ? "%.0fx" : "%.2gx", demoSpeed);

			IFont& font = fontManager->GetGuiFont();

			const Vector2 curSize = font.Measure(curStr);
			const Vector2 durSize = font.Measure(durStr);
			const Vector2 spdSize = font.Measure(spdBuf);

			const float statusIconSize = 16.0F;
			const float edgeMargin = 8.0F;
			const float labelGap = 8.0F;
			const float barGap = 6.0F;

			const float statusX = edgeMargin;
			const float speedFixedW = font.Measure("0.00x").x;
			const float controlsW = statusIconSize + labelGap + speedFixedW;
			const float timelineX = edgeMargin + controlsW + labelGap;
			const float speedX = statusX + statusIconSize + labelGap;

			const float barH = 8.0F;
			const float barX = timelineX + curSize.x + barGap;
			const float barY = (sh - edgeMargin) - barH;
			const float barW = (sw - edgeMargin) - durSize.x - barGap - barX;
			const float progressBarW = barW * progress;

			// Remember the bar so the mouse can hit-test it (see IsOverDemoTimeline).
			demoTimelineRect = AABB2(barX, barY, barW, barH);
			demoTimelineVisible = true;

			const float textY = barY + (barH - curSize.y) * 0.5F;

			// draw bar background
			renderer->SetColorAlphaPremultiplied(barBackgroundColor);
			renderer->DrawFilledRect(0, barY - edgeMargin, sw, sh);
			renderer->SetColorAlphaPremultiplied(barOutlineColor);
			renderer->DrawFilledRect(0, barY - edgeMargin - 1, sw, barY - edgeMargin);

			// draw bar
			renderer->SetColorAlphaPremultiplied(shadow);
			renderer->DrawFilledRect(barX, barY, barX + barW, barY + barH);

			// draw progress bar
			renderer->SetColorAlphaPremultiplied(barColor);
			renderer->DrawFilledRect(barX, barY, barX + progressBarW, barY + barH);

			// draw bar outline
			renderer->SetColorAlphaPremultiplied(barOutlineColor);
			renderer->DrawOutlinedRect(barX - 1, barY - 1, barX + barW + 1, barY + barH + 1);

			// draw bar playhead
			const float lineW = 4.0F;
			const float lineH = 1.0F;
			const float lineX = barX + progressBarW - lineW * 0.5F;
			renderer->SetColorAlphaPremultiplied(barColor * MakeVector4(0.7F, 0.7F, 0.7F, 1));
			renderer->DrawFilledRect(lineX, barY - lineH, lineX + lineW, barY + barH + lineH);
			renderer->SetColorAlphaPremultiplied(shadow);
			renderer->DrawOutlinedRect(lineX - 1, barY - lineH - 1, lineX + lineW + 1, barY + barH + lineH + 1);

			// draw status playing/paused
			Handle<IImage> playIcon = renderer->RegisterImage("Gfx/Demo/Play.png");
			Handle<IImage> pauseIcon = renderer->RegisterImage("Gfx/Demo/Pause.png");
			const float iconX = statusX;
			const float iconY = barY + (barH - statusIconSize) * 0.5F;
			renderer->SetColorAlphaPremultiplied(white);
			renderer->DrawImage(isPaused ? pauseIcon : playIcon, AABB2(iconX, iconY, statusIconSize, statusIconSize));

			// draw playback speed
			const float speedTextX = speedX + (speedFixedW - spdSize.x) * 0.5F;
			font.DrawShadow(spdBuf, MakeVector2(speedTextX, textY), 1.0F, white, shadow);

			// draw time labels
			font.DrawShadow(curStr, MakeVector2(timelineX, textY), 1.0F, white, shadow);
			font.DrawShadow(durStr, MakeVector2((sw - edgeMargin) - durSize.x, textY), 1.0F, white, shadow);
		}

		bool Client::IsOverDemoTimeline(float x, float y) {
			if (!demoTimelineVisible)
				return false;

			// Be generous: the bar itself can be as thin as 2 px.
			const float padX = 4.0F;
			const float padTop = 12.0F;
			const float minX = demoTimelineRect.GetMinX() - padX;
			const float maxX = demoTimelineRect.GetMinX() + demoTimelineRect.GetWidth() + padX;
			const float minY = demoTimelineRect.GetMinY() - padTop;
			return x >= minX && x <= maxX && y >= minY;
		}

		float Client::DemoTimelineTimeAt(float x) {
			const float w = demoTimelineRect.GetWidth();
			if (w <= 0.0F)
				return 0.0F;
			float t = (x - demoTimelineRect.GetMinX()) / w;
			t = std::max(0.0F, std::min(1.0F, t));
			return t * demoNet->GetDuration();
		}

		void Client::DrawDemoCursor() {
			if (!IsDemoCursorMode() || !demoCursorHasPos)
				return;

			const float sw = renderer->ScreenWidth();
			const float cx = cursor->GetPosition().x;
			const float cy = cursor->GetPosition().y;

			// Hover marker and time tooltip while pointing at the bar.
			if (demoTimelineVisible && (demoTimelineDragging || IsOverDemoTimeline(cx, cy))) {
				const float barMinX = demoTimelineRect.GetMinX();
				const float barMaxX = barMinX + demoTimelineRect.GetWidth();
				const float barY = demoTimelineRect.GetMinY();
				const float barH = demoTimelineRect.GetHeight();
				const float markerX = std::max(barMinX, std::min(barMaxX, cx));

				renderer->SetColorAlphaPremultiplied(MakeVector4(0.9F, 0.9F, 0.9F, 0.9F));
				renderer->DrawFilledRect(markerX - 1.0F, barY, markerX + 1.0F, barY + barH);

				const float t = DemoTimelineTimeAt(cx);
				const int mins = static_cast<int>(t) / 60;
				const int secs = static_cast<int>(t) % 60;
				char buf[16];
				snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);

				IFont& font = fontManager->GetGuiFont();
				const Vector2 size = font.Measure(buf);
				const float tx = std::max(4.0F, std::min(sw - size.x - 4.0F, markerX - size.x * 0.5F));
				const float ty = barY - size.y - 10.0F;
				font.DrawShadow(buf, MakeVector2(tx, ty), 1.0F, MakeVector4(1, 1, 1, 1),
				                MakeVector4(0, 0, 0, 0.5F));
			}

			// The shared pointer, so it looks and animates like the one in the menus.
			cursor->Draw();
		}

		void Client::DrawRecordingIndicator() {
			const float sw = renderer->ScreenWidth();
			
			const int t = static_cast<int>(net->GetDemoRecordingTime());
			char buf[32];
			snprintf(buf, sizeof(buf), "REC %d:%02d", t / 60, t % 60);

			IFont& font = fontManager->GetSmallGuiFont();
			const Vector2 size = font.Measure(buf);
			
			float minimapW = Clamp((float)cg_minimapSize, 32.0F, 256.0F);
			float minimapY = 8.0F;
			const int statsMode = cg_stats;
			if (statsMode == 2 || (statsMode >= 3 && scoreboardVisible))
				minimapY += cg_statsSmallFont ? 10.0F : 20.0F;
			
			const float x = (sw - 8.0F) - minimapW - 8.0F - size.x;
			const float y = minimapY;
			
			const float right = x + size.x + 4.0F;
			const float bottom = y + size.y + 4.0F;

			const Vector4 red = MakeVector4(1.0F, 0.15F, 0.15F, 1.0F);
			const Vector4 black = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
			const Vector4 fade1 = MakeVector4(0.0F, 0.0F, 0.0F, 0.0F);
			const Vector4 fade2 = MakeVector4(0.0F, 0.0F, 0.0F, 0.55F);
			const Vector4 textColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			const Vector4 outline = MakeVector4(0.0F, 0.0F, 0.0F, 0.7F);
			
			const float pulse = 0.5F + 0.5F * sinf(time * 3.5F);
			const Vector4 accent = black + (red - black) * pulse;

			renderer->DrawFilledRectFade(x - 8.0F, y, right - 2.0F, bottom, fade1, fade2, true);
			renderer->SetColorAlphaPremultiplied(accent);
			renderer->DrawFilledRect(right - 2.0F, y, right, bottom);

			font.DrawOutline(buf, MakeVector2(x, y + 1.0F), 1.0F, textColor, outline);
		}

		void Client::DrawAlivePlayersCount() {
			const int playerCountMode = cg_hudPlayerCount;
			if (playerCountMode >= 3)
				return; // draw on scoreboard

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float teamBarW = 30.0F;
			float teamBarH = 40.0F;

			float x = sw * 0.5F;
			float y = 8.0F;

			const int statsMode = cg_stats;

			bool isDemoMode = IsDemoMode();
			bool statsOnTop = statsMode == 2
				|| (statsMode >= 3 && scoreboardVisible)
				|| (statsMode == 1 && isDemoMode);
			bool playerCountOnTop = (playerCountMode > 0
				&& playerCountMode < 2)
				|| (playerCountMode >= 1 && playerCountMode < 3 && isDemoMode); // force on screen top for demo mode

			// account for client stats height
			if ((statsOnTop && playerCountOnTop)
				|| ((statsMode > 0 && statsMode == 1) && !playerCountOnTop))
				y += cg_statsSmallFont ? 10.0F : 20.0F;

			float teamBarY = playerCountOnTop ? y : ((sh - y) - teamBarH);

			Handle<IImage> img;
			IFont& font = fontManager->GetHeadingFont();
			Vector2 pos, size;
			std::string str;

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 col1 = ConvertColorRGBA(world->GetTeamColor(0));
			Vector4 col2 = ConvertColorRGBA(world->GetTeamColor(1));
			Vector4 brightCol1 = col1 + (white - col1) * 0.5F;
			Vector4 brightCol2 = col2 + (white - col2) * 0.5F;
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5);

			// draw team bar
			size.y = 2.0F;
			renderer->SetColorAlphaPremultiplied(brightCol1);
			renderer->DrawImage(nullptr, AABB2(x - teamBarW, teamBarY, teamBarW, size.y));
			renderer->SetColorAlphaPremultiplied(brightCol2);
			renderer->DrawImage(nullptr, AABB2(x, teamBarY, teamBarW, size.y));

			// draw shadow
			const float shadowSolid = 12.0F;
			const float shadowFade = 28.0F;
			const float shadowTotal = shadowSolid + shadowFade;
			Vector4 shadow1 = MakeVector4(0, 0, 0, 0.4F), shadow2 = MakeVector4(0, 0, 0, 0);
			renderer->DrawFilledRectFadeSolid(
				x - teamBarW, teamBarY + 2.0F, x + teamBarW, teamBarY + shadowTotal,
				teamBarY + shadowSolid, teamBarY + shadowTotal, shadow1, shadow2
			);

			// draw player icon
			img = renderer->RegisterImage("Gfx/User.png");
			float iconSize = 12.0F;
			float iconSpacing = 5.0F;
			pos.x = x - (teamBarW * 0.5F) - (iconSize * 0.5F);
			pos.y = (teamBarY - 2.0F) + iconSize - iconSpacing;
			renderer->SetColorAlphaPremultiplied(shadowColor);
			renderer->DrawImage(img, pos + MakeVector2(1, 1));
			renderer->SetColorAlphaPremultiplied(brightCol1);
			renderer->DrawImage(img, pos);

			pos.x = x + (teamBarW * 0.5F) - (iconSize * 0.5F);
			renderer->SetColorAlphaPremultiplied(shadowColor);
			renderer->DrawImage(img, pos + MakeVector2(1, 1));
			renderer->SetColorAlphaPremultiplied(brightCol2);
			renderer->DrawImage(img, pos);

			// count alive players
			int numAliveTeam1 = 0, numAliveTeam2 = 0;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (!player.IsAlive())
					continue;
				int teamId = player.GetTeamId();
				if (teamId == 0)
					numAliveTeam1++;
				else if (teamId == 1)
					numAliveTeam2++;
			}

			// draw player count
			str = ToString(numAliveTeam1);
			size = font.Measure(str);
			pos.x = x - (teamBarW * 0.5F) - (size.x * 0.5F);
			pos.y = (teamBarY - 2.0F) + iconSize + iconSpacing;
			font.Draw(str, pos + MakeVector2(1, 1), 1.0F, shadowColor);
			font.Draw(str, pos, 1.0F, brightCol1);

			str = ToString(numAliveTeam2);
			size = font.Measure(str);
			pos.x = x + (teamBarW * 0.5F) - (size.x * 0.5F);
			font.Draw(str, pos + MakeVector2(1, 1), 1.0F, shadowColor);
			font.Draw(str, pos, 1.0F, brightCol2);
		}

		void Client::DrawHurtSprites() {
			float per = (time - lastHurtTime) / 1.5F;
			if (per < 0.0F || per > 1.0F)
				return;

			Handle<IImage> img = renderer->RegisterImage("Gfx/HurtSprite.png");
			Vector2 size = {img->GetWidth(), img->GetHeight()};

			Vector2 scrSize = {renderer->ScreenWidth(), renderer->ScreenHeight()};
			Vector2 scrCenter = scrSize * 0.5F;

			float radius = scrSize.GetLength() * 0.5F;

			for (const auto& spr : hurtSprites) {
				float alpha = spr.strength - per;
				if (alpha < 0.0F)
					continue;
				if (alpha > 1.0F)
					alpha = 1.0F;

				float c = cosf(spr.angle);
				float s = sinf(spr.angle);

				Vector2 radDir = {c, s};
				Vector2 angDir = {-s, c};
				float siz = spr.scale * radius;
				Vector2 base = radDir * radius + scrCenter;
				Vector2 centVect = radDir * (-siz);
				Vector2 sideVect1 = angDir * (siz * 4.0F * (spr.horzShift));
				Vector2 sideVect2 = angDir * (siz * 4.0F * (spr.horzShift - 1.0F));

				Vector2 v1 = base + centVect + sideVect1;
				Vector2 v2 = base + centVect + sideVect2;
				Vector2 v3 = base + sideVect1;

				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, alpha));
				renderer->DrawImage(img, v1, v2, v3, AABB2(0, 8, size.x, size.y));
			}
		}

		void Client::DrawScreenEffect(bool hurt, float fadeTime) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			Player& p = world->GetLocalPlayer().value();
			float hpper = p.GetHealth() / 100.0F;

			float lastEffectTime = hurt ? lastHurtTime : lastHealTime;
			float timeSinceEffect = time - lastEffectTime;

			if (time >= lastEffectTime && timeSinceEffect < fadeTime) {
				float prg = 1.0F - (timeSinceEffect / fadeTime);
				prg *= 0.3F + (1.0F - hpper) * 0.7F;
				prg = 1.0F - std::min(prg, 0.9F);

				if (dynamic_cast<draw::SWRenderer*>(&GetRenderer())) {
					prg = (1.0F - prg) * 0.5F;
					renderer->SetColorAlphaPremultiplied(hurt
						? MakeVector4(prg, 0.0F, 0.0F, prg)
						: MakeVector4(0.0F, prg, 0.0F, prg));
					renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
				} else {
					renderer->MultiplyScreenColor(hurt
						? MakeVector3(1.0F, prg, prg)
						: MakeVector3(prg, 1.0F, prg));

					prg = (1.0F - prg) * 0.1F;
					renderer->SetColorAlphaPremultiplied(hurt
						? MakeVector4(prg, 0.0F, 0.0F, prg)
						: MakeVector4(0.0F, prg, 0.0F, prg));
					renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
				}
			}
		}

		Vector4 Client::GetPlayerColor(Player& player) {
			Vector4 playerColor = MakeVector4(1, 1, 1, 1);

			Vector3 eye = lastSceneDef.viewOrigin;
			Vector3 dir = player.GetEye() - eye;

			float dist = dir.GetSquaredLength2D();

			float fadeStart = FOG_DISTANCE + 10.0F;
			float fadeEnd = fadeStart + 10.0F;
			float fade = (sqrtf(dist) - fadeStart) / (fadeEnd - fadeStart);
			float alpha = Clamp(1.0F - fade, 0.1F, 1.0F);

			// camera coincides with the player's eye (e.g. mid-spectator-switch);
			// skip the raycast since direction is undefined
			GameMap::RayCastResult mapResult;
			if (dir.GetSquaredLength() < 1.0E-6F) {
				mapResult.hit = false;
			} else {
				mapResult = map->CastRay2(eye, dir, 256);
			}

			if (dist > FOG_DISTANCE_SQ) {
				playerColor = MakeVector4(1, 0.75, 0, 1);
			} else if (mapResult.hit && (mapResult.hitPos - eye).GetSquaredLength2D() < dist) {
				playerColor = player.IsLocalPlayer() ? MakeVector4(0, 1, 1, 1)
													 : ConvertColorRGBA(player.GetColor());
			}

			playerColor.w *= alpha;

			return playerColor;
		}

		void Client::DrawPlayerName(Player& player, const Vector4& color) {
			SPADES_MARK_FUNCTION();

			Vector3 origin = player.GetEye();
			origin.z -= 0.45F; // above player head

			Vector2 scrPos;
			if (Project(origin, scrPos)) {
				scrPos.x += (int)cg_playerNameX;
				scrPos.y += (int)cg_playerNameY;

				std::string str = player.GetName();

				// add distance
				const float dist = (origin - lastSceneDef.viewOrigin).GetLength2D();
				if ((int)cg_playerNames < 2 && dist <= FOG_DISTANCE) {
					char buf[32];
					snprintf(buf, sizeof(buf), " [%.1f]", dist);
					str += buf;
				}

				IFont& font = fontManager->GetGuiFont();

				Vector2 size = font.Measure(str);
				scrPos.x -= size.x * 0.5F;
				scrPos.y -= size.y;

				// rounded for better pixel alignment
				scrPos.x = floorf(scrPos.x);
				scrPos.y = floorf(scrPos.y);

				float luminosity = color.x + color.y + color.z;
				Vector4 shadowColor = (luminosity > 0.9F)
					? MakeVector4(0, 0, 0, 0.8F)
					: MakeVector4(1, 1, 1, 0.8F);

				shadowColor.w *= color.w;

				font.DrawShadow(str, scrPos, 1.0F, color, shadowColor);
			}
		}

		void Client::DrawHottrackedPlayerName() {
			SPADES_MARK_FUNCTION();

			auto hottracked = HotTrackedPlayer();
			if (hottracked) {
				Player& player = std::get<0>(*hottracked);
				if (HasTeamplayChevronName(player))
					return; // the chevron already names them, higher up
				DrawPlayerName(player, MakeVector4(1, 1, 1, 1));
			}
		}

		void Client::DrawPlayerBox(Player& player, const Vector4& color) {
			SPADES_MARK_FUNCTION();

			// get player aabb
			const auto& box = player.GetBox().Inflate(0.25F);

			// corners of the 3d box
			std::array<Vector3, 8> corners = {{
				{box.min.x, box.min.y, box.min.z},
				{box.min.x, box.max.y, box.min.z},
				{box.max.x, box.max.y, box.min.z},
				{box.max.x, box.min.y, box.min.z},
				{box.max.x, box.max.y, box.max.z},
				{box.min.x, box.max.y, box.max.z},
				{box.min.x, box.min.y, box.max.z},
				{box.max.x, box.min.y, box.max.z}
			}};

			float left = std::numeric_limits<float>::max();
			float top = std::numeric_limits<float>::max();
			float right = -std::numeric_limits<float>::max();
			float bottom = -std::numeric_limits<float>::max();

			// get screen positions
			Vector2 scrPos;
			for (auto const& corner : corners) {
				if (!Project(corner, scrPos))
					return; // offscreen or behind camera

				left = std::min(left, scrPos.x);
				right = std::max(right, scrPos.x);
				top = std::min(top, scrPos.y);
				bottom = std::max(bottom, scrPos.y);
			}

			// rounded for better pixel alignment
			left = floorf(left);
			top = floorf(top);
			right = ceilf(right);
			bottom = ceilf(bottom);

			// draw boxes
			Vector4 shadowP = MakeVector4(0, 0, 0, 0.25F * color.w).Premultiplied();
			Vector4 colorP = color.Premultiplied();

			renderer->SetColorAlphaPremultiplied(shadowP);
			renderer->DrawOutlinedRect(left - 1, top - 1, right + 1, bottom + 1);

			renderer->SetColorAlphaPremultiplied(colorP);
			renderer->DrawOutlinedRect(left, top, right, bottom);
		}

		void Client::DrawPubOVL() {
			SPADES_MARK_FUNCTION();

			auto cameraMode = GetCameraMode();
			bool isFollowingNonLocal = FollowsNonLocalPlayer(cameraMode);

			int focusedPlayerId = GetCameraTargetPlayerId();
			stmp::optional<Player&> maybeTarget;
			if (focusedPlayerId >= 0 && focusedPlayerId < static_cast<int>(world->GetNumPlayerSlots()))
				maybeTarget = world->GetPlayer(focusedPlayerId);

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue; // player is non-existent

				Player& p = maybePlayer.value();
				if (p.IsSpectator() || !p.IsAlive())
					continue; // don't draw dead players or spectators

				// dont draw the focused player name when following non-local players
				if (isFollowingNonLocal && maybeTarget && &p == &maybeTarget.value())
					continue;

				// Do not draw a player with an invalid state
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue;

				const auto& color = GetPlayerColor(p);
				if (staffSpectating)
					DrawPlayerBox(p, color);
				if (spectatorPlayerNames && !HasTeamplayChevronName(p))
					DrawPlayerName(p, color);
			}
		}

#pragma mark - Teamplay

		Vector4 Client::GetTeamplayTeamColor(Player& p, float alpha) {
			// The extension is explicit that a TEAM_ESP highlight uses the team colour
			// the server sent in State Data, not the player's own block colour, so the
			// highlight reads as team information at a glance.
			Vector4 color = ConvertColorRGBA(world->GetTeamColor(p.GetTeamId()));
			color.w = alpha;
			return color;
		}

		bool Client::HasTeamplayChevronName(Player& p) {
			SPADES_MARK_FUNCTION();

			if (!world || !p.IsAlive() || p.IsSpectator())
				return false;

			auto maybeLocal = world->GetLocalPlayer();
			if (!maybeLocal)
				return false;

			Player& local = maybeLocal.value();
			if (&p == &local)
				return false;

			// DrawEspMarks draws the chevron for a mark that names the world surface, but
			// names the player only when the server asked for it; with SHOW_NAME clear the
			// ordinary label is left to do its own job, since it is the client's feature
			// and not the mark speaking.
			auto mark = teamplay->GetMark(p.GetId());
			if (mark && (mark->surfaces & Teamplay::SurfaceWorld))
				return mark->showName;

			// DrawTeamOverlay draws one for every teammate while the key is held, marked
			// or not, unless a world mark above took the chevron over.
			return teamplay->IsTeamESPEnabled() && teamOverlayAlpha > 0.0F &&
				   !local.IsSpectator() && local.IsTeammate(p);
		}

		void Client::DrawPlayerChevron(Player& p, const Vector4& color, bool showWeapon,
									   bool showName, const std::string& note) {
			SPADES_MARK_FUNCTION();

			float alpha = color.w;
			if (alpha <= 0.0F)
				return;

			Vector3 origin = p.GetEye();
			origin.z -= 0.95F; // clear of the helmet at any pitch

			Vector2 scrPos;
			if (!Project(origin, scrPos))
				return;

			scrPos.x = floorf(scrPos.x) + 0.5F;
			scrPos.y = floorf(scrPos.y) + 0.5F;

			IFont& font = fontManager->GetGuiFont();

			// Sized against the screen height so the marker keeps its apparent size on
			// a high-DPI display instead of shrinking to nothing.
			float sz = Clamp(renderer->ScreenHeight() * 0.012F, 9.0F, 22.0F);
			float halfW = sz * 0.95F;
			float depth = sz * 0.7F;
			float thickShadow = std::max(3.0F, sz * 0.32F);
			float thickOutline = std::max(1.5F, sz * 0.18F);

			Vector2 tip = scrPos;
			Vector2 wingL = MakeVector2(scrPos.x - halfW, scrPos.y - depth);
			Vector2 wingR = MakeVector2(scrPos.x + halfW, scrPos.y - depth);

			Vector4 shadow = MakeVector4(0, 0, 0, 0.6F * alpha);
			Vector4 outline = color.Premultiplied();

			renderer->SetColorAlphaPremultiplied(shadow);
			renderer->DrawLine(wingL, tip, thickShadow);
			renderer->DrawLine(tip, wingR, thickShadow);
			renderer->SetColorAlphaPremultiplied(outline);
			renderer->DrawLine(wingL, tip, thickOutline);
			renderer->DrawLine(tip, wingR, thickOutline);

			Vector4 textCol = MakeVector4(color.x, color.y, color.z, alpha);
			Vector4 textShadow = MakeVector4(0, 0, 0, 0.7F * alpha);

			// Stack the class weapon icon and then the name above the chevron. The class
			// weapon is shown rather than the held tool: it says what role the player is
			// playing, which is what a teammate needs to know at a glance.
			float stackBottom = scrPos.y - depth - (floorf(sz * 0.35F) + 2.0F);

			Handle<IImage> weaponIcon;
			if (showWeapon) {
				const char* path = "Gfx/Hotbar/Rifle.png";
				switch (p.GetWeaponType()) {
					case RIFLE_WEAPON: path = "Gfx/Hotbar/Rifle.png"; break;
					case SMG_WEAPON: path = "Gfx/Hotbar/SMG.png"; break;
					case SHOTGUN_WEAPON: path = "Gfx/Hotbar/Shotgun.png"; break;
				}
				weaponIcon = renderer->RegisterImage(path);
			}

			if (weaponIcon) {
				const float iconH = 14.0F;
				float iconW = weaponIcon->GetWidth() * (iconH / weaponIcon->GetHeight());
				Vector2 iconPos = MakeVector2(floorf(scrPos.x - iconW * 0.5F),
											  floorf(stackBottom - iconH));
				AABB2 inRect(0, 0, weaponIcon->GetWidth(), weaponIcon->GetHeight());

				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.7F * alpha));
				renderer->DrawImage(weaponIcon.GetPointerOrNull(),
									AABB2(iconPos.x + 1, iconPos.y + 1, iconW, iconH), inRect);

				renderer->SetColorAlphaPremultiplied(MakeVector4(alpha, alpha, alpha, alpha));
				renderer->DrawImage(weaponIcon.GetPointerOrNull(),
									AABB2(iconPos.x, iconPos.y, iconW, iconH), inRect);

				stackBottom = iconPos.y;
			}

			// Whether the name goes with a mark is the server's decision, carried by the
			// SHOW_NAME flag: the outline says where the player is, and the name would
			// hand over who they are, which is not always what the server is revealing.
			if (showName) {
				const std::string& name = p.GetName();
				Vector2 nameSize = font.Measure(name);
				Vector2 namePos = MakeVector2(floorf(scrPos.x - nameSize.x * 0.5F),
											  floorf(stackBottom - nameSize.y - 1.0F));
				font.DrawShadow(name, namePos, 1.0F, textCol, textShadow);

				stackBottom = namePos.y;
			}

			// Everything this marker has to say stays in one stack above the head, so
			// it never collides with the ordinary name label lower down.
			if (!note.empty()) {
				Vector2 noteSize = font.Measure(note);
				Vector2 notePos = MakeVector2(floorf(scrPos.x - noteSize.x * 0.5F),
											  floorf(stackBottom - noteSize.y - 1.0F));
				font.DrawShadow(note, notePos, 1.0F, textCol, textShadow);
			}
		}

		bool Client::IsMarkBlinkPhase() const {
			// Slow enough to read both colours, fast enough that a glance catches both.
			constexpr float kBlinkPeriod = 1.0F;
			return fmodf(time, kBlinkPeriod) < kBlinkPeriod * 0.5F;
		}

		bool Client::IsTeamESPRevealing(Player& p) {
			if (!world || !teamplay->IsTeamESPEnabled() || teamOverlayAlpha <= 0.0F)
				return false;

			auto maybeLocal = world->GetLocalPlayer();
			if (!maybeLocal)
				return false;

			Player& local = maybeLocal.value();
			return &p != &local && !local.IsSpectator() && local.IsTeammate(p);
		}

		Vector3 Client::ResolveMarkColor(Player& p, const IntVector3& markColor) {
			if (IsTeamESPRevealing(p) && !IsMarkBlinkPhase()) {
				Vector4 teamColor = GetTeamplayTeamColor(p, 1.0F);
				return MakeVector3(teamColor.x, teamColor.y, teamColor.z);
			}

			return Teamplay::ToRenderColor(markColor);
		}

		bool Client::ShouldRevealPlayer(Player& p, Vector3& outColor) {
			SPADES_MARK_FUNCTION();

			if (!world || !p.IsAlive() || p.IsSpectator())
				return false;

			// Never the player whose eyes we are looking through: they are not hidden
			// from themselves, and their own outline would frame the whole screen.
			auto maybeLocal = world->GetLocalPlayer();
			if (maybeLocal && &p == &maybeLocal.value())
				return false;

			// A mark is an instruction from the server, so it is drawn whatever the
			// TEAM_ESP bit says — but only on the surfaces the packet named.
			auto mark = teamplay->GetMark(p.GetId());
			if (mark && (mark->surfaces & Teamplay::SurfaceWorld)) {
				outColor = ResolveMarkColor(p, mark->color);
				return true;
			}

			// TEAM_ESP reveals the client's own teammates, in their team's colour, and
			// only while the overlay key spends that permission.
			if (!IsTeamESPRevealing(p))
				return false;

			Vector4 teamColor = GetTeamplayTeamColor(p, 1.0F);
			outColor = MakeVector3(teamColor.x, teamColor.y, teamColor.z);
			return true;
		}

		bool Client::HasRevealedPlayers() {
			SPADES_MARK_FUNCTION();

			// A mark reveals its player whatever the feature bits say, but only one that
			// asked for the world surface puts anything through a wall.
			for (const auto& entry : teamplay->GetMarks()) {
				if (entry.second.surfaces & Teamplay::SurfaceWorld)
					return true;
			}

			// The team overlay only reveals anybody while it is on screen.
			return teamplay->IsTeamESPEnabled() && teamOverlayAlpha > 0.0F;
		}

		void Client::DrawTeamOverlay() {
			SPADES_MARK_FUNCTION();

			// TEAM_ESP is a permission, not an instruction: the server allows the client
			// to reveal its own team, and this client spends that permission only while
			// the overlay key is held, so the view stays clear the rest of the time.
			if (!teamplay->IsTeamESPEnabled())
				return;

			constexpr float kOverlayOpacity = 0.9F;
			float alpha = kOverlayOpacity * teamOverlayAlpha;
			if (alpha <= 0.0F)
				return;

			auto maybeLocal = world->GetLocalPlayer();
			if (!maybeLocal)
				return;

			Player& local = maybeLocal.value();
			if (local.IsSpectator())
				return;

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;

				Player& p = maybePlayer.value();
				if (&p == &local || p.IsSpectator() || !p.IsAlive())
					continue;
				if (!local.IsTeammate(p))
					continue;
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue; // invalid state

				// A mark that names the world surface is drawn by DrawEspMarks with its
				// own presentation; stacking both would double the name above their head.
				// A mark shown only on the minimap or the compass leaves this one alone.
				auto mark = teamplay->GetMark(p.GetId());
				if (mark && (mark->surfaces & Teamplay::SurfaceWorld))
					continue;

				// Showing the name alongside a TEAM_ESP highlight is recommended by the
				// extension and left to the client, which is what this does.
				DrawPlayerChevron(p, GetTeamplayTeamColor(p, alpha), true, true);
			}
		}

		void Client::DrawEspMarks() {
			SPADES_MARK_FUNCTION();

			// A mark is an instruction rather than a permission, so it is drawn whatever
			// the TEAM_ESP bit says and without waiting for the overlay key.
			if (!teamplay->HasMarks())
				return;

			int localPlayerId = world->GetLocalPlayerIndex().value_or(-1);

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				int playerId = static_cast<int>(i);
				if (playerId == localPlayerId)
					continue; // shown as a notice instead, see below

				auto mark = teamplay->GetMark(playerId);
				if (!mark)
					continue;

				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;

				// A mark that did not name the world surface says nothing here; it is
				// on the minimap or the compass instead.
				if (!(mark->surfaces & Teamplay::SurfaceWorld))
					continue;

				Player& p = maybePlayer.value();
				if (p.IsSpectator() || !p.IsAlive())
					continue;
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue; // invalid state

				// The same colour the outline is drawn in, blink and all, so the label
				// never contradicts the shape it belongs to.
				Vector3 revealColor;
				if (!ShouldRevealPlayer(p, revealColor))
					continue;

				constexpr float kMarkOpacity = 0.95F;
				Vector4 color = MakeVector4(revealColor.x, revealColor.y, revealColor.z,
											kMarkOpacity);

				// The Reason, when the server sent one, rides above the name so the
				// audience knows why this player and not another. It is rendered as
				// received: the extension assigns no reason values.
				// The body-and-weapon outline is the renderer's job, driven by
				// ShouldRevealPlayer; here only the name and reason are drawn.
				DrawPlayerChevron(p, color, false, mark->showName, mark->reason);
			}
		}

		void Client::DrawLocalPlayerMarkedIndicator() {
			SPADES_MARK_FUNCTION();

			// The server chooses the audience of a mark, so being included in the
			// recipients for one's own id is how a player learns they are being shown to
			// others. Telling them is the honest reading of that.
			if (!teamplay->HasMarks())
				return;

			auto localPlayerId = world->GetLocalPlayerIndex();
			if (!localPlayerId)
				return;

			auto mark = teamplay->GetMark(localPlayerId.value());
			if (!mark)
				return;

			std::string str = _Tr("Client", "YOU ARE MARKED");
			if (!mark->reason.empty())
				str += " — " + mark->reason;

			// In the colour the server chose, so the notice says the same thing to its
			// subject as the outline says to everybody else.
			Vector3 col = Teamplay::ToRenderColor(mark->color);

			IFont& font = fontManager->GetGuiFont();
			Vector2 size = font.Measure(str);
			Vector2 pos = MakeVector2(floorf((renderer->ScreenWidth() - size.x) * 0.5F),
									  floorf(renderer->ScreenHeight() * 0.16F));

			font.DrawShadow(str, pos, 1.0F, MakeVector4(col.x, col.y, col.z, 1.0F),
							MakeVector4(0, 0, 0, 0.8F));
		}

		void Client::DrawTeamplayPings() {
			SPADES_MARK_FUNCTION();

			if (!teamplay->HasPings())
				return;

			IFont& font = fontManager->GetSmallFont();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			// Sized against the screen height, like the chevron, and kept inside a
			// margin so a ping just off the edge still tells you which way to look.
			// (In an eye, well inside its picture: the edges of an eye's image are
			// past what that eye's lens shows.)
			float sz = Clamp(sh * 0.014F, 10.0F, 26.0F);
			float margin = sz * 2.0F;
			if (vrEyeOverlayPass)
				margin = std::max(margin, std::min(sw, sh) * 0.22F);

			// What is left of a marker the player is facing: enough to keep the callout
			// on screen without hiding what it points at. Down the sights it thins
			// further, since a shot being lined up is what a marker must least sit on,
			// and it thins as the sights come up rather than stepping down when they
			// arrive.
			constexpr float kFacedAlpha = 0.2F;
			constexpr float kScopedFacedAlpha = 0.05F;
			const float aimDownState = GetAimDownState(); // only valid for the local player
			float facedAlpha = Mix(kFacedAlpha, kScopedFacedAlpha, Clamp(aimDownState, 0.0F, 1.0F));

			for (const auto& entry : teamplay->GetPings()) {
				const Teamplay::Ping& ping = entry.second;

				// The packet decides where it is shown; this is the world.
				if (!(ping.surfaces & Teamplay::SurfaceWorld))
					continue;

				// Fade out over the last stretch of whatever lifetime the server gave
				// it, rather than blinking out of existence. A ping the server keeps
				// until it removes it never fades.
				constexpr float kFadeOutTime = 0.75F;
				float alpha = ping.GetFadeAlpha(kFadeOutTime);

				Vector3 rel = ping.position - lastSceneDef.viewOrigin;
				float forward = Vector3::Dot(rel, lastSceneDef.viewAxis[2]);

				Vector2 scrPos;
				bool onScreen = false;
				if (forward > 0.0F && Project(ping.position, scrPos)) {
					onScreen = scrPos.x >= margin && scrPos.x <= sw - margin &&
							   scrPos.y >= margin && scrPos.y <= sh - margin;
				} else {
					// Behind the camera: place the marker on the edge in the direction
					// the player has to turn, using the view-space offset.
					float right = Vector3::Dot(rel, lastSceneDef.viewAxis[0]);
					float up = Vector3::Dot(rel, lastSceneDef.viewAxis[1]);
					Vector2 dir = MakeVector2(right, -up);
					if (dir.GetSquaredLength() < 0.0001F)
						dir = MakeVector2(0.0F, 1.0F);
					dir = dir.Normalize();
					scrPos = MakeVector2(sw * 0.5F, sh * 0.5F) + dir * (sh * 0.5F);
				}

				scrPos.x = Clamp(scrPos.x, margin, sw - margin);
				scrPos.y = Clamp(scrPos.y, margin, sh - margin);

				// A marker stands between the player and the thing it points at — often
				// the marked enemy it was placed on — so it gets out of the way as the
				// aim reaches it. Within half a diamond of the middle of the screen it
				// is down to `facedAlpha`; by one and a half diamonds it is clear of the
				// aim point and fully drawn. Nothing is lost by that: what is left is
				// still legible, the copies on any other surface the ping asked for are
				// untouched, and it is back in full the moment the aim moves off.
				if (onScreen) {
					float dist = (scrPos - MakeVector2(sw, sh) * 0.5F).GetLength();
					alpha *= Mix(facedAlpha, 1.0F,
								 SmoothStep(Clamp((dist - sz * 0.5F) / sz, 0.0F, 1.0F)));
					if (alpha <= 0.0F)
						continue;
				}

				// The colour the server chose, drawn as sent: which colour a ping should
				// be is its business, and this client is never told what one means.
				Vector3 pingCol = Teamplay::ToRenderColor(ping.color);
				Vector4 color = MakeVector4(pingCol.x, pingCol.y, pingCol.z, alpha);

				DrawPingDiamond(*renderer, scrPos, sz, pingCol, alpha, ping.age);

				// Who pinged, and what they said, under the marker. A marker is never
				// anonymous, so that a label is never read as coming from somebody who
				// did not write it. An off-screen ping also gets its distance, which is
				// the only cue left once the marker is pinned to the edge.
				//
				// Player ID 255 is the exception and shows no sender: there is no player
				// behind it, and looking it up in the roster would put the marker under
				// the name of whoever holds a nearby id.
				std::string label;
				if (ping.playerId != kServerPlayerId)
					label = world->GetPlayerName(ping.playerId);

				// Rendered as received: the extension assigns no reason values, so
				// anything the client substituted would be putting words in the
				// sender's mouth.
				if (!ping.reason.empty())
					label += label.empty() ? ping.reason : ": " + ping.reason;
				if (!onScreen) {
					char buf[32];
					snprintf(buf, sizeof(buf), label.empty() ? "[%.0f]" : " [%.0f]",
							 rel.GetLength());
					label += buf;
				}

				// A server ping with no label has nothing to say in words; the marker
				// says it all.
				if (label.empty())
					continue;

				Vector2 size = font.Measure(label);
				Vector2 pos = MakeVector2(floorf(scrPos.x - size.x * 0.5F),
										  floorf(scrPos.y + GetPingDiamondExtent(sz) + 2.0F));
				font.DrawShadow(label, pos, 1.0F, color, MakeVector4(0, 0, 0, 0.8F * alpha));
			}
		}

		void Client::DrawDeadPlayers() {
			SPADES_MARK_FUNCTION();

			Vector3 eye = lastSceneDef.viewOrigin;

			IFont& font = fontManager->GetGuiFont();

			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue; // player is non-existent

				Player& p = maybePlayer.value();
				if (p.IsAlive())
					continue;

				// Do not draw a player with an invalid state
				if (p.GetFront().GetSquaredLength() < 0.01F)
					continue;

				Vector3 origin = p.GetEye();
				origin.z -= 0.9F; // above player

				float dist = (origin - eye).GetLength();
				float fadeStart = 2.0F;
				float fadeEnd = fadeStart + 1.0F;
				float fade = (dist - fadeStart) / (fadeEnd - fadeStart);
				float alpha = Clamp(1.0F - fade, 0.0F, 1.0F);

				if (alpha <= 0.0F)
					continue;

				Vector2 scrPos;
				if (!Project(origin, scrPos))
					continue;

				std::string str = p.GetName();

				Vector2 size = font.Measure(str);
				scrPos.x -= size.x * 0.5F;
				scrPos.y -= size.y;

				// rounded for better pixel alignment
				scrPos.x = floorf(scrPos.x);
				scrPos.y = floorf(scrPos.y);

				Vector4 playerColor = MakeVector4(1, 1, 1, 1);
				Vector4 shadowColor = MakeVector4(0, 0, 0, 0.8F);

				playerColor.w *= alpha;
				shadowColor.w *= alpha;

				font.DrawShadow(str, scrPos, 1.0F, playerColor, shadowColor);
			}
		}

		Client::DamageIndicator Client::DamageIndicator::Make(int playerId, int damage,
		                                                      const Vector3& position,
		                                                      float time, bool scatter) {
			DamageIndicator indicator;
			indicator.damage = Clamp(damage, -kMaxDamage, kMaxDamage);
			indicator.playerId = playerId;
			indicator.position = position;
			indicator.crit = indicator.damage >= kCritDamage;
			indicator.velocity = (scatter && !indicator.crit) ? RandomVector() * 4.0F
			                                                  : MakeVector3(0.0F, 0.0F, 0.0F);
			indicator.velocity.z = -2.0F;
			indicator.Refresh(time);
			return indicator;
		}

		void Client::DamageIndicator::Accumulate(int more, float time) {
			damage = Clamp(damage + Clamp(more, -kMaxDamage, kMaxDamage), -kMaxDamage,
			               kMaxDamage);
			if (!crit && damage >= kCritDamage) {
				crit = true;
				velocity = MakeVector3(0.0F, 0.0F, -2.0F);
			}
			Refresh(time);
		}

		void Client::DamageIndicator::Refresh(float time) {
			fade = crit ? 2.0F : 1.5F;
			lastHitTime = time;
		}

		void Client::UpdateDamageIndicators(float dt) {
			for (auto it = damageIndicators.begin(); it != damageIndicators.end();) {
				DamageIndicator& ent = *it;
				if (time < ent.showAt) {
					++it;
					continue;
				}
				ent.fade -= dt;
				if (ent.fade < 0) {
					std::list<DamageIndicator>::iterator tmp = it++;
					damageIndicators.erase(tmp);
					continue;
				}

				ent.position += ent.velocity * dt;

				++it;
			}
		}
		float Client::DamageIndicatorDelay() {
			// A hit is only shown once the server could have refused it: two
			// players firing at once, the one who dies first sent a hit that the
			// server drops, and its number was already up (a tester's report).
			// About a round trip; the death arrives within it.
			const int ping = activeNet ? activeNet->GetPing() : 0;
			return Clamp(static_cast<float>(ping) / 1000.0F + 0.05F, 0.05F, 0.5F);
		}

		void Client::DrawDamageIndicators() {
			SPADES_MARK_FUNCTION();

			IFont& mediumFont = fontManager->GetMediumFont();
			IFont& guiFont = fontManager->GetGuiFont();

			for (const auto& dmg : damageIndicators) {
				if (time < dmg.showAt)
					continue;
				float fade = dmg.fade;
				if (fade > 1.0F)
					fade = 1.0F;

				Vector2 scrPos;
				if (Project(dmg.position, scrPos)) {
					bool crit = dmg.crit;
					IFont& font = crit ? mediumFont : guiFont;

					// A negative amount is a heal, shown as a gain.
					int damage = dmg.damage;
					const bool heal = damage < 0;
					auto damageStr = heal ? "+" + ToString(-damage) : ToString(damage);
					// In a headset the number sat on the target, which is small at
					// range and has to stay in view while shooting (a tester's
					// report): smaller, and beside it, above and to the right.
					const bool vr = IsVRActive();
					const float scale = vr ? 0.7F : 1.0F;
					Vector2 size = font.Measure(damageStr) * scale;
					scrPos -= size * 0.5F;
					if (vr)
						scrPos += MakeVector2(size.x * 1.2F + 10.0F, -size.y * 1.5F - 6.0F);

					// rounded for better pixel alignment
					scrPos.x = floorf(scrPos.x);
					scrPos.y = floorf(scrPos.y);

					float per = Clamp(damage / 100.0F, 0.0F, 1.0F);
					Vector4 shadow = MakeVector4(0, 0, 0, 0.4F * fade);
					Vector4 color = MakeVector4(1.0F, 1.0F - per, 0.0F, fade);
					if (heal)
						color = MakeVector4(0.3F, 1.0F, 0.4F, fade);

					if (heal) {
						color = MakeVector4(0.3F, 1.0F, 0.3F, fade);
					} else if (crit) {
						float pulse = (sinf((time - dmg.lastHitTime) * 10.0F) * 0.5F) + 0.5F;
						color = MakeVector4(1.0F, pulse * 0.8F, pulse * 0.2F, fade);
					}

					font.DrawShadow(damageStr, scrPos + MakeVector2(1, 1), scale, shadow, shadow);
					font.Draw(damageStr, scrPos, scale, color);
				}
			}
		}

		void Client::DrawDebugAim(Player& p) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			Weapon& weapon = p.GetWeapon();

			float spread = weapon.GetSpread();

			// halve spread when aiming
			const std::size_t viewId = static_cast<std::size_t>(p.GetId());
			float aimDownState = viewId < clientPlayers.size() && clientPlayers[viewId]
			                       ? clientPlayers[viewId]->GetAimDownState()
			                       : 0.0F;
			if (aimDownState > 0.99F)
				spread /= 2;

			// halve spread when crouching (except for shotgun)
			PlayerInput actualInput = p.GetInput();
			WeaponType weaponType = weapon.GetWeaponType();
			if (actualInput.crouch && weaponType != SHOTGUN_WEAPON)
				spread /= 2;

			float fovY = tanf(lastSceneDef.fovY * 0.5F);
			float spreadDistance = (spread * 2) * (sh * 0.5F) / fovY;

			Vector2 center;
			center.x = sw * 0.5F;
			center.y = sh * 0.5F;

			Vector2 p1, p2;
			p1.x = floorf(center.x - spreadDistance);
			p1.y = floorf(center.y - spreadDistance);
			p2.x = ceilf(center.x + spreadDistance);
			p2.y = ceilf(center.y + spreadDistance);

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawOutlinedRect(p1.x - 1, p1.y - 1, p2.x + 1, p2.y + 1);

			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			renderer->DrawOutlinedRect(p1.x, p1.y, p2.x, p2.y);
		}

		void Client::DrawHitTestDebugger() {
			SPADES_MARK_FUNCTION();

			auto* debugger = world->GetHitTestDebugger();
			if (!debugger)
				return;

			auto bmp = debugger->GetBitmap();
			if (bmp) {
				auto img = renderer->CreateImage(*bmp);
				debugHitTestImage.Set(img.GetPointerOrNull());
			}

			if (!debugHitTestImage)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float cfgWndSize = cg_debugHitTestSize;
			Vector2 wndSize = {cfgWndSize, cfgWndSize};

			Vector2 zoomedSize = {512, 512};
			if (sw < zoomedSize.x || sh < zoomedSize.y)
				zoomedSize *= 0.75F;

			if (debugHitTestZoom) {
				float per = debugHitTestZoomState;
				per = 1.0F - per;
				per *= per;
				per = 1.0F - per;
				per = Mix(0.0F, 1.0F, per);
				zoomedSize = Mix(wndSize, zoomedSize, per);
				wndSize = zoomedSize;
			}

			float winX = (sw - 8.0F) - wndSize.x;
			float winY = (sh - 8.0F) - wndSize.y - 64.0F;

			const int statsMode = cg_stats;
			if (statsMode == 1)
				winY -= cg_statsSmallFont ? 2.0F : 12.0F;

			AABB2 inRect(128, 512 - 128, 256, 256 - 512); // flip Y axis
			AABB2 outRect(winX, winY, wndSize.x, wndSize.y);
			if (debugHitTestZoom) {
				outRect.min = MakeVector2(sw - zoomedSize.x, sh - zoomedSize.y) * 0.5F;
				outRect.max = MakeVector2(sw + zoomedSize.x, sh + zoomedSize.y) * 0.5F;
			}

			const float fadeOutStart = cg_debugHitTestFadeTime;
			const float fadeDuration = 1.0F;
			float timeSinceLastHit = world->GetTime() - lastHitTime;
			float fade = timeSinceLastHit - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			float alpha = debugHitTestZoom ? debugHitTestZoomState : fade;
			if (alpha <= 0.0F)
				return;

			renderer->SetColorAlphaPremultiplied(MakeVector4(alpha, alpha, alpha, alpha));
			renderer->DrawImage(debugHitTestImage, outRect, inRect);

			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, alpha));
			renderer->DrawOutlinedRect(outRect.min.x - 1, outRect.min.y - 1, outRect.max.x + 1, outRect.max.y + 1);
		}

		void Client::DrawBlockPaletteHUD(float winY) {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();

			IFont& font = fontManager->GetGuiFont();

			std::vector<std::string> lines;
			lines.push_back(_Tr("Client", "[{0}] Grab color", TrKey(cg_keyCaptureColor)));
			lines.push_back(_Tr("Client", "[{0}/{1}] Navigate up/down",
				TrKey(cg_keyPaletteUp), TrKey(cg_keyPaletteDown)));
			lines.push_back(_Tr("Client", "[{0}/{1}] Navigate left/right",
				TrKey(cg_keyPaletteLeft), TrKey(cg_keyPaletteRight)));
			lines.push_back(_Tr("Client", "[{0}] Toggle extended palette", TrKey(cg_keyExtendedPalette)));

			// add color information
			if ((int)cg_hudPalette >= 2) {
				IntVector3 color = world->GetLocalPlayer()->GetBlockColor();

				char buf[8];
				snprintf(buf, sizeof(buf), "%02X%02X%02X", color.x, color.y, color.z);
				lines.push_back(_Tr("Client", "#{0} / RGB({1}, {2}, {3})",
					std::string(buf), color.x, color.y, color.z));
			}

			float lh = 20.0F;
			float totalHeight = (int)lines.size() * lh;

			float x = sw - 8.0F;
			float y = (winY - 8.0F) - totalHeight;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			// draw each line
			for (const auto& line : lines) {
				Vector2 pos = MakeVector2(x, y);
				pos.x -= font.Measure(line).x;
				font.DrawShadow(line, pos, 1.0F, color, shadow);
				y += lh;
			}
		}

		void Client::DrawPlayerStats() {
			SPADES_MARK_FUNCTION();

			Player& p = world->GetLocalPlayer().value();

			IFont& font = fontManager->GetGuiFont();

			float sh = renderer->ScreenHeight();

			float x = 8.0F;
			float y = sh * 0.5F;
			y -= (float)cg_playerStatsHeight;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			float lh = 20.0F;
			auto addLine = [&](const std::string& text) {
				Vector2 pos = MakeVector2(x, y);
				y += lh;
				font.DrawShadow(text, pos, 1.0F, color, shadow);
			};

			const auto& weaponType = p.GetWeaponType();
			int hits = weaponStats.hits[weaponType];
			int shots = weaponStats.shots[weaponType];
			int accPerc = int(100.0F * (float(hits) / float(std::max(1, shots))));
			addLine(_Tr("Client", "Accuracy: {0}%", accPerc));

			char buf[32];
			snprintf(buf, sizeof(buf), "%.3g", curKills / float(std::max(1, curDeaths)));
			addLine(_Tr("Client", "Kill/Death Ratio: {0}", std::string(buf)));
			addLine(_Tr("Client", "Kill Streak: {0}, Best: {1}", curStreak, bestStreak));
			addLine(_Tr("Client", "Melee Kills: {0}", meleeKills));
			addLine(_Tr("Client", "Grenade Kills: {0}", grenadeKills));

			if (cg_playerStatsShowPlacedBlocks && !activeNet->GetGameProperties()->isGameModeArena)
				addLine(_Tr("Client", "Blocks Placed: {0}", placedBlocks));
		}

		void Client::DrawAlert() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float fade = time - alertDisappearTime;
			fade = std::min(1.0F - fade, 1.0F);
			if (fade <= 0.0F)
				return;

			float borderFade = (time - alertAppearTime) / 0.5F;
			borderFade = Clamp(1.0F - borderFade, 0.0F, 1.0F);

			Handle<IImage> alertIcon = renderer->RegisterImage("Gfx/AlertIcon.png");

			IFont& font = fontManager->GetGuiFont();
			Vector2 textSize = font.Measure(alertContents);
			Vector2 contentsSize = textSize;
			contentsSize.y = std::max(contentsSize.y, 16.0F);
			if (alertType != AlertType::Notice)
				contentsSize.x += 22.0F;

			// add margin
			const float margin = 4.0F;
			contentsSize += margin * 2.0F;
			contentsSize = contentsSize.Floor(); // rounded

			Vector2 pos = MakeVector2(sw, sh) - contentsSize;
			pos *= MakeVector2(0.5F, 0.7F);
			pos.y += 40.0F;
			pos = pos.Floor(); // rounded

			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5F);
			Vector4 color = MakeVector4(0, 0, 0, 1);
			switch (alertType) {
				case AlertType::Notice: color = MakeVector4(0, 0, 0, 1); break;
				case AlertType::Warning: color = MakeVector4(1, 1, 0, 1); break;
				case AlertType::Error: color = MakeVector4(1, 0, 0, 1); break;
			}

			float x = pos.x - margin;
			float y = pos.y;
			float w = pos.x + contentsSize.x + margin;
			float h = pos.y + contentsSize.y;

			// draw background
			renderer->SetColorAlphaPremultiplied(shadowColor * fade);
			renderer->DrawFilledRect(x + 1, y + 1, w - 1, h - 1);

			// draw border
			renderer->SetColorAlphaPremultiplied(color * fade * (1.0F - borderFade));
			renderer->DrawOutlinedRect(x, y, w, h);

			// draw fading border
			if (borderFade > 0.0F) {
				float scale = 8.0F * (1.0F - borderFade);
				renderer->SetColorAlphaPremultiplied(color * borderFade);
				renderer->DrawOutlinedRect(x - scale, y - scale, w + scale, h + scale);
			}

			// draw alert icon
			if (alertType != AlertType::Notice) {
				Vector2 iconPos = pos;
				iconPos.x += margin;
				iconPos.y += (contentsSize.y - 16.0F) * 0.5F;

				renderer->SetColorAlphaPremultiplied(color * fade);
				renderer->DrawImage(alertIcon, iconPos);
			}

			// draw text
			Vector2 textPos = pos;
			textPos.x += (contentsSize.x - textSize.x) - margin;
			textPos.y += (contentsSize.y - textSize.y) * 0.5F;

			color = MakeVector4(1, 1, 1, fade);
			shadowColor.w = 0.5F * fade;

			font.DrawShadow(alertContents, textPos, 1.0F, color, shadowColor);
		}

		Vector4 Client::GetHUDColor(Player& p) {
			IntVector3 col;
			switch ((int)cg_hudColor) {
				case 1: // team color
					col = p.GetColor();
					col += (MakeIntVector3(255, 255, 255) - col) / 2;
					break;
				case 2: col = MakeIntVector3(120, 200, 255); break; // light blue
				case 3: col = MakeIntVector3(0, 100, 255); break; // blue
				case 4: col = MakeIntVector3(230, 100, 255); break; // purple
				case 5: col = MakeIntVector3(255, 50, 50); break; // red
				case 6: col = MakeIntVector3(255, 120, 50); break; // orange
				case 7: col = MakeIntVector3(255, 255, 0); break; // yellow
				case 8: col = MakeIntVector3(0, 255, 0); break; // green
				case 9: col = MakeIntVector3(0, 255, 120); break; // aqua
				case 10: col = MakeIntVector3(255, 120, 150); break; // pink
				default: // custom
					col.x = (int)cg_hudColorR;
					col.y = (int)cg_hudColorG;
					col.z = (int)cg_hudColorB;
					break;
			}
			return ConvertColorRGBA(col);
		}

		void Client::DrawJoinedAlivePlayerHUD() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			float ratio = (sw / sh);
			float safeZoneXMin = 0.75F;
			if (ratio < 0.26F)
				safeZoneXMin = 0.28F;
			else if (ratio < 0.56F)
				safeZoneXMin = 0.475F;

			float safeZoneX = Clamp((float)cg_hudSafezoneX, safeZoneXMin, 1.0F);
			float safeZoneY = Clamp((float)cg_hudSafezoneY, 0.85F, 1.0F);

			float x = (sw - 16.0F) * safeZoneX;
			float y = (sh - 16.0F) * safeZoneY;

			const int statsMode = cg_stats;
			if (statsMode == 1)
				y -= cg_statsSmallFont ? 2.0F : 12.0F;

			// rounded for better pixel alignment
			x = floorf(x);
			y = floorf(y);

			Player& p = world->GetLocalPlayer().value();
			Weapon& weapon = p.GetWeapon();

			Player::ToolType curToolType = p.GetTool();
			WeaponType curWeaponType = weapon.GetWeaponType();

			bool isToolWeapon = curToolType == Player::ToolWeapon;
			bool awaitingReload = weapon.IsAwaitingReloadCompletion();
			bool isReloading = awaitingReload && !weapon.IsReloadSlow();

			Handle<IImage> ammoIcon;
			float spacing = 2.0F;
			int clipNum, clipSize, stockNum, stockMax;

			IFont& squareFont = fontManager->GetSquareDesignFont();

			Vector4 color = GetHUDColor(p);
			float luminosity = color.x + color.y + color.z;
			Vector4 shadowColor = (luminosity > 0.9F)
				? MakeVector4(0, 0, 0, 0.5)
				: MakeVector4(1, 1, 1, 0.5);

			// premultiplied
			Vector4 shadowP = shadowColor.Premultiplied();

			Vector4 white = MakeVector4(1, 1, 1, 1);
			Vector4 red = MakeVector4(1, 0, 0, 1);
			Vector4 gray = MakeVector4(0.4F, 0.4F, 0.4F, 1);
			Vector4 bgColor = (luminosity > 0.9F) ? color * gray : gray;

			switch (curToolType) {
				case Player::ToolSpade:
				case Player::ToolBlock:
					clipNum = stockNum = p.GetNumBlocks();
					clipSize = stockMax = 50;
					break;
				case Player::ToolGrenade:
					clipNum = stockNum = p.GetNumGrenades();
					clipSize = stockMax = 3;
					break;
				case Player::ToolWeapon: {
					switch (curWeaponType) {
						case RIFLE_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/7.62mm.png");
							break;
						case SMG_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/9mm.png");
							spacing = 1.0F;
							break;
						case SHOTGUN_WEAPON:
							ammoIcon = renderer->RegisterImage("Gfx/Bullet/12gauge.png");
							break;
						default: SPInvalidEnum("weapon.GetWeaponType()", curWeaponType);
					}

					clipSize = weapon.GetClipSize();
					clipNum = std::min(weapon.GetAmmo(), clipSize);
					stockNum = weapon.GetStock();
					stockMax = weapon.GetMaxStock();
				} break;
				default: SPInvalidEnum("p.GetTool()", curToolType);
			}

			// draw hitmarks (not in a headset: the middle of the view is not where
			// the gun in your hand points, and the controller rumbles instead)
			if (cg_hitIndicator && hitFeedbackIconState > 0.0F && !IsVRFrameActive()) {
				Handle<IImage> img = renderer->RegisterImage("Gfx/HitFeedback.png");
				Vector2 size = {img->GetWidth(), img->GetHeight()};

				Vector4 color = hitFeedbackFriendly
					? MakeVector4(0.02F, 1, 0.02F, 1)
					: MakeVector4(1, 0.02F, 0.04F, 1);

				renderer->SetColorAlphaPremultiplied(color * hitFeedbackIconState);
				renderer->DrawImage(img, (MakeVector2(sw, sh) - size) * 0.5F);
			}

			// draw damage rings (on the wrist they would point the wrong way: the
			// front panel draws them instead)
			if (!vrWristPass)
				hurtRingView->Draw();

			// draw color palette (not on the wrist, nor the hotbar below: what a
			// glance at the watch is for is health and ammo, testers)
			if (curToolType == Player::ToolBlock && !vrWristPass)
				paletteView->Draw();

			// draw hotbar when unable to use tool
			if (cg_hudHotbar && !vrWristPass &&
			    (!CanLocalPlayerUseTool() || (isToolWeapon && isReloading))) {
				IFont& font = fontManager->GetSmallFont();

				// register tool icons
				Handle<IImage> blockIcon = renderer->RegisterImage("Gfx/Hotbar/Block.png");
				Handle<IImage> grenadeIcon = renderer->RegisterImage("Gfx/Hotbar/Grenade.png");
				Handle<IImage> spadeIcon = renderer->RegisterImage("Gfx/Hotbar/Spade.png");
				Handle<IImage> weaponIcon;
				switch (curWeaponType) {
					case RIFLE_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/Rifle.png");
						break;
					case SMG_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/SMG.png");
						break;
					case SHOTGUN_WEAPON:
						weaponIcon = renderer->RegisterImage("Gfx/Hotbar/Shotgun.png");
						break;
				}

				const int toolCount = 4;
				Handle<IImage> toolIcons[toolCount] = {
					spadeIcon, blockIcon,
					weaponIcon, grenadeIcon
				};
				std::string hotKeys[toolCount] = {
					cg_keyToolSpade, cg_keyToolBlock,
					cg_keyToolWeapon, cg_keyToolGrenade
				};

				const float iconHeight = 20.0F;
				const float iconSpacing = 10.0F;

				float totalWidth = 0.0F;
				for (int i = 0; i < toolCount; i++) {
					if (!p.IsToolSelectable(static_cast<Player::ToolType>(i)))
						continue;
					totalWidth += toolIcons[i]->GetWidth() + iconSpacing;
				}

				Vector2 iconPos = MakeVector2((sw - totalWidth) * 0.5F, sh * 0.6F);

				// rounded for better pixel alignment
				iconPos.x = floorf(iconPos.x);
				iconPos.y = floorf(iconPos.y);

				for (int i = 0; i < toolCount; i++) {
					const auto tool = static_cast<Player::ToolType>(i);
					if (!p.IsToolSelectable(tool))
						continue;

					// draw icon
					Handle<IImage> icon = toolIcons[i];
					float iconWidth = icon->GetWidth();
					Vector2 pos = iconPos;

					Vector4 iconColor = color;
					Vector4 iconShadow = shadowP;

					// fade non-selected icons
					if (tool != curToolType) {
						iconColor *= 0.25F;
						iconShadow *= 0.25F;
					} else {
						float state = 1.0F - hotBarIconState;
						pos.y -= floorf(10.0F * state);
					}

					renderer->SetColorAlphaPremultiplied(iconShadow);
					renderer->DrawImage(icon, pos + MakeVector2(1, 1));

					renderer->SetColorAlphaPremultiplied(iconColor);
					renderer->DrawImage(icon, pos);

					// draw hotkey
					Vector4 hotKeyColor = MakeVector4(1, 1, 1, 1);
					Vector4 hotKeyShadow = MakeVector4(0, 0, 0, 0.8F);
					std::string hotkeyStr = hotKeys[i];
					Vector2 hotkeyPos = iconPos + MakeVector2(iconWidth - 1.0F, iconHeight - 1.0F);
					font.DrawOutline(hotkeyStr, hotkeyPos, 1.0F, hotKeyColor, hotKeyShadow);

					iconPos.x += iconWidth + iconSpacing;
				}
			}

			// draw indicator when the player has the intel
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = static_cast<CTFGameMode&>(mode.value());
				if (ctf.PlayerHasIntel(p)) {
					Handle<IImage> img = renderer->RegisterImage("Gfx/Intel.png");
					Vector2 size = {img->GetWidth(), img->GetHeight()};
					Vector2 pos = MakeVector2(((sw - size.x) * 0.5F) - 64.0F, y - size.y);

					// Strobe
					float pulse = fabsf(sinf(time * 2.0F));
					pulse *= pulse;

					renderer->SetColorAlphaPremultiplied(shadowP * pulse);
					renderer->DrawImage(img, pos + MakeVector2(1, 1));

					renderer->SetColorAlphaPremultiplied(color * pulse);
					renderer->DrawImage(img, pos);
				}
			}

			// draw "press ... to reload"
			if (isToolWeapon) {
				std::string msg = "";
				if (awaitingReload) {
					int reloadIndicatorMode = cg_hudReloadIndicator;
					if (reloadIndicatorMode == 1) {
						msg = _Tr("Client", "Reloading");
					} else if (reloadIndicatorMode == 2) {
						float prg = weapon.GetReloadProgress();
						const float fadeStart = 0.9F;
						float alpha = 1.0F - Clamp((prg - fadeStart) / (1.0F - fadeStart), 0.0F, 1.0F);
						DrawReloadIndicator(MakeVector4(color.x, color.y, color.z, alpha));
					}
				} else if (stockNum > 0 && clipNum < (clipSize / 4)) {
					msg = _Tr("Client", "Press [{0}] to Reload", TrKey(cg_keyReloadWeapon));
				}

				// (Not on the wrist: the watch says "Reloading", and the prompt
				// names a keyboard key)
				if (!msg.empty() && !vrWristPass) {
					IFont& font = fontManager->GetGuiFont();
					Vector2 size = font.Measure(msg);
					Vector2 pos = MakeVector2((sw - size.x) * 0.5F, sh * (2.0F / 3.0F));
					font.DrawShadow(msg, pos, 1.0F, white, MakeVector4(0, 0, 0, 0.5F));
				}
			}

			// On the wrist: a watch's face instead of the corner numbers.
			if (vrWristPass) {
				DrawVRWatch(p, clipNum, stockNum, clipSize, isToolWeapon, isReloading);
				return;
			}

			// draw remaining ammo counter
			{
				bool drawIcon = (int)cg_hudAmmoStyle < 1;

				float per = Clamp((float)clipNum / (float)(clipSize / 3), 0.0F, 1.0F);
				Vector4 ammoCol = color + (red - color) * (1.0F - per);

				per = Clamp((float)stockNum / (float)(stockMax / 3), 0.0F, 1.0F);
				Vector4 stockCol = color + (red - color) * (1.0F - per);

				auto stockStr = ToString(stockNum);
				if (!drawIcon && isToolWeapon)
					stockStr = ToString(clipNum) + "-" + stockStr;

				IFont& font = squareFont;
				Vector2 size = font.Measure(stockStr);
				Vector2 pos = MakeVector2(x, y) - size;

				// draw ammo icon
				if (drawIcon && isToolWeapon) {
					Vector2 iconSize = MakeVector2(ammoIcon->GetWidth(), ammoIcon->GetHeight());
					Vector2 iconPos = MakeVector2(x - (iconSize.x + spacing), y - iconSize.y);

					float reloadPrg = weapon.GetReloadProgress();
					int clip = isReloading ? (int)(clipSize * reloadPrg) : clipSize;

					for (int i = 0; i < clipSize; i++) {
						iconPos.x = x - ((float)(i + 1) * (iconSize.x + spacing));

						// draw icon shadow
						renderer->SetColorAlphaPremultiplied(shadowP);
						renderer->DrawImage(ammoIcon, iconPos + MakeVector2(1, 1));

						// draw icon
						renderer->SetColorAlphaPremultiplied(
						  isReloading ? ((i < clip) ? color : bgColor)
									  : ((clipNum >= i + 1) ? ammoCol : bgColor));
						renderer->DrawImage(ammoIcon, iconPos);
					}

					pos.y -= iconSize.y + 5.0F;
				}

				font.Draw(stockStr, pos + MakeVector2(1, 1), 1.0F, shadowColor);
				font.Draw(stockStr, pos, 1.0F, drawIcon ? stockCol : ammoCol);
			}

			// draw player health
			{
				int hp = lastHealth; // player health
				int maxHealth = 100; // server doesn't send this
				int dmgTaken = damageTaken;

				float hpFrac = Clamp((float)hp / (float)maxHealth, 0.0F, 1.0F);
				float dmgFrac = Clamp((float)dmgTaken / (float)maxHealth, 0.0F, 1.0F);

				Vector4 hpColor = color + (red - color) * (1.0F - hpFrac);

				float timeSinceLastHurt = time - lastHurtTime;
				float hurtTime = timeSinceLastHurt / 0.25F;
				hurtTime = std::max(0.0F, 1.0F - hurtTime);

				const float fadeOutStart = 0.4F;
				const float fadeDuration = 0.3F;
				float fade = timeSinceLastHurt - fadeOutStart;
				fade = 1.0F - (fade / fadeDuration);
				fade = Clamp(fade, 0.0F, 1.0F);

				auto healthStr = ToString(cg_hudHealthAnimation
					? ((int)(hp + dmgTaken * hurtTime)) : hp);
				IFont& font = squareFont;
				Vector2 size = font.Measure(healthStr);
				Vector2 pos = MakeVector2(sw - x, y - size.y);

				if (cg_hudHealthBar) {
					float barW = 45.0F;
					float barH = 4.0F;
					float barX = sw - x;
					float barY = y - barH;
					float barPrg = std::max(1.0F, floorf(barW * hpFrac));

					// draw shadow
					renderer->SetColorAlphaPremultiplied(shadowP);
					renderer->DrawImage(nullptr, AABB2(barX + 1, barY + 1, barW, barH));

					// draw background
					if (hp < maxHealth) {
						renderer->SetColorAlphaPremultiplied(bgColor);
						renderer->DrawImage(nullptr, AABB2(barX, barY, barW, barH));

						// draw damaged portion
						if (fade > 0.0F) {
							Vector4 dmgColor = red + (white - red) * hurtTime;
							float dmgBarW = std::min(ceilf(barW * dmgFrac), barW - barPrg);
							for (float x2 = barPrg; x2 < barPrg + dmgBarW; x2 += 1.0F) {
								float t = (x2 - barPrg) / dmgBarW;
								float per = (1.0F - t * t) * fade;
								renderer->SetColorAlphaPremultiplied(dmgColor * per);
								renderer->DrawImage(nullptr, AABB2(barX + x2, barY, 1.0F, barH));
							}
						}
					}

					// draw health bar
					renderer->SetColorAlphaPremultiplied(color + (white - color) * hurtTime);
					renderer->DrawImage(nullptr, AABB2(barX, barY, barPrg, barH));

					pos.y -= barH + 5.0F;
				}

				font.Draw(healthStr, pos + MakeVector2(1, 1), 1.0F, shadowColor);
				font.Draw(healthStr, pos, 1.0F, hpColor + (white - hpColor) * hurtTime);
			}
		}

		void Client::DrawFirstPersonHUD() {
			SPADES_MARK_FUNCTION();

			// draw weapon skin's 2D layer
			int focusedPlayerId = GetCameraTargetPlayerId();
			Player& p = world->GetPlayer(focusedPlayerId).value();
			if (static_cast<std::size_t>(focusedPlayerId) < clientPlayers.size() &&
			    clientPlayers[focusedPlayerId])
				clientPlayers[focusedPlayerId]->Draw2D();

			// The compass exists only where the server allows it: it is a client-side
			// feature the extension governs, and a client never turns one on by itself.
			// Where it is allowed, the player may still turn it off.
			if (teamplay->IsCompassAllowed() && cg_hudCompassBar)
				DrawCompassBar(p);

			if (cg_debugAim && p.IsToolWeapon())
				DrawDebugAim(p);
		}

		void Client::DrawDeadPlayerHUD() {
			SPADES_MARK_FUNCTION();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			// draw respawn time
			std::string msg = (lastRespawnCount > 0)
				? _Tr("Client", "You will respawn in: {0}", lastRespawnCount)
				: _Tr("Client", "Waiting for respawn");

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.5F);

			// Large on the wrist, where nothing should be small (testers)
			IFont& font = vrWristPass ? fontManager->GetLargeFont() : fontManager->GetGuiFont();
			Vector2 size = font.Measure(msg);
			Vector2 pos = MakeVector2((sw - size.x) * 0.5F, sh / 2.5F);
			font.DrawShadow(msg, pos, 1.0F, color, shadowColor);

			// draw deaths count
			const float fadeOutStart = 1.3F;
			const float fadeDuration = 1.0F;
			float timeSinceDeath = time - lastAliveTime;
			float fade = timeSinceDeath - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			if (fade > 0.0F) {
				color.w = fade;
				shadowColor.w = 0.5F * fade;

				msg = ToString(curDeaths);
				IFont& bigFont = fontManager->GetLargeFont();
				size = bigFont.Measure(msg);
				pos.x = (sw - size.x) * 0.5F;
				pos.y += 30.0F;
				bigFont.DrawShadow(msg, pos, 1.0F, color, shadowColor);
			}
		}

		void Client::DrawSpectateHUD() {
			SPADES_MARK_FUNCTION();

			// On the wrist: larger, and without the lines naming keyboard keys
			IFont& font = vrWristPass ? fontManager->GetMediumFont() : fontManager->GetGuiFont();
			const bool keyHelp = !vrWristPass;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			// In demo mode there's no local player, treat as spectator
			bool isDemoMode = IsDemoMode();
			bool localPlayerIsSpectator = maybePlayer && maybePlayer->IsSpectator();
			bool isSpectating = localPlayerIsSpectator || isDemoMode || staffSpectating;

			float x = sw - 8.0F;
			float minY = sh * 0.5F;
			minY -= 64.0F;

			float y = cg_minimapSize;
			if (y < minY)
				y = minY;
			if (y > 256.0F)
				y = 256.0F;
			y += 32.0F;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadow = MakeVector4(0, 0, 0, 0.7F);

			float lh = vrWristPass ? 30.0F : 20.0F;
			auto addLine = [&](const std::string& text) {
				Vector2 pos = MakeVector2(x, y);
				pos.x -= font.Measure(text).x;
				y += lh;
				font.DrawShadow(text, pos, 1.0F, color, shadow);
			};

			auto cameraMode = GetCameraMode();

			// Help messages (make sure to synchronize these with the keyboard input handler)
			if (FollowsNonLocalPlayer(cameraMode)) {
				int focusedPlayerId = GetCameraTargetPlayerId();
				auto maybeTarget = world->GetPlayer(focusedPlayerId);
				if (maybeTarget) {
					Player& camTarget = maybeTarget.value();
					addLine(_Tr("Client", "Following {0} [#{1}]",
						world->GetPlayerName(focusedPlayerId), focusedPlayerId));

					int secs = (int)roundf(camTarget.GetTimeToRespawn());
					if (secs > 0)
						addLine(_Tr("Client", "Respawning in: {0}", secs));

					y += lh * 0.5F;

					if (keyHelp) {
						if (camTarget.IsAlive())
							addLine(_Tr("Client", "[{0}] Cycle camera mode", TrKey(cg_keyJump)));
						addLine(_Tr("Client", "[{0}/{1}] Next/Prev player",
							TrKey(cg_keyAttack), TrKey(cg_keyAltAttack)));
						if (isSpectating)
							addLine(_Tr("Client", "[{0}] Unfollow", TrKey(cg_keyReloadWeapon)));
					}
				}
			} else if (keyHelp) {
				addLine(_Tr("Client", "[{0}/{1}] Follow a player",
					TrKey(cg_keyAttack), TrKey(cg_keyAltAttack)));
			}

			if (!keyHelp)
				return;

			if (cameraMode == ClientCameraMode::Free) {
				addLine(_Tr("Client", "[{0}/{1}] Go up/down",
					TrKey(cg_keyJump), TrKey(cg_keyCrouch)));
				addLine(_Tr("Client", "[{0}] Hold to Zoom", TrKey(cg_keySpectatorZoom)));
			}

			if (isSpectating) {
				addLine(_Tr("Client", "[{0}] Toggle player names", TrKey(cg_keyToggleSpectatorNames)));
				bool isStaff = activeNet && activeNet->GetGameProperties()->isStaff;
				if (isStaff || isDemoMode)
					addLine(_Tr("Client", "[{0}] Toggle ESP", TrKey(cg_keyStaffSpectating)));
			}

			y += lh * 0.5F;

			// Demo playback controls
			if (isDemoMode) {
				addLine(demoNet->IsPaused()
					? _Tr("Client", "[{0}] Resume", TrKey(cg_keyDemoPlayPause))
					: _Tr("Client", "[{0}] Pause", TrKey(cg_keyDemoPlayPause)));
				addLine(_Tr("Client", "[{0}/{1}] Seek +/-5s",
					TrKey(cg_keyDemoSeekForward), TrKey(cg_keyDemoSeekBackward)));

				char speedBuf[16];
				snprintf(speedBuf, sizeof(speedBuf), "%.2g", demoNet->GetSpeed());
				addLine(_Tr("Client", "[{0}/{1}] Speed: {2}x",
					TrKey(cg_keyDemoSlowDown), TrKey(cg_keyDemoSpeedUp), std::string(speedBuf)));
				addLine(_Tr("Client", "[{0}] Toggle Demo HUD", TrKey(cg_keyDemoToggleHud)));
			} else if (!inGameLimbo) {
				addLine(_Tr("Client", "[{0}] Select Team/Weapon", TrKey(cg_keyLimbo)));
			}
		}

		void Client::DrawCompassBar(Player& p) {
			const float sw = renderer->ScreenWidth();

			const float barW = 280.0F;
			const float barH = 18.0F;
			const float barX = (sw - barW) * 0.5F;

			float barY = 16.0F;

			const int statsMode = cg_stats;
			const int playerCountMode = cg_hudPlayerCount;

			bool isDemoMode = IsDemoMode();
			bool statsOnTop = statsMode == 2
				|| (statsMode >= 3 && scoreboardVisible)
				|| (statsMode == 1 && isDemoMode);
			bool playerCountOnTop = (playerCountMode > 0
				&& playerCountMode < 2)
				|| (playerCountMode >= 1 && playerCountMode < 3 && isDemoMode); // force on screen top for demo mode

			// account for client stats height
			if (statsOnTop)
				barY += cg_statsSmallFont ? 10.0F : 20.0F;

			// account for alive player count height
			if (playerCountOnTop)
				barY += 40.0F;

			const Vector4 color = GetHUDColor(p);
			float luminosity = color.x + color.y + color.z;

			const Vector4 shadow = MakeVector4(0, 0, 0, 0.8F);
			const Vector4 gray = MakeVector4(0.4F, 0.4F, 0.4F, 1);
			const Vector4 bgColor = (luminosity > 0.9F) ? color * gray : gray;

			// premultiplied
			Vector4 shadowP = shadow.Premultiplied();

			// draw background
			renderer->DrawFilledRectFade(barX, barY, barX + barW, barY + barH,
				MakeVector4(0, 0, 0, 0), bgColor);

			// draw border
			const float sideH = barH * 0.5F;
			auto drawBorder = [&](float x, float y, Vector4 col) {
				renderer->SetColorAlphaPremultiplied(col);
				renderer->DrawFilledRect(x - 1, y + (barH - sideH), x, y + barH + 1); // left
				renderer->DrawFilledRect(x + barW, y + (barH - sideH), x + barW + 1, y + barH + 1); // right
				renderer->DrawFilledRect(x, y + barH, x + barW,   y + barH + 1); // bottom
			};
			drawBorder(barX + 1, barY + 1, shadowP);
			drawBorder(barX, barY, color);

			IFont& font = fontManager->GetSmallFont();

			// Every bearing on the bar, the heading included, is read against the north
			// the server last sent in the Teamplay Config.
			const float yawDeg = teamplay->GetBearing(p.GetFront2D().GetXY());
			const float range = 120.0F;

			// draw labels and ticks
			std::array<std::string, 8> cardinalLabels = {
				_Tr("Client", "N"),  _Tr("Client", "NE"),
				_Tr("Client", "E"),  _Tr("Client", "SE"),
				_Tr("Client", "S"),  _Tr("Client", "SW"),
				_Tr("Client", "W"),  _Tr("Client", "NW"),
			};
			auto drawLabel = [&](const char* label, float px, float alpha) {
				Vector2 size = font.Measure(label);
				float tx = floorf(px - size.x * 0.5F);
				float ty = floorf(barY + (barH - size.y) * 0.5F);
				Vector4 colorLabel = MakeVector4(color.x, color.y, color.z, color.w * alpha);
				Vector4 shadowLabel = MakeVector4(shadow.x, shadow.y, shadow.z, shadow.w * alpha);
				font.DrawOutline(label, MakeVector2(tx, ty), 1.0F, colorLabel, shadowLabel);
			};

			const int tickStep = 5;
			const int cardinalStep = 45;
			const int intermediateOffset = (int)roundf(cardinalStep / 2.0F / tickStep) * tickStep;
			for (int deg = 0; deg < 360; deg += tickStep) {
				float delta = std::remainderf((float)deg - roundf(yawDeg), 360.0F);
				if (fabsf(delta) > range * 0.5F)
					continue;

				float fadeStart = range * 0.42F;
				float fadeEnd = range * 0.48F;
				float edgeFade = 1.0F - (fabsf(delta) - fadeStart) / (fadeEnd - fadeStart);
				edgeFade = Clamp(edgeFade, 0.0F, 1.0F);

				const int degOffset = deg % cardinalStep;
				bool isCardinal = degOffset == 0;
				float px = roundf(barX + barW * 0.5F + (delta / range) * barW);
				float tH = isCardinal ? barH * 0.55F : barH * 0.3F;

				// draw tick marks
				renderer->SetColorAlphaPremultiplied(shadowP * edgeFade);
				renderer->DrawFilledRect(px + 1, barY + barH - tH, px + 2.0F, barY + barH);
				renderer->SetColorAlphaPremultiplied(color * edgeFade);
				renderer->DrawFilledRect(px, barY + barH - tH, px + 1.0F, barY + barH);

				// draw cardinal labels
				if (isCardinal) {
					const auto& label = cardinalLabels[deg / cardinalStep];
					drawLabel(label.c_str(), px, edgeFade);
					continue;
				}

				// draw intermediate degree labels
				if (degOffset == intermediateOffset) {
					char degLabel[8];
					snprintf(degLabel, sizeof(degLabel), "%d", deg);
					drawLabel(degLabel, px, edgeFade * 0.5F);
				}
			}

			// draw heading degrees
			char degBuf[8];
			snprintf(degBuf, sizeof(degBuf), "%d", (int)roundf(yawDeg) % 360);
			Vector2 degSize = font.Measure(degBuf);
			float degX = floorf(barX + barW * 0.5F - degSize.x * 0.5F);
			float degY = barY + barH + 3.0F;
			font.DrawOutline(degBuf, MakeVector2(degX, degY), 1.0F, color, shadow);

			// draw icons
			Vector2 pos2D = p.GetPosition().GetXY();

			auto drawIcon = [&](Handle<IImage>& icon, Vector3 targetPos, Vector4 color, float size = 12.0F) {
				Vector2 delta = targetPos.GetXY() - pos2D;
				float angle = teamplay->GetBearing(delta);
				float yawDelta = std::remainderf(angle - roundf(yawDeg), 360.0F);
				if (fabsf(yawDelta) > range * 0.5F)
					return;
				float px = roundf(barX + barW * 0.5F + (yawDelta / range) * barW);
				float fade = 1.0F - Clamp((fabsf(yawDelta) - range * 0.42F) / (range * 0.06F), 0.0F, 1.0F);
				renderer->SetColorAlphaPremultiplied(color * fade);
				renderer->DrawImage(icon, AABB2(px - size * 0.5F, barY + (barH - size) * 0.5F, size, size));
			};

			// draw map objects
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = dynamic_cast<CTFGameMode&>(mode.value());
				Handle<IImage> baseIcon = renderer->RegisterImage("Gfx/Map/CommandPost.png");
				Handle<IImage> intelIcon = renderer->RegisterImage("Gfx/Map/Intel.png");

				for (int tId = 0; tId < 2; tId++) {
					CTFGameMode::Team& team = ctf.GetTeam(tId);
					CTFGameMode::Team& otherTeam = ctf.GetTeam(1 - tId);

					// draw base
					drawIcon(baseIcon, team.basePos, color);

					// draw intel
					if (!otherTeam.hasIntel)
						drawIcon(intelIcon, team.flagPos, color);
				}
			} else if (mode && mode->ModeType() == IGameMode::m_TC) {
				auto& tc = dynamic_cast<TCGameMode&>(mode.value());
				Handle<IImage> baseIcon = renderer->RegisterImage("Gfx/Map/CommandPost.png");

				// draw territories
				for (int i = 0; i < tc.GetNumTerritories(); i++)
					drawIcon(baseIcon, tc.GetTerritory(i).pos, color);
			}

			// The objectives above are on the compass by default; everything below is
			// here because a ping or a mark asked for it. A bearing carries no distance
			// and no position, only which way to turn, which is why a callout known by
			// direction alone belongs on this surface.
			//
			// Where a bearing lands on the bar and how much the edge fade leaves of it;
			// `false` when it is outside the visible range or fully faded.
			auto locateBearing = [&](const Vector3& targetPos, float alpha, float& px,
									 float& fade) {
				Vector2 delta = targetPos.GetXY() - pos2D;
				float angle = teamplay->GetBearing(delta);
				float yawDelta = std::remainderf(angle - roundf(yawDeg), 360.0F);
				if (fabsf(yawDelta) > range * 0.5F)
					return false;

				px = roundf(barX + barW * 0.5F + (yawDelta / range) * barW);
				fade = 1.0F - Clamp((fabsf(yawDelta) - range * 0.42F) / (range * 0.06F),
									0.0F, 1.0F);
				fade *= alpha;
				return fade > 0.0F;
			};

			// A mark follows a player, so it gets a full-height stripe rather than an
			// icon: it reads as a direction, not as a thing sitting at a place.
			auto drawBearing = [&](const Vector3& targetPos, const Vector3& col, float alpha) {
				float px, fade;
				if (!locateBearing(targetPos, alpha, px, fade))
					return;

				const float w = 2.0F;
				renderer->SetColorAlphaPremultiplied(shadowP * fade);
				renderer->DrawFilledRect(px, barY + 1.0F, px + w + 1.0F, barY + barH);
				renderer->SetColorAlphaPremultiplied(
				  MakeVector4(col.x * fade, col.y * fade, col.z * fade, fade));
				renderer->DrawFilledRect(px, barY, px + w, barY + barH - 1.0F);
			};

			// A ping's pulse rings reach past the bar; keep them on it rather than over
			// the heading readout underneath.
			const bool clipPings = teamplay->HasPings();
			if (clipPings)
				renderer->BeginClippingRect(AABB2(barX, barY, barW, barH));

			for (const auto& entry : teamplay->GetPings()) {
				const Teamplay::Ping& ping = entry.second;
				if (!(ping.surfaces & Teamplay::SurfaceCompass))
					continue;

				constexpr float kFadeOutTime = 0.75F;
				float px, fade;
				if (!locateBearing(ping.position, ping.GetFadeAlpha(kFadeOutTime), px, fade))
					continue;

				// The same diamond the ping wears in the world and on the minimap,
				// centred on the bar and sized to sit inside it with its outline.
				constexpr float kCompassPingHalfSize = 5.0F;
				DrawPingDiamond(*renderer, MakeVector2(px, barY + barH * 0.5F),
								kCompassPingHalfSize, Teamplay::ToRenderColor(ping.color), fade,
								ping.age);
			}

			if (clipPings)
				renderer->EndClippingRect();

			for (const auto& entry : teamplay->GetMarks()) {
				const Teamplay::Mark& mark = entry.second;
				if (!(mark.surfaces & Teamplay::SurfaceCompass))
					continue;

				auto maybeMarked = world->GetPlayer(static_cast<unsigned int>(entry.first));
				if (!maybeMarked)
					continue;

				Player& marked = maybeMarked.value();
				if (marked.IsSpectator() || !marked.IsAlive())
					continue;

				// The blink runs on every surface the mark names, this one included.
				drawBearing(marked.GetPosition(), ResolveMarkColor(marked, mark.color), 1.0F);
			}
		}

		bool Client::IsOnTrainingCourse() {
			GameMap* m = map.GetPointerOrNull();
			if (m != trainingCheckedMap) {
				trainingCheckedMap = m;
				trainingCourse = false;
				if (m && m->IsSolid(training::kMarkerX, training::kMarkerY, training::kMarkerZ)) {
					// The marker's colour, in whichever byte order the map keeps.
					const uint32_t c = m->GetColor(training::kMarkerX, training::kMarkerY,
					                               training::kMarkerZ) & 0xFFFFFFu;
					const uint32_t rgb = training::kMarkerRGB;
					const uint32_t bgr =
					  ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);
					trainingCourse = c == rgb || c == bgr;
				}
			}
			return trainingCourse;
		}

		bool Client::TrainingTip(bool vr, std::string& title, std::string& tip) {
			if (AimTip(vr, title, tip))
				return true;
			if (!world || !IsOnTrainingCourse())
				return false;
			stmp::optional<Player&> p = world->GetLocalPlayer();
			if (!p || !p->IsAlive() || p->IsSpectator())
				return false;
			const training::Station st = training::station_at(p->GetPosition().x);
			const std::string spade = static_cast<std::string>(cg_keyToolSpade),
			                  block = static_cast<std::string>(cg_keyToolBlock),
			                  gun = static_cast<std::string>(cg_keyToolWeapon),
			                  nade = static_cast<std::string>(cg_keyToolGrenade);
			char step[32];
			std::snprintf(step, sizeof(step), "%d / %d", (int)st + 1, (int)training::StationCount);
			// The headset's names for things, for whichever hand holds the gun
			// (vr_leftHanded): a right-handed player's gun hand has A and B, the
			// other hand X and Y.
			const bool rightGun = VRToolHand() == VRHandRight;
			const std::string gunSide = rightGun ? _Tr("Client", "right") : _Tr("Client", "left");
			const std::string otherSide = rightGun ? _Tr("Client", "left") : _Tr("Client", "right");
			const std::string jumpBtn = rightGun ? "A" : "X", reloadBtn = rightGun ? "B" : "Y";
			const std::string colourBtn = rightGun ? "X" : "A", mapBtn = rightGun ? "Y" : "B";
			const std::string cycle = rightGun ? "X / Y" : "A / B";
			// With vr_toolButtons the other hand's buttons step through the tools.
			const std::string orStep =
			  (int)vr_toolButtons ? _Tr("Client", " (or step to it with {0})", cycle) : std::string();
			switch (st) {
				case training::Start:
					title = _Tr("Client", "Training {0}: the start", std::string(step));
					tip = vr ? _Tr("Client", "Turn your {0} wrist towards you: health, ammo and the "
					                         "map are on it. Walk with the {0} stick (click it in to "
					                         "sprint) and turn with the {1} one. {2} jumps; clicking "
					                         "the {1} stick in crouches. Walk ahead to the range.",
					               otherSide, gunSide, jumpBtn)
					         : _Tr("Client", "Walk ahead to the range. Health and ammo are at the "
					                         "bottom of the screen.");
					break;
				case training::Range:
					title = _Tr("Client", "Training {0}: the range", std::string(step));
					if (!vr) {
						tip = _Tr("Client", "Shoot the targets walking across the range (left "
						                    "click; right click aims). A shot lands the moment you "
						                    "fire, so aim right at them. They do not shoot back. "
						                    "Crouching halves the spread. {0} reloads. Walk on when "
						                    "you are ready.",
						          static_cast<std::string>(cg_keyReloadWeapon));
					} else {
						const std::string reload =
						  (int)vr_reloadGesture
						    ? _Tr("Client", "To reload, press {0} to drop the magazine, take a new "
						                    "one from your belt with your {1} hand and put it in "
						                    "under the gun.",
						          reloadBtn, otherSide)
						    : _Tr("Client", "{0} reloads: the laser stays amber until it is done, "
						                    "and the gun buzzes in your hand.",
						          reloadBtn);
						tip = _Tr("Client", "Your gun is over your {0} shoulder: reach up there and "
						                    "squeeze the grip{1}. Shoot the targets walking across "
						                    "the range: the laser's dot is where the shot lands, the "
						                    "moment you fire. Crouching halves the spread. {2} They "
						                    "do not shoot back. Walk on when you are ready.",
						          gunSide, orStep, reload);
					}
					break;
				case training::Dig:
					title = _Tr("Client", "Training {0}: dig", std::string(step));
					tip = vr ? _Tr("Client", "A wall. Take the spade from over your {0} shoulder "
					                         "(squeeze the grip there){1}. Pull the trigger or swing "
					                         "it to dig a block, or squeeze the grip to dig a tunnel "
					                         "your size. The laser's dot turns green when a block is "
					                         "in reach.",
					               otherSide, orStep)
					         : _Tr("Client", "A wall. Take the spade ({0}) and dig through it: right "
					                         "click digs a tunnel your size.",
					               spade);
					break;
				case training::Build:
					title = _Tr("Client", "Training {0}: build", std::string(step));
					tip = vr ? _Tr("Client", "Too high to jump. Take blocks from your {0} hip{1} and "
					                         "build steps up: point where a block should go and pull "
					                         "the trigger, or squeeze the grip and drag to lay a "
					                         "line. The wrist menu has a palette.{2}",
					               gunSide, orStep,
					               (int)vr_toolButtons
					                 ? std::string()
					                 : _Tr("Client", " {0} copies the colour of the block you point at.",
					                       colourBtn))
					         : _Tr("Client", "Too high to jump. Take blocks ({0}) and build steps up: "
					                         "left click places a block where you look.",
					               block);
					break;
				case training::Grenade:
					title = _Tr("Client", "Training {0}: blow it open", std::string(step));
					tip = vr ? _Tr("Client", "The tunnel is plugged. Take a grenade from your {0} "
					                         "hip{1}, hold the trigger to cook it and throw it at the "
					                         "plug, letting go as you throw. Stand back.",
					               otherSide, orStep)
					         : _Tr("Client", "The tunnel is plugged. Take a grenade ({0}), hold left "
					                         "click to cook it and let go to throw it at the plug. "
					                         "Stand back.",
					               nade);
					break;
				default:
					title = _Tr("Client", "Training {0}: done", std::string(step));
					tip = vr ? _Tr("Client", "You made it. {1} opens and closes the big map, and your "
					                         "{0} grip shows the scores. With a gun, hold your {0} "
					                         "trigger to brace it on your shoulder. Bring your {2} "
					                         "hand to your {0} wrist for the wrist menu: chat, quick "
					                         "messages, colours, the light, and Menu, to leave. Hold "
					                         "both sticks in to recentre.",
					               otherSide,
					               (int)vr_toolButtons
					                 ? _Tr("Client", "Your {0} trigger (without a gun)", otherSide)
					                 : mapBtn,
					               gunSide)
					         : _Tr("Client", "You made it. Your gun is {0}. Press Esc to leave when "
					                         "you are ready.",
					               gun);
					break;
			}
			tip += "\n\n" + TrainingChecklist(vr);
			return true;
		}

		std::vector<std::string> Client::WrapTipText(IFont& font, const std::string& text,
		                                             float width) {
			// Words to lines of at most `width`; a newline always breaks one,
			// and two make an empty line.
			std::vector<std::string> lines(1);
			size_t i = 0;
			while (i < text.size()) {
				if (text[i] == '\n') {
					lines.emplace_back();
					++i;
					continue;
				}
				size_t j = text.find_first_of(" \n", i);
				if (j == std::string::npos)
					j = text.size();
				const std::string word = text.substr(i, j - i);
				const std::string tryLine = lines.back().empty() ? word : lines.back() + " " + word;
				if (!lines.back().empty() && font.Measure(tryLine).x > width)
					lines.push_back(word);
				else
					lines.back() = tryLine;
				i = j < text.size() && text[j] == ' ' ? j + 1 : j;
			}
			return lines;
		}

		void Client::DrawTipBox(const std::string& title, const std::string& text, Vector4 accent,
		                        Vector2 at, float layoutW, float k) {
			IFont& font = fontManager->GetGuiFont();
			const float inner = layoutW - 24.0F;
			std::vector<std::string> lines = WrapTipText(font, text, inner);
			const float lh = font.Measure("Ag").y;
			const float pad = 12.0F * k;
			const float boxW = layoutW * k;
			const float boxH = (lh * (float)(lines.size() + 1) + 24.0F) * k;
			const AABB2 box(at.x - boxW * 0.5F, at.y, boxW, boxH);
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.75F));
			renderer->DrawImage(nullptr, box);
			renderer->SetColorAlphaPremultiplied(accent);
			renderer->DrawImage(nullptr, AABB2(box.GetMinX(), box.GetMinY(), 4.0F * k, boxH));
			float y = box.GetMinY() + pad;
			font.Draw(title, MakeVector2(box.GetMinX() + pad, y), k, accent);
			y += lh * k;
			for (const auto& l : lines) {
				font.Draw(l, MakeVector2(box.GetMinX() + pad, y), k, MakeVector4(1, 1, 1, 1));
				y += lh * k;
			}
		}

		void Client::DrawTrainingTip() {
			std::string title, tip;
			if (!TrainingTip(false, title, tip))
				return;
			const float w = std::min(560.0F, renderer->ScreenWidth() - 32.0F);
			DrawTipBox(title, tip, MakeVector4(1, 0.85F, 0.3F, 1),
			           MakeVector2(renderer->ScreenWidth() * 0.5F, 64.0F), w, 1.0F);
		}

		void Client::DrawInEachVREye(const std::function<void()>& draw, int firstCanvas) {
			const SceneDefinition head = lastSceneDef;
			if (head.numVREyes != 2)
				return;
			for (int e = 0; e < 2; e++) {
				if (!renderer->SetVRCanvas(firstCanvas + e))
					continue;
				SceneDefinition eye = head;
				const SceneDefinition::VREye& v = head.vrEyes[e];
				eye.viewOrigin = v.origin;
				eye.viewAxis[0] = v.axis[0];
				eye.viewAxis[1] = v.axis[1];
				eye.viewAxis[2] = v.axis[2];
				eye.fovX = v.fovX;
				eye.fovY = v.fovY;
				eye.asymmetric = v.asymmetric;
				eye.tangents = v.tangents;
				lastSceneDef = eye;
				UpdateMatrices(); // with the eye canvas's size
				vrEyeOverlayPass = true;
				draw();
				vrEyeOverlayPass = false;
				renderer->SetVRCanvas(0);
			}
			lastSceneDef = head;
			UpdateMatrices();
		}

		void Client::DrawVRReflexDot() {
			// Called per eye: the dot is where this eye's line along the barrel
			// crosses the sight's window, and is seen only through it, so it
			// stands at infinity on the barrel's line, as a reflex sight's does,
			// in both eyes alike. Its size is a fixed angle, 3 milliradians.
			// The setting's Cross (4) draws a thin cross there instead.
			const Vector3 eye = lastSceneDef.viewOrigin;
			const Vector3 dir = vrRig.reflexDir;
			const float depth = Vector3::Dot(vrRig.reflexCenter - eye, dir);
			if (depth < 0.02F)
				return;
			const Vector3 at = eye + dir * depth;
			if ((at - vrRig.reflexCenter).GetLength() > vrRig.reflexRadius)
				return;
			Vector3 side = Vector3::Cross(dir, MakeVector3(0.0F, 0.0F, 1.0F));
			if (side.GetLength() < 1.0E-3F)
				side = MakeVector3(1.0F, 0.0F, 0.0F);
			Vector2 centre, edge;
			if (!Project(at, centre) || !Project(at + side.Normalize() * (depth * 0.003F), edge))
				return;
			const float r = std::max(1.5F, (edge - centre).GetLength());
			if ((int)cg_reflexScope == 4) {
				// Two bars, 8 milliradians across, added to what is behind them
				const float len = r * (8.0F / 3.0F), thick = std::max(1.0F, r * 0.3F);
				renderer->SetColorAlphaPremultiplied(MakeVector4(1.0F, 0.1F, 0.06F, 0.0F));
				renderer->DrawImage(nullptr, AABB2(centre.x - len, centre.y - thick * 0.5F,
				                                   len * 2.0F, thick));
				renderer->DrawImage(nullptr, AABB2(centre.x - thick * 0.5F, centre.y - len, thick,
				                                   len * 2.0F));
				return;
			}
			Handle<IImage> ball = renderer->RegisterImage("Gfx/Ball.png");
			// A red glow round a bright core, added to what is behind it
			renderer->SetColorAlphaPremultiplied(MakeVector4(1.0F, 0.05F, 0.03F, 0.0F));
			renderer->DrawImage(*ball, AABB2(centre.x - r * 2.0F, centre.y - r * 2.0F, r * 4.0F,
			                                 r * 4.0F));
			renderer->SetColorAlphaPremultiplied(MakeVector4(1.0F, 0.6F, 0.5F, 0.0F));
			renderer->DrawImage(*ball, AABB2(centre.x - r, centre.y - r, r * 2.0F, r * 2.0F));
		}

		void Client::DrawVRScopeLens() {
			// Called per eye, with Project set to that eye: the lens lands where it
			// is on the gun in each eye, so it sits at its own depth.
			IImage* image = renderer->GetVRScopeImage();
			Vector2 centre;
			if (!Project(vrRig.scopeLensPos, centre))
				return;
			// Its size by the angle it fills, at the middle of the view's scale:
			// the rim projected near the edge of the eye's picture spread far
			// wider than the lens looked (with the cross as thick as a felt-tip
			// at world scale 1, testers' report).
			const float dist = (vrRig.scopeLensPos - lastSceneDef.viewOrigin).GetLength();
			const float pixelsPerTangent =
			  renderer->ScreenWidth() / std::max(0.01F, lastSceneDef.GetFovTangents().Width());
			const float r = pixelsPerTangent * vrRig.scopeLensRadius / std::max(0.01F, dist);
			if (r < 4.0F)
				return;

			// A black rim, then the picture, both clipped round.
			renderer->BeginClippingCircle(centre, r * 1.08F);
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer->DrawImage(nullptr, AABB2(centre.x - r * 1.1F, centre.y - r * 1.1F, r * 2.2F,
			                                   r * 2.2F));
			renderer->EndClippingCircle();
			// The lens through a wall: black, not the far side (ClientPlayer).
			if (vrRig.scopeBlocked || !image)
				return;

			renderer->BeginClippingCircle(centre, r);
			renderer->SetColorAlphaPremultiplied(MakeVector4(1, 1, 1, 1));
			// The picture is a render target, top row first, as the 2D layer's
			// images are on this renderer (the vr branch's had them bottom row
			// first, and drew it from flipped source rectangles: upside down here,
			// testers' report).
			//
			// A fisheye: the picture is a plain perspective view out to the rim's
			// half-angle E, and a point at angle alpha off the axis is drawn at
			// lens radius rho with alpha = C rho + (E - C) rho^3, C the centre's
			// half-angle. The centre is then magnified as a monitor's scope is
			// (see ClientPlayer) and the rim squeezed. Drawn as a grid of cells
			// over the picture, each to where its corners land; the 2D layer only
			// maps a rectangle to a parallelogram, so the cells are small.
			const float w = image->GetWidth(), h = image->GetHeight();
			const float C = vrRig.scopeCentreHalf, E = vrRig.scopeEdgeHalf;
			const float tanE = std::tan(E);
			auto warp = [&](float nx, float ny) -> Vector2 {
				const float u = std::sqrt(nx * nx + ny * ny);
				if (u < 1.0E-5F)
					return centre;
				const float alpha = std::atan(u * tanE);
				// Newton on C rho + (E - C) rho^3 = alpha, monotonic in rho.
				float rho = alpha / std::max(C, 1.0E-4F);
				for (int k = 0; k < 6; k++) {
					const float g = C * rho + (E - C) * rho * rho * rho - alpha;
					const float dg = C + 3.0F * (E - C) * rho * rho;
					rho -= g / std::max(dg, 1.0E-5F);
				}
				return centre + MakeVector2(nx, ny) * (rho * r / u);
			};
			// The plain picture underneath: where two cells do not quite meet,
			// it shows instead of the black rim (a grid of black lines in the
			// headset, the first time).
			renderer->DrawImage(image, AABB2(centre.x - r, centre.y - r, r * 2.0F, r * 2.0F),
			                    AABB2(0.0F, 0.0F, w, h));
			const int N = 20;
			for (int j = 0; j < N; j++) {
				const float ny0 = -1.0F + 2.0F * j / N, ny1 = -1.0F + 2.0F * (j + 1) / N;
				for (int i = 0; i < N; i++) {
					const float nx0 = -1.0F + 2.0F * i / N, nx1 = -1.0F + 2.0F * (i + 1) / N;
					const Vector2 tl = warp(nx0, ny0), tr = warp(nx1, ny0), bl = warp(nx0, ny1);
					// Cells wholly outside the lens are clipped anyway.
					const Vector2 mid = (tr + bl) * 0.5F;
					if ((mid - centre).GetLength() > r * 1.2F)
						continue;
					// Each cell a pixel bigger all round, and its source with it,
					// so neighbours overlap and still show the same picture: a cell
					// is a parallelogram on its fourth corner, which misses the
					// next cell's by a hair.
					const Vector2 e1 = tr - tl, e2 = bl - tl;
					const float l1 = e1.GetLength(), l2 = e2.GetLength();
					if (l1 < 1.0E-3F || l2 < 1.0E-3F)
						continue;
					const float a = 1.0F / l1, b = 1.0F / l2;
					const Vector2 o = tl - e1 * a - e2 * b;
					const float sw = w / N, sh = h / N;
					renderer->DrawImage(
					  image, o, o + e1 * (1.0F + 2.0F * a), o + e2 * (1.0F + 2.0F * b),
					  AABB2((nx0 + 1.0F) * 0.5F * w - sw * a, (ny0 + 1.0F) * 0.5F * h - sh * b,
					        sw * (1.0F + 2.0F * a), sh * (1.0F + 2.0F * b)));
				}
			}
			// The reticle: a thin cross along the gun's own right and up, so it
			// cants with the gun (the picture stays level with the world, as a real
			// scope's does). Each arm is the gun's axis projected into this eye.
			const float t = std::max(1.0F, r * 0.015F);
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.9F));
			auto arm = [&](const Vector3& axis) {
				Vector2 end;
				if (!Project(vrRig.scopeLensPos + axis * vrRig.scopeLensRadius, end))
					return;
				Vector2 u = end - centre;
				if (u.GetLength() < 1.0E-3F)
					return;
				u = u.Normalize();
				const Vector2 n = MakeVector2(-u.y, u.x) * (t * 0.5F);
				const Vector2 a = centre - u * (r * 1.1F), b = centre + u * (r * 1.1F);
				renderer->DrawImage(nullptr, a - n, b - n, a + n, AABB2(0.0F, 0.0F, 1.0F, 1.0F));
			};
			arm(vrRig.scopeGunRight);
			arm(vrRig.scopeGunUp);
			renderer->EndClippingCircle();
		}

		void Client::DrawVRFrameRate() {
			// Below the headset's rate, the runtime makes up the missing frames by
			// warping old ones, and the world wobbles and loses its solidity.
			IFont& font = fontManager->GetMediumFont(); // nothing small on the wrist
			std::string text = ToString(static_cast<int>(std::lround(vrRig.fps))) + " fps";
			float gpuMs = 0.0F, scale = 1.0F;
			if (renderer->GetVRFrameStats(gpuMs, scale)) {
				char buf[64];
				std::snprintf(buf, sizeof(buf), "  GPU %.1f ms  eyes %d%%", gpuMs,
				              static_cast<int>(scale * 100.0F + 0.5F));
				text += buf;
			}
			Vector2 size = font.Measure(text);
			Vector2 pos = MakeVector2(std::floor((renderer->ScreenWidth() - size.x) * 0.5F),
			                          renderer->ScreenHeight() - size.y - 8.0F);
			font.DrawShadow(text, pos, 1.0F, MakeVector4(1, 1, 1, 0.8F), MakeVector4(0, 0, 0, 0.6F));
		}

		void Client::DrawVRWristMenu() {
			IFont& big = fontManager->GetLargeFont();
			IFont& mid = fontManager->GetMediumFont();
			IFont& small = fontManager->GetGuiFont();
			IntVector3 current = MakeIntVector3(-1, -1, -1);
			if (auto p = world->GetLocalPlayer())
				current = p->GetBlockColor();

			auto frame = [&](const AABB2& r, float b, const Vector4& c) {
				renderer->SetColorAlphaPremultiplied(c);
				renderer->DrawImage(nullptr, AABB2(r.GetMinX(), r.GetMinY(), r.GetWidth(), b));
				renderer->DrawImage(nullptr,
				                    AABB2(r.GetMinX(), r.GetMaxY() - b, r.GetWidth(), b));
				renderer->DrawImage(nullptr, AABB2(r.GetMinX(), r.GetMinY() + b, b,
				                                   r.GetHeight() - b * 2.0F));
				renderer->DrawImage(nullptr, AABB2(r.GetMaxX() - b, r.GetMinY() + b, b,
				                                   r.GetHeight() - b * 2.0F));
			};
			auto centred = [&](IFont& f, const std::string& text, const AABB2& r) {
				Vector2 size = f.Measure(text);
				Vector2 at = MakeVector2(r.GetMinX() + (r.GetWidth() - size.x) * 0.5F,
				                         r.GetMinY() + (r.GetHeight() - size.y) * 0.5F);
				f.Draw(text, MakeVector2(std::floor(at.x), std::floor(at.y)), 1.0F,
				       MakeVector4(1, 1, 1, 1));
			};

			const std::vector<VRWristButton> buttons = VRWristMenuLayout();
			for (std::size_t i = 0; i < buttons.size(); i++) {
				const VRWristButton& b = buttons[i];
				const bool hot = static_cast<int>(i) == vrRig.wristHover;
				if (b.kind == VRWristButton::Colour) {
					const Vector4 c = MakeVector4(b.color.x / 255.0F, b.color.y / 255.0F,
					                              b.color.z / 255.0F, 1.0F);
					AABB2 r = b.rect;
					r = AABB2(r.GetMinX() + 2.0F, r.GetMinY() + 2.0F, r.GetWidth() - 4.0F,
					          r.GetHeight() - 4.0F);
					renderer->SetColorAlphaPremultiplied(c);
					renderer->DrawImage(nullptr, r);
					if (b.color == current)
						frame(r, 5.0F, MakeVector4(1, 1, 1, 1));
					else if (hot)
						frame(r, 3.0F, MakeVector4(0.8F, 0.8F, 0.8F, 1));
					continue;
				}
				const float a = hot ? 0.55F : 0.25F;
				renderer->SetColorAlphaPremultiplied(MakeVector4(a, a, a, 0.9F));
				renderer->DrawImage(nullptr, b.rect);
				switch (b.kind) {
					case VRWristButton::Chat: centred(big, _Tr("Client", "Chat"), b.rect); break;
					case VRWristButton::TeamChat:
						centred(big, _Tr("Client", "Team chat"), b.rect);
						break;
					case VRWristButton::Phrase:
						centred(mid, _Tr("Client", VRQuickPhrase(b.index)), b.rect);
						break;
					case VRWristButton::Menu: centred(mid, _Tr("Client", "Menu"), b.rect); break;
					case VRWristButton::Light: {
						auto local = world->GetLocalPlayer();
						centred(mid,
						        local && local->IsFlashlightOn() ? _Tr("Client", "Light: on")
						                                         : _Tr("Client", "Light: off"),
						        b.rect);
					} break;
					case VRWristButton::Team:
						centred(mid, _Tr("Client", "Team / weapon"), b.rect);
						break;
					case VRWristButton::QuickTarget:
						centred(mid,
						        vrRig.wristToTeam ? _Tr("Client", "Quick chat to: team")
						                          : _Tr("Client", "Quick chat to: everyone"),
						        b.rect);
						break;
					default: break;
				}
			}

			// The colour held, under the palette.
			const AABB2 swatch(500.0F, 470.0F, 120.0F, 110.0F);
			if (current.x >= 0) {
				renderer->SetColorAlphaPremultiplied(MakeVector4(
				  current.x / 255.0F, current.y / 255.0F, current.z / 255.0F, 1.0F));
				renderer->DrawImage(nullptr, swatch);
				frame(swatch, 3.0F, MakeVector4(1, 1, 1, 1));
			}
			small.Draw(_Tr("Client", "Block colour"), MakeVector2(636.0F, 480.0F), 1.0F,
			           MakeVector4(1, 1, 1, 0.9F));
			small.Draw(_Tr("Client", "Touch or point and pull"), MakeVector2(636.0F, 510.0F), 1.0F,
			           MakeVector4(1, 1, 1, 0.6F));

			// Where the tool hand points.
			if (vrRig.wristPointing) {
				const Vector2 c = vrRig.wristCursor;
				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.8F));
				renderer->DrawImage(nullptr, AABB2(c.x - 9.0F, c.y - 9.0F, 18.0F, 18.0F));
				renderer->SetColorAlphaPremultiplied(MakeVector4(1.0F, 0.15F, 0.1F, 1.0F));
				renderer->DrawImage(nullptr, AABB2(c.x - 6.0F, c.y - 6.0F, 12.0F, 12.0F));
			}
		}

		void Client::DrawVRWatch(Player& p, int clipNum, int stockNum, int clipSize,
		                         bool isToolWeapon, bool isReloading) {
			// The wrist as a watch (testers asked): health and ammo large along
			// its bottom, readable at a glance, where the monitor's corner numbers
			// were small on a panel a hand's width across. The map, feeds and chat
			// keep their places above.
			const float sw = renderer->ScreenWidth(), sh = renderer->ScreenHeight();
			IFont& font = fontManager->GetSquareDesignFont();
			// The captions over the numbers: they stayed small when the numbers
			// grew (testers)
			IFont& label = fontManager->GetMediumFont();
			const Vector4 color = GetHUDColor(p);
			const Vector4 red = MakeVector4(1.0F, 0.1F, 0.05F, 1.0F);
			const Vector4 shadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.6F);
			const Vector4 white = MakeVector4(1.0F, 1.0F, 1.0F, 0.85F);
			const float scale = 3.0F, pad = 28.0F;
			auto big = [&](const std::string& value, float x, bool alignRight, Vector4 col,
			               const std::string& caption) {
				const Vector2 size = font.Measure(value) * scale;
				const Vector2 pos = MakeVector2(alignRight ? x - size.x : x, sh - pad - size.y);
				font.Draw(value, pos + MakeVector2(3.0F, 3.0F), scale, shadow);
				font.Draw(value, pos, scale, col);
				const Vector2 ls = label.Measure(caption);
				label.DrawShadow(caption,
				                 MakeVector2(alignRight ? x - ls.x : x, pos.y - ls.y - 2.0F), 1.0F,
				                 white, shadow);
			};

			const float hurt = std::max(0.0F, 1.0F - (time - lastHurtTime) / 0.25F);
			const float hpFrac = Clamp(static_cast<float>(lastHealth) / 100.0F, 0.0F, 1.0F);
			const Vector4 hpCol = color + (red - color) * (1.0F - hpFrac);
			big(ToString(lastHealth), pad, false, hpCol + (MakeVector4(1, 1, 1, 1) - hpCol) * hurt,
			    _Tr("Client", "Health"));

			std::string value, caption;
			float per = 1.0F;
			if (isToolWeapon) {
				value = ToString(clipNum) + "-" + ToString(stockNum);
				caption = isReloading ? _Tr("Client", "Reloading") : _Tr("Client", "Ammo");
				per = Clamp(static_cast<float>(clipNum) / std::max(1.0F, clipSize / 3.0F), 0.0F,
				            1.0F);
			} else if (p.IsToolGrenade()) {
				value = ToString(p.GetNumGrenades());
				caption = _Tr("Client", "Grenades");
			} else {
				value = ToString(p.GetNumBlocks());
				caption = _Tr("Client", "Blocks");
			}
			big(value, sw - pad, true, color + (red - color) * (1.0F - per), caption);
		}

		void Client::DrawVRWristBackdrop() {
			// A dark glass so the HUD reads against whatever is behind the wrist.
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.0F, 0.0F, 0.0F, 0.45F));
			renderer->DrawImage(nullptr, AABB2(0.0F, 0.0F, sw, sh));
			const float b = 3.0F;
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));
			renderer->DrawImage(nullptr, AABB2(0.0F, 0.0F, sw, b));
			renderer->DrawImage(nullptr, AABB2(0.0F, sh - b, sw, b));
			renderer->DrawImage(nullptr, AABB2(0.0F, b, b, sh - b * 2.0F));
			renderer->DrawImage(nullptr, AABB2(sw - b, b, b, sh - b * 2.0F));
		}

		void Client::DrawReloadIndicator(const Vector4& col) {
			SPADES_MARK_FUNCTION();

			if (col.w <= 0.0F)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			const Vector2 center = MakeVector2(sw * 0.5F, sh * 0.5F);

			const float radius = 16.0F;
			const float thickness = 2.0F;
			const float spinSpeed = kTwoPi * 0.75F;
			const float sweepAngle = DEG2RAD(225.0F);
			const float startAngle = fmodf(time * spinSpeed, kTwoPi);
			const float fadeAngle = DEG2RAD(45.0F);

			// draw background
			renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.4F * col.w));
			renderer->DrawOutlinedCircle(center, radius, thickness * 2.0F);

			// draw outlined arc
			const float inner = radius - (thickness * 0.5F);
			const float outer = radius + (thickness * 0.5F);
			const float sweepAbs = fabsf(sweepAngle);
			const float fadeLength = std::min(sweepAbs, fadeAngle);

			Vector2 prevInner = center + MakeVector2(cosf(startAngle), sinf(startAngle)) * inner;
			Vector2 prevOuter = center + MakeVector2(cosf(startAngle), sinf(startAngle)) * outer;

			const int segments = Clamp((int)radius, 16, 64);
			for (int i = 1; i <= segments; i++) {
				float t = (float)i / (float)segments;
				float angle = startAngle + sweepAngle * t;

				Vector2 curInner = center + MakeVector2(cosf(angle), sinf(angle)) * inner;
				Vector2 curOuter = center + MakeVector2(cosf(angle), sinf(angle)) * outer;

				float distFromStart = sweepAbs * t;
				float segAlpha = (fadeLength > 0.0F)
					? Clamp(distFromStart / fadeLength, 0.0F, 1.0F) : 1.0F;
				segAlpha *= col.w;

				renderer->SetColorAlphaPremultiplied(MakeVector4(col.x * segAlpha, col.y * segAlpha,
																col.z * segAlpha, segAlpha));
				renderer->DrawFilledTriangle(prevInner, prevOuter, curInner);
				renderer->DrawFilledTriangle(prevOuter, curOuter, curInner);

				prevInner = curInner;
				prevOuter = curOuter;
			}
		}

		void Client::Draw2DWithWorld() {
			SPADES_MARK_FUNCTION();

			bool isDemoMode = IsDemoMode();
			// The debug screen replaces the HUD while it is open.
			bool shouldDrawHUD = hudVisible && !cg_hideHud && !debugScreenVisible;
			bool isChatPanelVisible = scriptedUI->IsChatPanelOpen();

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			// fade the map when the world starts (draw)
			const float fadeOutStart = 1.0F;
			const float fadeDuration = 2.5F;
			float timeSinceWorldStart = time - worldSetTime;
			float fade = timeSinceWorldStart - fadeOutStart;
			fade = 1.0F - (fade / fadeDuration);
			fade = Clamp(fade, 0.0F, 1.0F);

			// (Not in a headset, where the 2D layer is a panel in the air and this
			// would be a black board hanging in front of you.)
			if (fade > 0.0F && !IsVRFrameActive()) {
				renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, fade));
				renderer->DrawImage(nullptr, AABB2(0, 0, sw, sh));
			}

			for (const auto& ent : localEntities)
				ent->Render2D();

			stmp::optional<Player&> maybePlayer = world->GetLocalPlayer();

			if (maybePlayer) { // joined local player
				Player& player = maybePlayer.value();

				bool localPlayerIsSpectator = player.IsSpectator() || staffSpectating;
				const bool vrFrame = IsVRFrameActive();

				// In a headset the HUD goes on the wrist panel, and is drawn only on
				// the frames the wrist is being looked at. The views you call up on
				// purpose (big map, scoreboard) stay on the panel in front, as does
				// the chat while you type into it.
				const bool onWrist = vrFrame && (int)vr_wristHud != 0 && shouldDrawHUD;
				const bool chatInFront =
				  onWrist && (AcceptsTextInput() || isChatPanelVisible);

				// Names, boxes, chevrons, pings and damage numbers: what is pinned to
				// places in the world. In a headset they are drawn into each eye's
				// picture with that eye's projection, so they appear at the depth of
				// what they label. On the flat panel a name over someone 30 blocks
				// away was 1.5m in front of you, and the two eyes disagreed.
				auto drawPinned = [&](bool inEye) {
					if (cg_playerNamesDead && !debugScreenVisible)
						DrawDeadPlayers();

					if (!localPlayerIsSpectator) {
						// (Not in a headset: a full-screen effect on a panel in the
						// air is a tinted rectangle.)
						if (cg_hurtScreenEffects && !vrFrame) {
							DrawHurtSprites();
							DrawScreenEffect(true);
						}

						if (cg_healScreenEffects && !vrFrame)
							DrawScreenEffect(false);

						if (cg_playerNames && !debugScreenVisible)
							DrawHottrackedPlayerName();

						if (cg_damageIndicators && !debugScreenVisible)
							DrawDamageIndicators();
					} else if (spectatorPlayerNames && !debugScreenVisible) { // only if we are spectating
						DrawPubOVL();
					}

					// Teamplay marks and pings come from the server and are drawn
					// whether or not the local player is spectating; the team overlay
					// is the local player's own view of their team and is not.
					if (debugScreenVisible)
						return;
					DrawEspMarks();
					if (!onWrist && !inEye)
						DrawLocalPlayerMarkedIndicator();
					DrawTeamplayPings();
					if (!localPlayerIsSpectator)
						DrawTeamOverlay();
				};
				if (vrFrame) {
					DrawInEachVREye([&]() {
						drawPinned(true);
						if (vrRig.scopeActive)
							DrawVRScopeLens();
						if (vrRig.reflexActive)
							DrawVRReflexDot();
					});
					if (!onWrist)
						DrawLocalPlayerMarkedIndicator();
				} else {
					drawPinned(false);
				}

				if (shouldDrawHUD) {
					// draw firstperson camera player HUD (not in a headset: its
					// crosshair and scope belong to a screen, and the gun in your
					// hand is the sight)
					if (IsFirstPerson(GetCameraMode()) && !vrFrame)
						DrawFirstPersonHUD();

					bool largeMap = largeMapView->IsZoomed();
					// In a headset the big map is held in the off hand, on the wrist
					// panel's image (the VR system put the panel there this frame).
					const bool heldMap = onWrist && largeMap && (int)vr_heldMap;

					if (!onWrist || renderer->SetVRCanvas(1)) {
						vrWristPass = onWrist;
						if (onWrist)
							DrawVRWristBackdrop();
						// Chat, kill feed and centre messages half as large again on
						// the wrist: at the gui font's 16 pixels they were hard to
						// make out there (testers).
						if (onWrist) {
							chatWindow->SetTextScale(&fontManager->GetMediumFont(), 1.5F);
							// Over the watch face's numbers (DrawVRWatch), which the
							// larger chat ran into (testers)
							chatWindow->SetBottomMargin(180.0F);
							killfeedWindow->SetTextScale(&fontManager->GetMediumFont(), 1.5F);
							centerMessageView->SetFont(&fontManager->GetLargeFont());
						}

						// The other hand at the wrist: the wrist menu instead. The big
						// map held in the hand (vr_heldMap): only the map.
						const bool wristMenu = onWrist && vrRig.wristMenu;
						if (heldMap) {
							largeMapView->Draw();
						} else if (wristMenu) {
							DrawVRWristMenu();
						} else {
							tcView->Draw();

							if (cg_hudPlayerCount)
								DrawAlivePlayersCount();

							// draw map (not on the wrist: the big map is a button away,
							// and the watch is for health and ammo, testers)
							if (!largeMap && !onWrist)
								mapView->Draw();

							if (!localPlayerIsSpectator) { // player is not spectator
								if (cg_playerStats)
									DrawPlayerStats();
								if (!player.IsToolBlock() && !debugHitTestZoom && !vrFrame)
									DrawHitTestDebugger();

								if (player.IsAlive()) {
									DrawJoinedAlivePlayerHUD();
								} else {
									DrawDeadPlayerHUD();
									DrawSpectateHUD();
								}
							} else {
								DrawSpectateHUD();
							}

							// the chat panel shows the history by itself while it is open
							if (!chatInFront && !isChatPanelVisible)
								chatWindow->Draw();
							// On the wrist only if asked for (testers found it
							// crowded there); see vr_killfeed
							if (!onWrist || (int)vr_killfeed == 2)
								killfeedWindow->Draw();

							// above the chat
							if (netgraphVisible && !isDemoMode)
								DrawNetGraph();

						}

						if (onWrist) {
							if (!wristMenu && !heldMap) {
								DrawLocalPlayerMarkedIndicator();
								DrawAlert();
								centerMessageView->Draw();
								if ((int)vr_showFps)
									DrawVRFrameRate();
							}
							renderer->SetVRCanvas(0);
							chatWindow->SetTextScale(nullptr, 1.0F);
							chatWindow->SetBottomMargin(64.0F);
							killfeedWindow->SetTextScale(nullptr, 1.0F);
							centerMessageView->SetFont(nullptr);
						}
						vrWristPass = false;
					}

					if (chatInFront && !isChatPanelVisible)
						chatWindow->Draw();
					// The kill feed in front, where it is on a monitor
					if (onWrist && (int)vr_killfeed == 1)
						killfeedWindow->Draw();

					// Where the hits came from, around where you look.
					if (onWrist && !localPlayerIsSpectator && player.IsAlive())
						hurtRingView->Draw();

					if (debugHitTestZoom && !vrFrame)
						DrawHitTestDebugger();

					// large map view should come in front
					if (largeMap && !heldMap)
						largeMapView->Draw();
				}

				if (pieMenuView && pieMenuView->IsOpen())
					pieMenuView->Draw();

				if (!onWrist) {
					DrawAlert();
					centerMessageView->Draw();
				}
				if (scoreboardVisible)
					scoreboard->Draw();

				// --- end "player is there" render
			} else {
				// world exists, but no local player: not joined (or demo mode)

				if (isDemoMode) {
					// Draw spectator HUD elements in demo mode.
					// Boxes always render (staff use); names follow the toggle.
					if (!debugScreenVisible)
						DrawPubOVL();

					if (shouldDrawHUD) {
						// draw firstperson camera player HUD
						if (IsFirstPerson(GetCameraMode()))
							DrawFirstPersonHUD();

						tcView->Draw();

						if (cg_hudPlayerCount)
							DrawAlivePlayersCount();

						// draw map
						bool largeMap = largeMapView->IsZoomed();
						if (!largeMap)
							mapView->Draw();

						DrawSpectateHUD();

						if (!isChatPanelVisible)
							chatWindow->Draw();
						killfeedWindow->Draw();

						// large map view should come in front
						if (largeMap)
							largeMapView->Draw();
					}
				}

				// In demo mode, only show scoreboard when toggled
				if (!isDemoMode || scoreboardVisible)
					scoreboard->Draw();

				centerMessageView->Draw();
				DrawAlert();
			}

			// The timeline can only be clicked while it is on screen; the playback
			// HUD sets this flag again when it draws the bar.
			demoTimelineVisible = false;

			if (shouldDrawHUD) {
				// draw demo hud
				if (isDemoMode)
					DrawDemoPlaybackHUD();
				if (net && net->IsDemoRecording())
					DrawRecordingIndicator();

				if (cg_stats)
					DrawStats();
			}

			// On the training course, the tip for where you are (in a
			// headset it is drawn in each eye instead, DrawVRTutorial).
			if (!IsVRFrameActive() && !IsLimboViewActive())
				DrawTrainingTip();

			// draw limbo view (above everything)
			if (IsLimboViewActive() && !scriptedUI->NeedsInput())
				limboMenu->Draw();

			// Over everything in each eye, joined or not: the tutorial, then (on
			// the headset-only canvases, not in the window's preview) the comfort
			// vignette and the comfort camera's blink. A second pass on the eye
			// canvas adds to the first.
			if (IsVRFrameActive()) {
				DrawInEachVREye([&]() { DrawVRTutorial(); });
				// The headset's alone: the window's preview is taken before it
				DrawInEachVREye(
				  [&]() {
					  DrawVRVignette();
					  if (vrRig.fade > 0.0F) {
						  renderer->SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, vrRig.fade));
						  renderer->DrawImage(nullptr, AABB2(0.0F, 0.0F, renderer->ScreenWidth(),
						                                     renderer->ScreenHeight()));
					  }
				  },
				  4);
			}
		}

		void Client::Draw2DWithoutWorld() {
			SPADES_MARK_FUNCTION();

			DrawSplash();

			// no world; loading?
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			const float prgBarW = 440.0F;
			const float prgBarH = 8.0F;
			const float prgBarX = (sw - prgBarW) * 0.5F;
			const float prgBarY = sh - (prgBarH + 40.0F);
			const float prgBarX1 = prgBarX + prgBarW;
			const float prgBarY1 = prgBarY + prgBarH;

			// draw background track
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.2F, 0.2F, 0.2F, 1.0F));
			renderer->DrawFilledRect(prgBarX, prgBarY, prgBarX1, prgBarY1);

			NetClientStatus status = activeNet->GetStatus();
			if (status == NetClientStatusReceivingMap) {
				const float progress = Clamp(mapReceivingProgressSmoothed, 0.0F, 1.0F);
				// draw progress bar: horizontal gradient from dark to brighter blue
				const float fillX1 = prgBarX + prgBarW * progress;
				renderer->DrawFilledRectFade(prgBarX, prgBarY, fillX1, prgBarY1,
						MakeVector4(0.0F, 0.25F, 0.5F, 1.0F),
						MakeVector4(0.0F, 0.5F, 1.0F, 1.0F), true);
			} else {
				// draw indeterminate progress bar: a soft glow sweeping across the track
				const float glowRadius = 200.0F;
				const float peakAlpha = 0.55F;
				const float progressPosition = fmodf(timeSinceInit * 0.7F, 1.0F);
				const float centerX = prgBarX + progressPosition * (prgBarW + glowRadius * 2.0F) - glowRadius;

				// glow alpha at a given x (linear falloff from the center)
				auto glowColorAt = [&](float x) {
					float a = 1.0F - fabsf(x - centerX) / glowRadius;
					a = std::max(a, 0.0F) * peakAlpha;
					return MakeVector4(0.5F, 0.5F, 0.5F, 1.0F) * a;
				};

				// draw one linear segment of the glow, clipped to the track
				auto drawGlowSegment = [&](float xa, float xb) {
					xa = std::max(xa, prgBarX);
					xb = std::min(xb, prgBarX1);
					if (xb <= xa)
						return;
					renderer->DrawFilledRectFade(xa, prgBarY, xb, prgBarY1,
						glowColorAt(xa), glowColorAt(xb), true);
				};

				drawGlowSegment(centerX - glowRadius, centerX); // fade in
				drawGlowSegment(centerX, centerX + glowRadius); // fade out
			}

			// draw outline
			renderer->SetColorAlphaPremultiplied(MakeVector4(0.1F, 0.1F, 0.1F, 0.1F));
			renderer->DrawOutlinedRect(prgBarX - 1, prgBarY - 1, prgBarX1 + 1, prgBarY1 + 1, 1);

			// draw net status
			Vector4 dim = MakeVector4(0.5F, 0.5F, 0.5F, 1.0F);
			IFont& font = fontManager->GetGuiFont();
			auto statusStr = activeNet->GetStatusString();
			Vector2 size = font.Measure(statusStr);
			Vector2 pos = MakeVector2((sw - size.x) * 0.5F, (prgBarY - 10.0F) - size.y);
			font.Draw(statusStr, pos, 1.0F, dim);

			// draw the server name and the cancel hint in the top-left corner
			const float margin = 8.0F;
			float y = margin;

			// server name (empty in demo mode)
			if (!serverName.empty()) {
				IFont& headingFont = fontManager->GetHeadingFont();
				std::string serverStr = _Tr("Client", "Connecting to {0}", serverName);
				headingFont.Draw(serverStr, MakeVector2(margin, y), 1.0F, MakeVector4(1, 1, 1, 1));
				y += headingFont.Measure(serverStr).y + 4.0F;
			}

			// cancel hint
			std::string hintStr = _Tr("Client", "[Esc] Cancel");
			font.Draw(hintStr, MakeVector2(margin, y), 1.0F, dim);
		}

		void Client::DrawStats() {
			SPADES_MARK_FUNCTION();

			bool isDemoMode = IsDemoMode();

			// only draw stats when scoreboard is visible
			const int statsMode = cg_stats;
			if (statsMode >= 3 && !scoreboardVisible)
				return;

			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();

			char buf[64];
			std::string str;

			{
				auto fps = fpsCounter.GetFps();
				if (fps <= 0) {
					str += "FPS: NA";
				} else {
					snprintf(buf, sizeof(buf), "%.0fFPS", fps);
					str += buf;
				}
			}
			{
				// Display world updates per second
				auto ups = upsCounter.GetFps();
				if (ups <= 0) {
					str += ", UPS: NA";
				} else {
					snprintf(buf, sizeof(buf), ", %.0fUPS", ups);
					str += buf;
				}
			}

			if (!isDemoMode && !netgraphVisible) {
				auto ping = activeNet->GetPing();
				snprintf(buf, sizeof(buf), ", ping: %dms", ping);
				str += buf;

				auto upbps = activeNet->GetUplinkBps() / 1000;
				auto downbps = activeNet->GetDownlinkBps() / 1000;
				snprintf(buf, sizeof(buf), ", up/down: %.02f/%.02fkbps", upbps, downbps);
				str += buf;

				auto loss = activeNet->GetPacketLoss() * 100.0F;
				snprintf(buf, sizeof(buf), ", loss: %.0f%%", loss);
				str += buf;

				auto throttle = activeNet->GetPacketThrottle();
				auto choke = (1.0F - throttle) * 100.0F;
				snprintf(buf, sizeof(buf), ", choke: %.0f%%", choke);
				str += buf;
			}

			// add margin
			const float margin = 2.0F;
			IFont& font = cg_statsSmallFont
				? fontManager->GetSmallFont()
				: fontManager->GetGuiFont();
			Vector2 size = font.Measure(str) + (margin * 2.0F);
			Vector2 pos = MakeVector2(sw, sh) - size;
			pos *= MakeVector2(0.5F, (statsMode >= 2 || isDemoMode) ? 0.0F : 1.0F);

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 outline = MakeVector4(0, 0, 0, 0.8F);

			// draw text
			pos += MakeVector2(margin, margin);
			font.DrawOutline(str, pos, 1.0F, color, outline);
		}

		void Client::DrawNetGraph() {
			SPADES_MARK_FUNCTION();

			IFont& font = fontManager->GetSmallFont();

			// const float sw = renderer->ScreenWidth();
			const float sh = renderer->ScreenHeight();

			const float margin = 4.0F;
			const float graphW = 128.0F;
			const float graphH = 64.0F;
			const float graphX = margin;
			const float graphY = (sh * 0.5F) - margin - graphH * 0.5F;

			// label column to the right of the graph
			const float labelColW = 64.0F;
			const float panelX = graphX;
			const float panelW = graphW + margin + labelColW;
			const float panelH = graphH;
			const float panelY = graphY;

			// colors
			const Vector4 white = MakeVector4(1, 1, 1, 1);
			const Vector4 colDown = MakeVector4(0.2F, 1, 0.35F, 1);
			const Vector4 colUp = MakeVector4(0.35F, 0.55F, 1, 1);
			const Vector4 colPing = MakeVector4(1, 0.25F, 0.25F, 1);
			const Vector4 outline = MakeVector4(0, 0, 0, 0.7F);
			const Vector4 colBackground = MakeVector4(0, 0, 0, 0.55F);
			const Vector4 colBorder = MakeVector4(0.6F, 0.6F, 0.6F, 0.6F);

			// draw background
			const float bgTop = panelY - margin;
			const float bgBottom = panelY + panelH + margin;
			const float solidEnd = graphX + graphW + margin;
			const float fadeEnd = panelX + panelW + margin;
			const Vector4 bgTransparent = MakeVector4(0, 0, 0, 0);
			renderer->DrawFilledRectFadeSolid(0.0F, bgTop, fadeEnd, bgBottom,
											  solidEnd, fadeEnd,
											  colBackground, bgTransparent, true);

			// compute max values for scaling
			float maxBw = 8.0F; // kbps floor
			float maxPing = 300.0F; // ms floor
			for (int i = 0; i < kNetGraphSamples; i++) {
				const auto& s = netGraph.At(i);
				maxBw = std::max({maxBw, s.downKbps, s.upKbps});
				maxPing = std::max(maxPing, s.pingMs);
			}

			// round up to a clean headroom ceiling
			maxBw *= 2.0F;
			maxPing *= 2.0F;

			// draw graph lines (one pixel-wide column per sample)
			const float colW = graphW / static_cast<float>(kNetGraphSamples);
			for (int i = 0; i < kNetGraphSamples; i++) {
				const auto& s = netGraph.At(i);
				const float x = graphX + float(i) * colW;

				float downH = (s.downKbps / maxBw) * graphH;
				float upH = (s.upKbps / maxBw) * graphH;
				float pingH = (s.pingMs / maxPing) * graphH;
				float baseH = graphY + graphH;

				{
					float top = std::max(baseH - downH, graphY);
					renderer->SetColorAlphaPremultiplied(colDown);
					renderer->DrawFilledRect(x, top, x + colW, baseH);
					baseH = top;
				}
				{
					float top = std::max(baseH - upH, graphY);
					renderer->SetColorAlphaPremultiplied(colUp);
					renderer->DrawFilledRect(x, top, x + colW, baseH);
					baseH = top;
				}
				{
					float top = std::max(baseH - pingH, graphY);
					renderer->SetColorAlphaPremultiplied(colPing);
					renderer->DrawFilledRect(x, top, x + colW, baseH);
				}
			}

			// graph border
			renderer->SetColorAlphaPremultiplied(colBorder);
			renderer->DrawOutlinedRect(graphX - 1, graphY - 1, graphX + graphW + 1, graphY + graphH + 1);

			// draw labels (right-aligned text in the label column)
			const float downKbps = std::min(static_cast<float>(activeNet->GetDownlinkBps() / 1000.0), 999.0F);
			const float upKbps = std::min(static_cast<float>(activeNet->GetUplinkBps() / 1000.0), 999.0F);
			const float pingMs = std::min(static_cast<float>(activeNet->GetPing()), 999.0F);
			const float lossPer = activeNet->GetPacketLoss() * 100.0F;

			char buf[64];
			const float lh = graphH / 4.0F;
			Vector2 labelPos = MakeVector2(graphX + graphW + margin, panelY);
			auto drawLabel = [&](const std::string& label, const std::string& value, const Vector4& col) {
				float ty = labelPos.y + (lh - font.Measure(label).y) * 0.5F;
				font.DrawOutline(label, MakeVector2(labelPos.x, ty), 1.0F, col, outline);
				font.DrawOutline(value, MakeVector2(labelPos.x + labelColW - font.Measure(value).x, ty), 1.0F, col, outline);
				labelPos.y += lh;
			};

			snprintf(buf, sizeof(buf), "%.0fkbps", downKbps);
			drawLabel("in", buf, colDown);
			snprintf(buf, sizeof(buf), "%.0fkbps", upKbps);
			drawLabel("out", buf, colUp);
			snprintf(buf, sizeof(buf), "%dms", static_cast<int>(pingMs));
			drawLabel("ping", buf, colPing);
			snprintf(buf, sizeof(buf), "%.0f%%", lossPer);
			drawLabel("loss", buf, white);
		}

		void Client::Draw2D() {
			SPADES_MARK_FUNCTION();

			if (GetWorld())
				Draw2DWithWorld();
			else
				Draw2DWithoutWorld();
		}
	} // namespace client
} // namespace spades