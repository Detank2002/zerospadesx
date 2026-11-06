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

#include <list>
#include <string>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class IFont;
		class IImage;
		class IRenderer;

		static const char MsgColorTeam1 = 1;
		static const char MsgColorTeam2 = 2;
		static const char MsgColorTeam3 = 3;
		static const char MsgColorRed = 4;
		static const char MsgColorGreen = 5;
		static const char MsgColorRestore = 6;
		static const char MsgColorGray = 7;
		static const char MsgColorYellow = 8;
		static const char MsgImage = 9;
		static const char MsgColorMax = MsgImage;
		static const char MsgColorFriendlyFire = MsgColorRed;
		static const char MsgColorSysInfo = MsgColorYellow;

		class ChatWindow {
			Client* client;
			IRenderer& renderer;
			IFont* font;
			// The font and size SetTextScale gave (the wrist panel's), else the
			// window's own at 1
			IFont* baseFont;
			float textScale = 1.0F;
			// From the chat's last line to the bottom of what it is drawn on
			float bottomMargin = 64.0F;

			struct ChatEntry {
				std::string msg;
				float height;
				float fade = 0.0F; // usual fade opacity
				float bufferFade = 0.0F;
				float timeFade; // timeout fade opacity

				ChatEntry(const std::string& Msg, float Height, float TimeFade)
					: msg(Msg), height(Height), timeFade(TimeFade) {}
			};

			std::list<ChatEntry> entries;
			float firstY;
			// The height of the surface last drawn on. Update() lays the lines
			// out before Draw() runs, and in a headset Draw() is on the wrist
			// panel, not the window.
			float layoutHeight = 0.0F;
			float LayoutHeight();
			bool killfeed;

			float GetWidth();
			float GetHeight();
			float GetLineHeight();

			/** Kill images are named by a letter from `a`, after `MsgImage`. */
			static constexpr int kKillImageCount = 13;
			std::vector<IImage*> killImages;
			IImage* GetKillImage(char);

		public:
			ChatWindow(Client*, IFont* font, bool killfeed);
			~ChatWindow();

			void AddMessage(const std::string&, float fadeTime = -1.0F);
			static std::string ColoredMessage(const std::string&, char);
			static std::string TeamColorMessage(const std::string&, int);
			static std::string KillImage(int killType, int weaponType = -1);
			/** The message as text alone: the color and image control bytes this
			 * window acts on are removed, as it never draws them as characters. */
			static std::string StripFormatting(const std::string&);

			/** Returns the color that a message color control byte selects. */
			Vector4 GetColor(char);

			void Update(float dt);
			void Draw();

			/** Draws in `scaledFont` with lines and images `scale` times their size
			 * (the wrist panel's larger text); nullptr, 1: the window's own. */
			void SetTextScale(IFont* scaledFont, float scale);
			/** The room under the chat's last line (the wrist's watch face). */
			void SetBottomMargin(float margin) { bottomMargin = margin; }
		};
	} // namespace client
} // namespace spades
