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

#include "ExtensionSymbols.h"

#include <algorithm>
#include <cmath>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Client/NetClient.h>
#include <Client/NetProtocol.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		namespace {
			using client::IRenderer;

			/** The symbols' order: what changes the game first. */
			const std::uint8_t kOrder[] = {
			  client::ExtensionTypeTeamplay,    client::ExtensionTypeDamageMarkers,
			  client::ExtensionTypePlayerOutfit, client::ExtensionTypePieMenu,
			  client::ExtensionTypeFlashlight,  client::ExtensionTypeDaytimeWeather,
			  client::ExtensionTypeCompactWorldUpdate, client::ExtensionTypeSmoke,
			  client::ExtensionTypeVRPresence,
			  client::ExtensionTypePlayerLimit};

			bool IsPlumbing(std::uint8_t id) {
				return id == client::ExtensionTypeMessageTypes ||
				       id == client::ExtensionTypeKickReason ||
				       id == client::ExtensionTypePlayerProperties;
			}

			/** A pen in the symbol's square: (0, 0) its top left, (1, 1) its bottom
			 * right. */
			struct Pen {
				IRenderer& r;
				Vector2 o;
				float s;
				float w;

				Vector2 P(float x, float y) const { return o + MakeVector2(x, y) * s; }
				void Line(float x0, float y0, float x1, float y1) const {
					r.DrawLine(P(x0, y0), P(x1, y1), w);
				}
				void Box(float x0, float y0, float x1, float y1) const {
					const Vector2 a = P(x0, y0), b = P(x1, y1);
					r.DrawFilledRect(a.x, a.y, b.x, b.y);
				}
				/** An arc around (cx, cy), from angle a0 to a1 (radians, clockwise
				 * on screen from +x). */
				void Arc(float cx, float cy, float rad, float a0, float a1) const {
					const int n = std::max(3, static_cast<int>(std::fabs(a1 - a0) * 4.0F));
					Vector2 prev = P(cx + rad * std::cos(a0), cy + rad * std::sin(a0));
					for (int i = 1; i <= n; i++) {
						const float a = a0 + (a1 - a0) * static_cast<float>(i) / n;
						const Vector2 next = P(cx + rad * std::cos(a), cy + rad * std::sin(a));
						r.DrawLine(prev, next, w);
						prev = next;
					}
				}
				void Circle(float cx, float cy, float rad) const {
					Arc(cx, cy, rad, 0.0F, 6.2832F);
				}
			};
		} // namespace

		std::vector<std::uint8_t> ExtensionsWithSymbols(const std::vector<std::uint8_t>& ids) {
			std::vector<std::uint8_t> out;
			for (std::uint8_t id : kOrder)
				if (std::find(ids.begin(), ids.end(), id) != ids.end())
					out.push_back(id);
			for (std::uint8_t id : ids)
				if (!IsPlumbing(id) && std::find(std::begin(kOrder), std::end(kOrder), id) ==
				                         std::end(kOrder))
					out.push_back(id);
			return out;
		}

		void DrawExtensionSymbol(IRenderer& r, client::IFont& font, std::uint8_t id,
		                         Vector2 topLeft, float size, Vector4 color) {
			ui::SetColorNP(r, color);
			const Pen p{r, topLeft, size, std::max(1.0F, size * 0.12F)};
			constexpr float kPi = 3.14159265F;
			switch (id) {
				case client::ExtensionTypeTeamplay:
					// Two heads and shoulders, one behind the other
					p.Circle(0.66F, 0.3F, 0.15F);
					p.Arc(0.66F, 0.95F, 0.3F, kPi * 1.15F, kPi * 1.85F);
					p.Circle(0.36F, 0.38F, 0.17F);
					p.Arc(0.36F, 1.05F, 0.34F, kPi * 1.1F, kPi * 1.9F);
					break;
				case client::ExtensionTypeDamageMarkers:
					// A crosshair
					p.Circle(0.5F, 0.5F, 0.3F);
					p.Line(0.5F, 0.0F, 0.5F, 0.32F);
					p.Line(0.5F, 0.68F, 0.5F, 1.0F);
					p.Line(0.0F, 0.5F, 0.32F, 0.5F);
					p.Line(0.68F, 0.5F, 1.0F, 0.5F);
					break;
				case client::ExtensionTypePlayerOutfit:
					// A shirt
					p.Line(0.35F, 0.1F, 0.05F, 0.3F);
					p.Line(0.05F, 0.3F, 0.18F, 0.48F);
					p.Line(0.18F, 0.48F, 0.26F, 0.42F);
					p.Line(0.26F, 0.42F, 0.26F, 0.92F);
					p.Line(0.26F, 0.92F, 0.74F, 0.92F);
					p.Line(0.74F, 0.92F, 0.74F, 0.42F);
					p.Line(0.74F, 0.42F, 0.82F, 0.48F);
					p.Line(0.82F, 0.48F, 0.95F, 0.3F);
					p.Line(0.95F, 0.3F, 0.65F, 0.1F);
					p.Arc(0.5F, 0.1F, 0.15F, 0.0F, kPi);
					break;
				case client::ExtensionTypePieMenu:
					// A pie with a slice out
					p.Arc(0.5F, 0.5F, 0.42F, 0.0F, kPi * 1.5F);
					p.Line(0.5F, 0.5F, 0.92F, 0.5F);
					p.Line(0.5F, 0.5F, 0.5F, 0.08F);
					p.Line(0.62F, 0.38F, 1.0F, 0.38F);
					p.Line(0.62F, 0.38F, 0.62F, 0.0F);
					p.Arc(0.62F, 0.38F, 0.38F, kPi * 1.5F, kPi * 2.0F);
					break;
				case client::ExtensionTypeFlashlight:
					// A lamp, and its beam widening to the right
					p.Box(0.0F, 0.38F, 0.32F, 0.62F);
					p.Box(0.32F, 0.3F, 0.42F, 0.7F);
					p.Line(0.46F, 0.32F, 1.0F, 0.08F);
					p.Line(0.46F, 0.68F, 1.0F, 0.92F);
					p.Line(0.72F, 0.5F, 1.0F, 0.5F);
					break;
				case client::ExtensionTypeDaytimeWeather: {
					// A crescent moon: an arc around the left, closed by a smaller
					// one through the same tips
					const float cx = 0.5F, cy = 0.5F, rad = 0.42F, tip = 0.75F;
					const float tx = rad * std::cos(tip), ty = rad * std::sin(tip);
					const float h = 0.55F * rad; // the inner circle's centre, right of cx
					const float inner = std::sqrt((tx - h) * (tx - h) + ty * ty);
					const float a = std::atan2(ty, tx - h);
					p.Arc(cx, cy, rad, tip, 2.0F * kPi - tip);
					p.Arc(cx + h, cy, inner, a, 2.0F * kPi - a);
					break;
				}
				case client::ExtensionTypeSmoke:
					// A cloud: three puffs on a flat base
					p.Arc(0.3F, 0.62F, 0.22F, kPi * 0.5F, kPi * 1.5F);
					p.Arc(0.52F, 0.42F, 0.26F, kPi, kPi * 2.0F);
					p.Arc(0.76F, 0.6F, 0.2F, kPi * 1.5F, kPi * 2.5F);
					p.Line(0.3F, 0.84F, 0.76F, 0.84F);
					p.Line(0.26F, 0.42F, 0.26F, 0.42F);
					break;
				case client::ExtensionTypeCompactWorldUpdate:
					// A lightning bolt: the world's news, faster
					p.Line(0.62F, 0.0F, 0.22F, 0.55F);
					p.Line(0.22F, 0.55F, 0.52F, 0.55F);
					p.Line(0.52F, 0.55F, 0.36F, 1.0F);
					p.Line(0.36F, 1.0F, 0.8F, 0.4F);
					p.Line(0.8F, 0.4F, 0.5F, 0.4F);
					p.Line(0.5F, 0.4F, 0.62F, 0.0F);
					break;
				case client::ExtensionTypeVRPresence: {
					// A headset: its body, and the strap at either side
					const Vector2 a = p.P(0.08F, 0.28F), b = p.P(0.92F, 0.76F);
					r.DrawOutlinedRect(a.x, a.y, b.x, b.y,
					                   std::max(1, static_cast<int>(p.w + 0.5F)));
					p.Box(0.24F, 0.42F, 0.42F, 0.6F);
					p.Box(0.58F, 0.42F, 0.76F, 0.6F);
					p.Line(0.0F, 0.4F, 0.08F, 0.4F);
					p.Line(0.92F, 0.4F, 1.0F, 0.4F);
					break;
				}
				case client::ExtensionTypePlayerLimit:
					// A player, and a plus: room for more
					p.Circle(0.36F, 0.3F, 0.17F);
					p.Arc(0.36F, 1.0F, 0.34F, kPi * 1.1F, kPi * 1.9F);
					p.Line(0.82F, 0.2F, 0.82F, 0.6F);
					p.Line(0.62F, 0.4F, 1.0F, 0.4F);
					break;
				default: {
					// One this client has no symbol for
					const Vector2 a = p.P(0.0F, 0.0F), b = p.P(1.0F, 1.0F);
					r.DrawOutlinedRect(a.x, a.y, b.x, b.y);
					const Vector2 q = font.Measure("?");
					const float scale = std::min(1.0F, size / std::max(q.y, 1.0F));
					font.Draw("?", topLeft + (MakeVector2(size, size) - q * scale) * 0.5F, scale,
					          color);
					break;
				}
			}
		}

		std::vector<std::uint8_t> ExtensionsInOrder(const std::vector<std::uint8_t>& ids) {
			std::vector<std::uint8_t> out = ExtensionsWithSymbols(ids);
			for (std::uint8_t id : ids)
				if (IsPlumbing(id))
					out.push_back(id);
			return out;
		}

		bool HasExtensionSymbol(std::uint8_t id) { return !IsPlumbing(id); }
	} // namespace gui
} // namespace spades
