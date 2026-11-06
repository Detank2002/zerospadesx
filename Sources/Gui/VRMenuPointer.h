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

#pragma once

#include <string>
#include <vector>

#include <Client/VRState.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Client/IAudioChunk.h>

namespace spades {
	namespace client {
		class IRenderer;
		class IFont;
		class IAudioDevice;
	} // namespace client
	namespace gui {
		class View;

		/**
		 * Turns a controller into the mouse for every 2D screen: the main menu and
		 * its server browser, the team and weapon picker, the scoreboard, the
		 * pause menu.
		 *
		 * All of those draw through the renderer's 2D layer, which in VR is the
		 * HUD panel, so the only thing they lack is a pointer. The aim ray is cut
		 * against the panel and the hit becomes absolute mouse motion; the trigger
		 * is the left button and the thumbstick is the wheel. Nothing here is
		 * specific to a screen, so a new menu works without being taught about VR.
		 *
		 * The menu button is Escape on every screen, which is what opens and
		 * closes the in-game menu on a keyboard too.
		 */
		class VRMenuPointer {
		public:
			/** Feeds `view` for one frame. Only moves the mouse while the view wants
			 * absolute coordinates; the menu button is passed on regardless. */
			void Update(View& view, const client::VRFrameState& fs, float screenWidth,
			            float screenHeight, float dt);

			bool IsOnPanel() const { return onPanel; }
			int GetHand() const { return hand; }
			/** In screen pixels, when IsOnPanel(). */
			Vector2 GetPosition() const { return position; }

			/** The keyboard, while the view takes typing (chat, an address).
			 * Drawn over the view, so after it has drawn. */
			void DrawKeyboard(client::IRenderer&, client::IFont&);

			/** For the keyboard's click. */
			void SetAudio(client::IAudioDevice* a) { audio = a; }

		private:
			// A keyboard you point at, as there is no keyboard to look at in a
			// headset. Clicks on it type into the view instead of reaching it.
			enum KeyAction { KeyChar, KeyBack, KeyEnter, KeyShift, KeySpace };
			struct Key {
				std::string label;
				char ch;
				KeyAction action;
				AABB2 rect;
			};
			std::vector<Key> keys;
			bool keyboardVisible = false;
			// Where it sits: just below eye level unless the text field is there.
			enum KeyboardPlace { KeyboardTop, KeyboardMiddle, KeyboardBottom, KeyboardAboveField };
			bool shift = false;
			int hoverKey = -1;
			AABB2 keyboardRect;
			float laidOutW = 0.0F, laidOutH = 0.0F;
			int laidOutPlace = -1;
			float laidOutFieldTop = -1.0F;
			/** `fieldTop`: the text field's top, for KeyboardAboveField. */
			void LayoutKeyboard(float w, float h, int place, float fieldTop = 0.0F);
			int KeyAt(const Vector2&) const;
			void PressKey(View&, const Key&);

			// A key pressed answers: a soft click, a tick in the hand, and the key
			// lit and pressed in for a moment (pressedKey, pressedTime).
			client::IAudioDevice* audio = nullptr; // weak
			Handle<client::IAudioChunk> clickSound;
			int pressedKey = -1;
			float pressedTime = 0.0F;
			void KeyFeedback(const Key&, int index);

			int hand = client::VRHandRight;
			bool onPanel = false;
			Vector2 position{0.0F, 0.0F};
			bool clickDown = false;
			bool triggerHeld[2] = {false, false};
			bool menuHeld = false;
			float wheelTimer = 0.0F;
		};
	} // namespace gui
} // namespace spades
