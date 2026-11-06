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

#include "QualityPresets.h"

#include <cstdlib>

#include <Core/Settings.h>

namespace spades {
	namespace gui {
		namespace {
			// A level, from a value for each of the set's setting names, in order
			QualityPreset Level(const char* name, const std::vector<std::string>& keys,
			                    const std::vector<std::string>& values) {
				QualityPreset p;
				p.name = name;
				for (size_t i = 0; i < keys.size(); i++)
					p.values.emplace_back(keys[i], values.at(i));
				return p;
			}

			// "1" and "1.0" are the same value
			bool SameValue(const std::string& a, const std::string& b) {
				if (a == b)
					return true;
				char* endA = nullptr;
				char* endB = nullptr;
				const double x = std::strtod(a.c_str(), &endA);
				const double y = std::strtod(b.c_str(), &endB);
				return !a.empty() && !b.empty() && *endA == '\0' && *endB == '\0' && x == y;
			}
		} // namespace

		const std::vector<QualityPreset>& GetPCQualityPresets() {
			// What costs frames, not what is a matter of taste: outlines, depth of
			// field, camera blur, HDR and the temporal AA are left as they are.
			// High is the defaults. MSAA takes effect at the next start, the rest
			// at the next map.
			static const std::vector<std::string> keys = {
			  "r_multisamples", "r_fxaa",     "r_bloom",          "r_lensFlare",
			  "r_lensFlareDynamic", "r_colorCorrection", "r_softParticles", "r_fogShadow",
			  "r_ssao",         "r_water",    "r_maxAnisotropy",  "r_shadowMapSize",
			  "r_dlights",      "r_modelShadows", "r_radiosity",  "r_physicalLighting"};
			static const std::vector<QualityPreset> presets = {
			  Level("Low", keys,
			        {"0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "1", "1024", "0", "0", "0",
			         "0"}),
			  Level("Medium", keys,
			        {"0", "1", "0", "1", "0", "1", "1", "0", "0", "1", "4", "1024", "1", "1", "0",
			         "0"}),
			  Level("High", keys,
			        {"0", "1", "1", "1", "1", "1", "1", "0", "0", "2", "8", "2048", "1", "1", "0",
			         "0"}),
			  Level("Ultra", keys,
			        {"4", "0", "1", "1", "1", "1", "1", "1", "1", "3", "16", "4096", "1", "1", "1",
			         "1"}),
			};
			return presets;
		}

		const std::vector<QualityPreset>& GetHeadsetQualityPresets() {
			// Performance is what a standalone headset's tiled GPU affords. Every
			// full-screen pass costs a whole load and store of each eye there, so
			// the ones that are only polish go: bloom, FXAA (4x MSAA instead, which
			// a tiler resolves cheaply), HDR and its exposure pass, fog shadows,
			// soft particles (a depth read), outlines, the lens flare, colour
			// correction; and the costly textures shrink. Both eyes in one pass,
			// which halves the draw calls; the water drawn in the scene's pass and
			// the pass resolving itself, which spare a whole copy of the scene.
			// Balanced adds sharper shadows and textures and the colour correction
			// (7.1 ms of a Quest 3's GPU at 1680x1760, against Performance's 6.5);
			// Quality the soft particles, which bring the depth read back (9.6 ms,
			// still 72 FPS there). Not the reflecting water: with it a Quest 3
			// fell to 50 FPS, its CPU and GPU levels raised or not.
			static const std::vector<std::string> keys = {
			  "r_multisamples", "r_fxaa",          "r_bloom",        "r_hdr",
			  "r_fogShadow",    "r_softParticles", "r_outlines",     "r_lensFlare",
			  "r_colorCorrection", "r_depthOfField", "r_cameraBlur", "r_ssao",
			  "r_temporalAA",   "r_water",         "r_maxAnisotropy", "r_shadowMapSize",
			  "vr_multiview",   "r_waterInScene",  "r_resolveInPass"};
			static const std::vector<QualityPreset> presets = {
			  Level("Performance", keys,
			        {"4", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "0", "1", "4",
			         "1024", "1", "1", "1"}),
			  Level("Balanced", keys,
			        {"4", "0", "0", "0", "0", "0", "0", "0", "1", "0", "0", "0", "0", "1", "8",
			         "2048", "1", "1", "1"}),
			  Level("Quality", keys,
			        {"4", "0", "0", "0", "0", "1", "0", "0", "1", "0", "0", "0", "0", "1", "8",
			         "2048", "1", "1", "1"}),
			};
			return presets;
		}

		void ApplyQualityPreset(const QualityPreset& preset) {
			for (const auto& v : preset.values) {
				Settings::ItemHandle item(v.first, nullptr);
				item = v.second;
			}
		}

		bool MatchesQualityPreset(const QualityPreset& preset) {
			for (const auto& v : preset.values) {
				Settings::ItemHandle item(v.first, nullptr);
				if (!SameValue(static_cast<std::string>(item), v.second))
					return false;
			}
			return true;
		}
	} // namespace gui
} // namespace spades
