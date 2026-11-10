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

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <Gui/UI/MainScreen/CountryFlags.h>
#include <Gui/UI/Widgets/ButtonBase.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		class MainScreenHelper;
		class MainScreenServerItem;

		/** One row of the server browser. */
		class ServerListItem : public ui::ButtonBase {
			MainScreenHelper* helper; // weak
			Handle<FlagIconRenderer> flagIconRenderer;

		public:
			Handle<MainScreenServerItem> item;

			ServerListItem(ui::UIManager* manager, MainScreenHelper* helper,
			               Handle<MainScreenServerItem> item);
			void Render() override;
		};

		using ServerListItemEventHandler = std::function<void(MainScreenServerItem&)>;

		/** `ListViewModel` for the server browser, with per-row element caching. */
		class ServerListModel : public ui::ListViewModel {
			ui::UIManager* manager; // weak
			MainScreenHelper* helper; // weak
			std::vector<Handle<ServerListItem>> itemElements;

			void OnItemClicked(ui::UIElement& sender);
			void OnItemDoubleClicked(ui::UIElement& sender);
			void OnItemRightClicked(ui::UIElement& sender);

		public:
			std::vector<Handle<MainScreenServerItem>> list;

			ServerListItemEventHandler itemActivated;
			ServerListItemEventHandler itemDoubleClicked;
			ServerListItemEventHandler itemRightClicked;

			ServerListModel(ui::UIManager* manager, MainScreenHelper* helper,
			                std::vector<Handle<MainScreenServerItem>> list);

			void ReplaceList(std::vector<Handle<MainScreenServerItem>> newList);

			int GetNumRows() override { return static_cast<int>(list.size()); }
			Handle<ui::UIElement> CreateElement(int row) override;
		};

		/**
		 * The extensions of the server row the mouse is on, each with its symbol
		 * and name: a key to the row's symbols. In the empty space beside the
		 * list, level with the row, where the screen is wide enough; else in a
		 * box under the row. A row sets it as it renders; this, added after the
		 * list so drawn over its rows, shows and clears it. Where it is placed
		 * does not matter: it draws by the row.
		 */
		class ServerExtensionsHint : public ui::UIElement {
		public:
			ServerExtensionsHint(ui::UIManager* manager) : ui::UIElement(manager) {}
			/** Set by the hovered row each frame, with the row's place on screen. */
			static void SetHovered(std::string title, std::vector<std::uint8_t> ids, AABB2 row);
			void Render() override;
		};

		/** A clickable column header for the server browser. */
		class ServerListHeader : public ui::ButtonBase {
		public:
			std::string text;
			Vector2 alignment = MakeVector2(0.0F, 0.5F);

			ServerListHeader(ui::UIManager* manager) : ui::ButtonBase(manager) {}
			void Render() override;
		};

		/** "Fetching server list..." placeholder. */
		class MainScreenServerListLoadingView : public ui::UIElement {
		public:
			MainScreenServerListLoadingView(ui::UIManager* manager) : ui::UIElement(manager) {}
			void Render() override;
		};

		/** "Failed to fetch the server list." placeholder. */
		class MainScreenServerListErrorView : public ui::UIElement {
		public:
			MainScreenServerListErrorView(ui::UIManager* manager) : ui::UIElement(manager) {}
			void Render() override;
		};
	} // namespace gui
} // namespace spades
