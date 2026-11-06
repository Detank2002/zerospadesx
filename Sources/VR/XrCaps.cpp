/*
 Copyright (c) 2026 Detank2002, ZeroSpades developers.

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
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "XrCaps.h"

#include <algorithm>

namespace spades {
	namespace vr {

		namespace {
			// Spelled out rather than taken from the OpenXR headers: the table is
			// tested without them, and an older header must not decide what a newer
			// runtime may offer.
			const char* const kNames[(int)XrCap::Count] = {
			  "XR_KHR_composition_layer_depth",
			  "XR_KHR_composition_layer_cylinder",
			  "XR_FB_composition_layer_settings",
			  "XR_FB_display_refresh_rate",
			  "XR_EXT_performance_settings",
			  "XR_FB_foveation",
			  "XR_FB_foveation_configuration",
			  "XR_FB_foveation_vulkan",
			  "XR_FB_swapchain_update_state",
			  "XR_FB_space_warp",
			  "XR_FB_passthrough",
			  "XR_EXT_hand_tracking",
			  "XR_FB_hand_tracking_aim",
			  "XR_FB_hand_tracking_mesh",
			  "XR_EXT_eye_gaze_interaction",
			  "XR_FB_body_tracking",
			  "XR_HTCX_vive_tracker_interaction",
			  "XR_FB_haptic_amplitude_envelope",
			  "XR_FB_haptic_pcm",
			  "XR_FB_touch_controller_pro",
			  "XR_META_touch_controller_plus",
			  "XR_FB_touch_controller_proximity",
			  "XR_KHR_android_create_instance",
			};

			std::vector<std::string> SplitList(const std::string& s) {
				std::vector<std::string> out;
				std::string cur;
				for (char c : s) {
					if (c == ',' || c == ' ' || c == ';' || c == '\t') {
						if (!cur.empty())
							out.push_back(cur);
						cur.clear();
					} else {
						cur.push_back(c);
					}
				}
				if (!cur.empty())
					out.push_back(cur);
				return out;
			}

			bool Contains(const std::vector<std::string>& v, const std::string& s) {
				return std::find(v.begin(), v.end(), s) != v.end();
			}
		} // namespace

		const char* XrCapExtension(XrCap c) {
			int i = (int)c;
			return i >= 0 && i < (int)XrCap::Count ? kNames[i] : "";
		}

		XrCaps NegotiateXrCaps(const std::vector<std::string>& available,
		                       const std::string& disabled, bool android,
		                       std::vector<std::string>& enable) {
			XrCaps caps;
			const std::vector<std::string> off = SplitList(disabled);
			for (int i = 0; i < (int)XrCap::Count; ++i) {
				const std::string name = kNames[i];
				if (!Contains(available, name) || Contains(off, name))
					continue;
				if ((XrCap)i == XrCap::AndroidCreateInstance && !android)
					continue;
				caps.has[i] = true;
			}

			// An extension whose prerequisite is missing is dropped with it: a
			// runtime may refuse the whole instance over one that is asked for alone.
			static const XrCap kRequires[][2] = {
			  {XrCap::Foveation, XrCap::SwapchainUpdateState},
			  {XrCap::FoveationConfiguration, XrCap::Foveation},
			  {XrCap::FoveationVulkan, XrCap::Foveation},
			  {XrCap::HandTrackingAim, XrCap::HandTracking},
			  {XrCap::HandTrackingMesh, XrCap::HandTracking},
			};
			for (bool changed = true; changed;) {
				changed = false;
				for (const auto& r : kRequires) {
					if (caps[r[0]] && !caps[r[1]]) {
						caps.has[(int)r[0]] = false;
						changed = true;
					}
				}
			}

			for (int i = 0; i < (int)XrCap::Count; ++i)
				if (caps.has[i] && !Contains(enable, kNames[i]))
					enable.push_back(kNames[i]);
			return caps;
		}
	} // namespace vr
} // namespace spades
