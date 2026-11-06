/*
 Copyright (c) 2026 ZeroSpades X contributors

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
#include <utility>
#include <vector>

namespace spades {
	namespace gui {
		/**
		 * A quality level: the values it gives a set of settings. Every level of a
		 * set names the same settings, so a config matching none of them is
		 * "Custom".
		 */
		struct QualityPreset {
			std::string name; // untranslated; the menus translate it
			std::vector<std::pair<std::string, std::string>> values;
		};

		/** The Graphics tab's levels: Low, Medium, High (the defaults), Ultra. */
		const std::vector<QualityPreset>& GetPCQualityPresets();

		/**
		 * The VR tab's levels for a headset: Performance (what a standalone
		 * headset's tiled GPU affords, set on a Quest's first start), Balanced,
		 * Quality.
		 */
		const std::vector<QualityPreset>& GetHeadsetQualityPresets();

		/** Writes the level's values into the config. */
		void ApplyQualityPreset(const QualityPreset&);

		/** Whether every setting the level names has its value now. */
		bool MatchesQualityPreset(const QualityPreset&);
	} // namespace gui
} // namespace spades
