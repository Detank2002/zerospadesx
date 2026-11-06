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

#pragma once

#include <string>
#include <vector>

namespace spades {
	namespace vr {

		/**
		 * The optional OpenXR extensions, and which of them this run has.
		 *
		 * Every feature beyond the core (foveation, space warp, refresh rates, hand
		 * tracking, ...) checks its row here, never the platform: a Quest, a PC
		 * runtime and a PC runtime pretending to be a Quest differ only in what this
		 * table says. Kept free of OpenXR types so Server/test/xr_caps_test.cpp can
		 * check it without a runtime.
		 */
		enum class XrCap {
			CompositionLayerDepth,     // XR_KHR_composition_layer_depth: ASW / motion smoothing
			CompositionLayerCylinder,  // XR_KHR_composition_layer_cylinder: the menu
			CompositionLayerSettings,  // XR_FB_composition_layer_settings: sharpened quads
			DisplayRefreshRate,        // XR_FB_display_refresh_rate: 72/90/120 Hz
			PerformanceSettings,       // XR_EXT_performance_settings: CPU/GPU level hints
			Foveation,                 // XR_FB_foveation
			FoveationConfiguration,    // XR_FB_foveation_configuration: fixed levels
			FoveationVulkan,           // XR_FB_foveation_vulkan: density-map images
			SwapchainUpdateState,      // XR_FB_swapchain_update_state: what foveation sets
			SpaceWarp,                 // XR_FB_space_warp: AppSW
			Passthrough,               // XR_FB_passthrough: the lobby on Quest
			HandTracking,              // XR_EXT_hand_tracking
			HandTrackingAim,           // XR_FB_hand_tracking_aim: pinch
			HandTrackingMesh,          // XR_FB_hand_tracking_mesh
			EyeGaze,                   // XR_EXT_eye_gaze_interaction: your avatar's eyes
			BodyTracking,              // XR_FB_body_tracking
			ViveTrackers,              // XR_HTCX_vive_tracker_interaction
			HapticAmplitudeEnvelope,   // XR_FB_haptic_amplitude_envelope
			HapticPcm,                 // XR_FB_haptic_pcm
			TouchControllerPro,        // XR_FB_touch_controller_pro: Quest Pro bindings
			TouchControllerPlus,       // XR_META_touch_controller_plus: Quest 3 bindings
			TouchControllerProximity,  // XR_FB_touch_controller_proximity: finger near a button
			AndroidCreateInstance,     // XR_KHR_android_create_instance: Android only
			Count
		};

		/** The extension name of a row. */
		const char* XrCapExtension(XrCap);

		struct XrCaps {
			bool has[(int)XrCap::Count] = {};
			bool operator[](XrCap c) const { return has[(int)c]; }
		};

		/**
		 * Choose what to enable: every optional extension the runtime lists, unless
		 * `disabled` names it (vr_disableExtensions, a comma or space separated
		 * list, which is how a PC runtime is made to lack what a Quest lacks), and
		 * Android's only on Android. Appends the chosen names to `enable`, which
		 * the caller has already started with the required ones; a name already
		 * in it is not added twice.
		 */
		XrCaps NegotiateXrCaps(const std::vector<std::string>& available,
		                       const std::string& disabled, bool android,
		                       std::vector<std::string>& enable);
	} // namespace vr
} // namespace spades
