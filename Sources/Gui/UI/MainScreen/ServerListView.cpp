/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#include <algorithm>

#include "ExtensionSymbols.h"
#include "ServerListView.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Client/NetClient.h>
#include <Core/Debug.h>
#include <Core/Strings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		// -- ServerListItem --

		ServerListItem::ServerListItem(UIManager* manager, MainScreenHelper* helper,
		                               Handle<MainScreenServerItem> item)
		    : ui::ButtonBase(manager), helper(helper), item(std::move(item)) {
			flagIconRenderer = Handle<FlagIconRenderer>::New(&manager->GetRenderer());
		}

		void ServerListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			// adjust based on screen width
			float maxContentsWidth = 750.0F;
			float scaleF = std::min(GetManager().screenWidth / maxContentsWidth, 1.0F);

			float itemOffsetX = 2.0F;
			float itemOffsetY = 2.0F;
			float flagIconOffsetX = (itemOffsetX + 12.0F) * scaleF;
			float nameOffsetX = (itemOffsetX + 24.0F) * scaleF;
			float slotsOffsetX = 335.0F * scaleF;
			float mapNameOffsetX = 372.0F * scaleF;
			float gameModeOffsetX = 580.0F * scaleF;
			float protocolOffsetX = 658.0F * scaleF;
			float pingOffsetX = sz.x - itemOffsetX;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			Vector4 fgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);

			if (item->IsFavorite()) {
				bgcolor = MakeVector4(0.3F, 0.3F, 1.0F, 0.1F);
				fgcolor = MakeVector4(220, 220, 0, 255) / 255.0F;
			}

			if (pressed && hover) {
				bgcolor.w = 0.3F;
			} else if (hover) {
				bgcolor.w = 0.15F;
			}

			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			Vector4 col = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (!hover && !item->IsFavorite() && item->GetNumPlayers() == 0) {
				col.w *= 0.5F;
				fgcolor.w *= 0.5F;
			}

			// Draw server country flag icon
			SetColorNP(r, col);
			flagIconRenderer->DrawIcon(
			    item->GetCountry(), pos + MakeVector2(flagIconOffsetX, itemOffsetY + (sz.y * 0.5F)));

			// Draw server name
			font->Draw(item->GetName(), pos + MakeVector2(nameOffsetX, itemOffsetY), 1.0F, fgcolor);

			// The extensions it was seen with, a symbol each after the name, as many
			// as fit before the slots; their names in the footer while hovered.
			const std::vector<std::uint8_t> extensions = item->GetExtensions();
			if (!extensions.empty()) {
				const Vector4 symbolColor = MakeVector4(1.0F, 0.72F, 0.2F, col.w);
				const float symbolSize = std::min(11.0F, sz.y - 8.0F);
				const float gap = 4.0F;
				const float limit = slotsOffsetX - 22.0F * scaleF;
				float x = nameOffsetX + font->Measure(item->GetName()).x + 8.0F;
				const float y = (sz.y - symbolSize) * 0.5F;
				for (std::uint8_t id : ExtensionsWithSymbols(extensions)) {
					if (x + symbolSize > limit)
						break;
					DrawExtensionSymbol(r, *font, id, pos + MakeVector2(x, y), symbolSize,
					                    symbolColor);
					x += symbolSize + gap;
				}
				if (hover)
					ServerExtensionsHint::SetHovered(item->GetName(), extensions,
					                                 AABB2(pos.x, pos.y, sz.x, sz.y));
			}

			// Draw server slots
			std::string playersStr =
			    std::to_string(item->GetNumPlayers()) + "/" + std::to_string(item->GetMaxPlayers());
			Vector4 playersCol = col;
			if (item->GetNumPlayers() >= item->GetMaxPlayers())
				playersCol = MakeVector4(1.0F, 0.7F, 0.7F, col.w);
			else if (item->GetNumPlayers() >= item->GetMaxPlayers() * 3 / 4)
				playersCol = MakeVector4(1.0F, 1.0F, 0.7F, col.w);
			else if (item->GetNumPlayers() == 0)
				playersCol = MakeVector4(0.7F, 0.7F, 1.0F, col.w);
			font->Draw(playersStr,
			           pos + MakeVector2(slotsOffsetX - font->Measure(playersStr).x * 0.5F,
			                             itemOffsetY),
			           1.0F, playersCol);

			// Draw map name
			font->Draw(item->GetMapName(), pos + MakeVector2(mapNameOffsetX, itemOffsetY), 1.0F,
			           col);

			// Draw server gamemode
			font->Draw(item->GetGameMode(),
			           pos + MakeVector2(gameModeOffsetX -
			                                 font->Measure(item->GetGameMode()).x * 0.5F,
			                             itemOffsetY),
			           1.0F, col);

			// Draw server protocol
			font->Draw(item->GetProtocol(),
			           pos + MakeVector2(protocolOffsetX -
			                                 font->Measure(item->GetProtocol()).x * 0.5F,
			                             itemOffsetY),
			           1.0F, col);

			// Draw server ping
			int ping = helper->GetServerPing(item->GetAddress());
			std::string pingStr = (ping == -1) ? "?" : std::to_string(ping);

			Vector4 pingCol = MakeVector4(1.0F, 1.0F, 1.0F, col.w);
			if (ping != -1) {
				int maxPing = 300;
				float ratio = static_cast<float>(std::min(ping, maxPing)) /
				              static_cast<float>(maxPing);
				float hue = (1.0F - ratio) * 240.0F / 360.0F;
				Vector3 gradient = HSV2RGB(hue, 0.9F, 1.0F);
				pingCol.x = gradient.x;
				pingCol.y = gradient.y;
				pingCol.z = gradient.z;
			}

			font->Draw(pingStr,
			           pos + MakeVector2(pingOffsetX - font->Measure(pingStr).x, itemOffsetY), 1.0F,
			           pingCol);
		}

		// -- ServerExtensionsHint --

		namespace {
			struct HoveredExtensions {
				std::string title;
				std::vector<std::uint8_t> ids;
				AABB2 row;
			};
			HoveredExtensions g_hovered;
		} // namespace

		void ServerExtensionsHint::SetHovered(std::string title, std::vector<std::uint8_t> ids,
		                                      AABB2 row) {
			g_hovered = {std::move(title), std::move(ids), row};
		}

		void ServerExtensionsHint::Render() {
			HoveredExtensions h;
			std::swap(h, g_hovered); // shown once: the row sets it each frame
			client::IFont* font = GetFont();
			if (h.ids.empty() || !font)
				return;
			client::IRenderer& r = GetManager().GetRenderer();
			const Vector4 accent = MakeVector4(1.0F, 0.72F, 0.2F, 1.0F);
			const Vector4 plain = MakeVector4(0.75F, 0.75F, 0.75F, 1.0F);

			const std::vector<std::uint8_t> ids = ExtensionsInOrder(h.ids);
			const float pad = 8.0F;
			const float lineHeight = font->Measure("Ag").y + 2.0F;
			const float symbolSize = 12.0F;
			const float textX = symbolSize + 8.0F;
			float width = font->Measure(h.title).x;
			for (std::uint8_t id : ids)
				width = std::max(width,
				                 textX + font->Measure(client::GetExtensionDisplayName(id)).x);
			const float boxW = width + pad * 2.0F;
			const float boxH = lineHeight * static_cast<float>(ids.size() + 1) + pad * 2.0F;
			const float screenW = GetManager().screenWidth;
			const float screenH = GetManager().screenHeight;

			float x, y;
			if (screenW - h.row.GetMaxX() >= boxW + 24.0F) {
				// Beside the list, level with the row, kept on the screen
				x = h.row.GetMaxX() + 16.0F;
				y = std::max(8.0F, std::min(h.row.GetMinY() - pad, screenH - boxH - 8.0F));
			} else {
				// Under the row, or over it when it would run off the bottom
				x = h.row.GetMinX() + 20.0F;
				y = h.row.GetMaxY() + 2.0F;
				if (y + boxH > screenH - 60.0F)
					y = h.row.GetMinY() - 2.0F - boxH;
			}

			ui::SetColorNP(r, MakeVector4(0.08F, 0.08F, 0.1F, 0.94F));
			r.DrawFilledRect(x, y, x + boxW, y + boxH);
			ui::SetColorNP(r, MakeVector4(1.0F, 0.72F, 0.2F, 0.7F));
			r.DrawOutlinedRect(x, y, x + boxW, y + boxH);

			float ty = y + pad;
			font->Draw(h.title, MakeVector2(x + pad, ty), 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			for (std::uint8_t id : ids) {
				ty += lineHeight;
				const bool symbol = HasExtensionSymbol(id);
				if (symbol)
					DrawExtensionSymbol(r, *font, id,
					                    MakeVector2(x + pad, ty + (lineHeight - 2.0F - symbolSize) * 0.5F),
					                    symbolSize, accent);
				font->Draw(client::GetExtensionDisplayName(id), MakeVector2(x + pad + textX, ty),
				           1.0F, symbol ? accent : plain);
			}
		}

		// -- ServerListModel --

		ServerListModel::ServerListModel(UIManager* manager, MainScreenHelper* helper,
		                                 std::vector<Handle<MainScreenServerItem>> list)
		    : manager(manager), helper(helper), list(std::move(list)) {
			itemElements.resize(this->list.size());
		}

		void ServerListModel::ReplaceList(std::vector<Handle<MainScreenServerItem>> newList) {
			SPAssert(newList.size() == list.size());
			list = std::move(newList);

			// Kinda dirty hack
			for (size_t i = 0, count = list.size(); i < count; ++i) {
				if (itemElements[i])
					itemElements[i]->item = list[i];
			}
		}

		void ServerListModel::OnItemClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(*item->item);
		}

		void ServerListModel::OnItemDoubleClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemDoubleClicked)
				itemDoubleClicked(*item->item);
		}

		void ServerListModel::OnItemRightClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemRightClicked)
				itemRightClicked(*item->item);
		}

		Handle<UIElement> ServerListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				Handle<ServerListItem> i =
				    Handle<ServerListItem>::New(manager, helper, list[row]);
				i->activated = [this](UIElement& s) { OnItemClicked(s); };
				i->doubleClicked = [this](UIElement& s) { OnItemDoubleClicked(s); };
				i->rightClicked = [this](UIElement& s) { OnItemRightClicked(s); };
				itemElements[row] = i;
			}
			return itemElements[row].Cast<UIElement>();
		}

		// -- ServerListHeader --

		void ServerListHeader::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * alignment;

			if (alignment.x == 0.0F)
				txtPos.x += 2.0F;

			if (pressed && hover)
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.3F));
			else if (hover)
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.15F));
			else
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.0F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
		}

		// -- placeholders --

		void MainScreenServerListLoadingView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			std::string text = _Tr("MainScreen", "Fetching server list...");

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.8F));
		}

		void MainScreenServerListErrorView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			std::string text = _Tr("MainScreen", "Failed to fetch the server list.");

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.8F));
		}
	} // namespace gui
} // namespace spades
