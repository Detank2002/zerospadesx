/*
 Copyright (c) 2026 ZeroSpades VR contributors

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

#include <cctype>
#include <cmath>

#include <Client/IAudioChunk.h>
#include <Client/IAudioDevice.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>

#include "View.h"
#include "VRMenuPointer.h"

namespace spades {
	namespace gui {

		namespace {
			// Trigger hysteresis: a press needs a firm pull, a release a clear let-go,
			// so a finger resting on the trigger never double-clicks.
			constexpr float kPressAt = 0.75F;
			constexpr float kReleaseAt = 0.55F;
		} // namespace

		void VRMenuPointer::Update(View& view, const client::VRFrameState& fs, float screenWidth,
		                           float screenHeight, float dt) {
			using namespace client;

			bool triggerDown[2];
			for (int h = 0; h < 2; h++) {
				float t = fs.hands[h].trigger;
				triggerDown[h] = triggerHeld[h] ? t > kReleaseAt : t > kPressAt;
			}

			// The menu button (or both B/Y buttons, for controllers whose menu
			// button belongs to the system) is Escape.
			bool menu = fs.hands[VRHandLeft].menu || fs.hands[VRHandRight].menu ||
			            (fs.hands[VRHandLeft].buttonSecondary &&
			             fs.hands[VRHandRight].buttonSecondary);
			if (menu && !menuHeld) {
				view.KeyEvent("Escape", true);
				view.KeyEvent("Escape", false);
			}
			menuHeld = menu;

			// The keyboard comes up whenever the view takes typing. The panel turns
			// with the head, so looking at part of it is moving the eyes alone:
			// the keyboard sits where eyes rest, from a little above the middle to
			// a little below (0.36 to 0.76 of the height). Only if the text field
			// is there too does it go to the top or bottom, away from the field,
			// so you see what you type. (0.1.7 put it at the top whenever the
			// field was in the lower half, which is where chat's is: eyes up.) A
			// field below that band (the server list's filter, in its footer) has
			// the keyboard just above it: in the middle it covered the list being
			// filtered, whose top, where the matches are, now stays in view.
			keyboardVisible = view.AcceptsTextInput() && screenWidth > 0.0F;
			if (keyboardVisible) {
				AABB2 field = view.GetTextInputRect();
				int place = KeyboardMiddle;
				if (field.GetHeight() > 0.0F && field.GetMaxY() > screenHeight * 0.34F &&
				    field.GetMinY() < screenHeight * 0.78F)
					place = field.GetMinY() > screenHeight * 0.5F ? KeyboardTop : KeyboardBottom;
				else if (field.GetHeight() > 0.0F && field.GetMinY() >= screenHeight * 0.78F)
					place = KeyboardAboveField;
				LayoutKeyboard(screenWidth, screenHeight, place, field.GetMinY());
			} else {
				shift = false;
			}

			if ((!view.NeedsAbsoluteMouseCoordinate() && !keyboardVisible) || screenWidth <= 0.0F) {
				// Gameplay: the client reads the controllers itself. Let go of a
				// click still held from the menu that just closed.
				if (clickDown) {
					view.KeyEvent("LeftMouseButton", false);
					clickDown = false;
				}
				onPanel = false;
				triggerHeld[0] = triggerDown[0];
				triggerHeld[1] = triggerDown[1];
				return;
			}

			// A view that takes typing without taking the mouse (chat during play)
			// gets the keyboard's keys and nothing else: a click outside the
			// keyboard would be a shot.
			const bool mouseToView = view.NeedsAbsoluteMouseCoordinate();

			// Whichever hand pulls its trigger becomes the pointer.
			for (int h = 0; h < 2; h++)
				if (triggerDown[h] && !triggerHeld[h] && !clickDown)
					hand = h;

			Vector2 uv;
			float distance;
			onPanel = VRRayHitsHudPanel(fs, hand, screenHeight / screenWidth, uv, distance);
			hoverKey = -1;
			if (onPanel) {
				position = MakeVector2(uv.x * screenWidth, uv.y * screenHeight);
				if (keyboardVisible)
					hoverKey = KeyAt(position);
				// Over the keyboard the view is not told where the pointer is, so
				// nothing under the keyboard lights up or takes the click.
				if (mouseToView && !(keyboardVisible && keyboardRect.Contains(position)))
					view.MouseEvent(position.x, position.y);
			}

			bool click = triggerDown[hand];
			const bool pressed = click && !triggerHeld[hand];
			if (keyboardVisible && onPanel && keyboardRect.Contains(position) && !clickDown) {
				if (pressed && hoverKey >= 0) {
					PressKey(view, keys[static_cast<std::size_t>(hoverKey)]);
					KeyFeedback(keys[static_cast<std::size_t>(hoverKey)], hoverKey);
				}
			} else if (mouseToView && click != clickDown && (onPanel || !click)) {
				view.KeyEvent("LeftMouseButton", click);
				clickDown = click;
			}

			// Thumbstick up/down scrolls, faster the further it is pushed.
			float stick = fs.hands[hand].thumbstick.y;
			if (mouseToView && std::fabs(stick) > 0.4F) {
				wheelTimer -= dt;
				if (wheelTimer <= 0.0F) {
					view.WheelEvent(0.0F, stick > 0.0F ? -1.0F : 1.0F);
					wheelTimer = 0.25F - 0.18F * (std::fabs(stick) - 0.4F) / 0.6F;
				}
			} else {
				wheelTimer = 0.0F;
			}

			triggerHeld[0] = triggerDown[0];
			triggerHeld[1] = triggerDown[1];
			pressedTime = std::max(0.0F, pressedTime - dt);
		}

		void VRMenuPointer::LayoutKeyboard(float w, float h, int place, float fieldTop) {
			if (place != KeyboardAboveField)
				fieldTop = 0.0F;
			if (w == laidOutW && h == laidOutH && place == laidOutPlace &&
			    fieldTop == laidOutFieldTop && !keys.empty())
				return;
			laidOutW = w;
			laidOutH = h;
			laidOutPlace = place;
			laidOutFieldTop = fieldTop;
			keys.clear();

			// Five rows over 80% of the panel's width and 40% of its height. Digits,
			// ':', '/' and '.' are there for server addresses.
			const float kbW = w * 0.8F, kbH = h * 0.4F;
			const float kbX = (w - kbW) * 0.5F;
			const float kbY = place == KeyboardTop      ? h * 0.04F
			                  : place == KeyboardMiddle ? h * 0.36F
			                  : place == KeyboardAboveField
			                    ? std::max(h * 0.04F, fieldTop - kbH - h * 0.02F)
			                    : h * 0.56F;
			keyboardRect = AABB2(kbX, kbY, kbW, kbH);
			const float units = 12.5F; // the widest row
			const float unit = kbW / units;
			const float rowH = kbH / 5.0F;
			const float gap = std::max(2.0F, unit * 0.06F);

			struct Item {
				std::string label;
				char ch;
				KeyAction action;
				float width;
			};
			auto chars = [](const char* s) {
				std::vector<Item> row;
				for (; *s; ++s)
					row.push_back({std::string(1, *s), *s, KeyChar, 1.0F});
				return row;
			};
			std::vector<std::vector<Item>> rows;
			rows.push_back(chars("1234567890-"));
			rows.back().push_back({"Back", 0, KeyBack, 1.5F});
			rows.push_back(chars("qwertyuiop/"));
			rows.push_back(chars("asdfghjkl:'"));
			rows.back().push_back({"Enter", 0, KeyEnter, 1.5F});
			rows.push_back({{"Shift", 0, KeyShift, 1.5F}});
			for (auto& it : chars("zxcvbnm,.?!"))
				rows.back().push_back(it);
			rows.push_back(chars("@_"));
			rows.back().insert(rows.back().begin() + 1, {"Space", ' ', KeySpace, 7.0F});

			for (std::size_t r = 0; r < rows.size(); r++) {
				float rowWidth = 0.0F;
				for (auto& it : rows[r])
					rowWidth += it.width;
				float x = kbX + (kbW - rowWidth * unit) * 0.5F;
				const float y = kbY + rowH * static_cast<float>(r);
				for (auto& it : rows[r]) {
					Key k;
					k.label = it.label;
					k.ch = it.ch;
					k.action = it.action;
					k.rect = AABB2(x + gap, y + gap, it.width * unit - gap * 2.0F, rowH - gap * 2.0F);
					keys.push_back(k);
					x += it.width * unit;
				}
			}
		}

		int VRMenuPointer::KeyAt(const Vector2& p) const {
			for (std::size_t i = 0; i < keys.size(); i++)
				if (keys[i].rect.Contains(p))
					return static_cast<int>(i);
			return -1;
		}

		void VRMenuPointer::KeyFeedback(const Key& key, int index) {
			// Kept small: the menus' own soft tick, a touch lower for the big keys
			// and a little different each press so a word does not drone; a tick
			// in the hand that pressed; the key lit and in for a tenth of a second.
			pressedKey = index;
			pressedTime = 0.12F;
			if (client::IVRSystem* vr = client::GetVRSystem())
				vr->Vibrate(hand, 0.2F, 0.012F);
			if (!audio)
				return;
			if (!clickSound)
				clickSound = audio->RegisterSound("Sounds/Feedback/Limbo/Hover.opus");
			if (!clickSound)
				return;
			client::AudioParam param;
			param.volume = 0.5F;
			static unsigned seed = 1;
			seed = seed * 1103515245u + 12345u;
			const float jitter = static_cast<float>((seed >> 16) & 0xFF) / 255.0F; // 0..1
			param.pitch = (key.action == KeyChar ? 1.1F : 0.85F) + jitter * 0.1F;
			audio->PlayLocal(clickSound.GetPointerOrNull(), param);
		}

		void VRMenuPointer::PressKey(View& view, const Key& key) {
			switch (key.action) {
				case KeyChar: {
					char c = key.ch;
					if (shift && std::isalpha(static_cast<unsigned char>(c)))
						c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
					view.TextInputEvent(std::string(1, c));
					// Let go of it, as a keyboard does: the UI repeats a typed
					// character until a key comes up ("bbbbbb" in 0.1.7).
					view.KeyEvent(std::string(1, key.ch), false);
					shift = false; // one capital, like a phone's
					break;
				}
				case KeySpace:
					view.TextInputEvent(" ");
					view.KeyEvent(" ", false);
					break;
				case KeyBack:
					view.KeyEvent("BackSpace", true);
					view.KeyEvent("BackSpace", false);
					break;
				case KeyEnter:
					view.KeyEvent("Enter", true);
					view.KeyEvent("Enter", false);
					break;
				case KeyShift: shift = !shift; break;
			}
		}

		void VRMenuPointer::DrawKeyboard(client::IRenderer& r, client::IFont& font) {
			if (!keyboardVisible || keys.empty())
				return;
			const float pad = 8.0F;
			r.SetColorAlphaPremultiplied(MakeVector4(0.0F, 0.0F, 0.0F, 0.75F));
			r.DrawImage(nullptr, AABB2(keyboardRect.GetMinX() - pad, keyboardRect.GetMinY() - pad,
			                           keyboardRect.GetWidth() + pad * 2.0F,
			                           keyboardRect.GetHeight() + pad * 2.0F));
			for (std::size_t i = 0; i < keys.size(); i++) {
				const Key& k = keys[i];
				const bool hot = static_cast<int>(i) == hoverKey;
				const bool on = k.action == KeyShift && shift;
				const bool down = static_cast<int>(i) == pressedKey && pressedTime > 0.0F;
				const float a = down ? 0.8F : hot ? 0.55F : on ? 0.4F : 0.22F;
				r.SetColorAlphaPremultiplied(MakeVector4(a, a, a, 0.9F));
				// Pressed in: a little smaller for its moment.
				const float in = down ? 2.0F : 0.0F;
				r.DrawImage(nullptr, AABB2(k.rect.GetMinX() + in, k.rect.GetMinY() + in,
				                           k.rect.GetWidth() - in * 2.0F,
				                           k.rect.GetHeight() - in * 2.0F));

				std::string label = k.label;
				if (k.action == KeyChar && shift &&
				    std::isalpha(static_cast<unsigned char>(k.ch)))
					label = std::string(1, static_cast<char>(std::toupper(
					                         static_cast<unsigned char>(k.ch))));
				Vector2 size = font.Measure(label);
				Vector2 at = MakeVector2(k.rect.GetMinX() + (k.rect.GetWidth() - size.x) * 0.5F,
				                         k.rect.GetMinY() + (k.rect.GetHeight() - size.y) * 0.5F);
				font.Draw(label, MakeVector2(std::floor(at.x), std::floor(at.y)), 1.0F,
				          MakeVector4(1, 1, 1, 1));
			}
		}
	} // namespace gui
} // namespace spades
