/*
 Copyright (c) 2026 ZeroSpades developers.

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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * The extensions a server row shows a symbol for, in the order they are
		 * drawn. The plumbing that nearly every extension server has (Message
		 * Types, Kick Reason, Player Properties) gets none: it would only crowd
		 * out the ones that change the game. An id the client has no symbol
		 * for comes last, as a "?".
		 */
		std::vector<std::uint8_t> ExtensionsWithSymbols(const std::vector<std::uint8_t>& ids);

		/**
		 * Draws one extension's symbol in the square at `topLeft`, `size` wide, in
		 * the colour already set on the renderer; lines and boxes only, so no
		 * font or image is needed (but the "?", which takes `font`).
		 */
		void DrawExtensionSymbol(client::IRenderer& r, client::IFont& font, std::uint8_t id,
		                         Vector2 topLeft, float size, Vector4 color);

		/** The names of a server's extensions, every one, for the hover line. */
		std::string ExtensionNames(const std::vector<std::uint8_t>& ids);
	} // namespace gui
} // namespace spades
