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

#include <ZeroSpades.h>

#if USE_OPENXR

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#ifdef __ANDROID__
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <Client/VRState.h>
#include <VR/XrCaps.h>

namespace spades {
	namespace vr {

		/** One OpenXR swapchain and the Vulkan images behind it. */
		struct XRSwapchain {
			XrSwapchain handle = XR_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			uint32_t width = 0;
			uint32_t height = 0;
			std::vector<VkImage> images;
			/** Index of the image acquired this frame, or UINT32_MAX. */
			uint32_t acquired = UINT32_MAX;
		};

		/**
		 * The headset, through OpenXR with Vulkan.
		 *
		 * Lifetime, in the order the runner drives it:
		 *   1. TryCreate()        - instance and system; before any Vulkan object.
		 *   2. CreateVulkan*()    - SDLVulkanDevice builds its instance and device
		 *                           through these, so the runtime gets the
		 *                           extensions and the GPU it needs.
		 *   3. CreateSession()    - once the Vulkan device exists.
		 *   4. per frame: PollEvents(), BeginFrame(), [renderer acquires, draws
		 *      and releases the eye and HUD images], EndFrame() from Flip.
		 *
		 * Everything runs on the render thread: the runtime may use the Vulkan
		 * queue inside xrBeginFrame, xrEndFrame and the swapchain calls, and the
		 * renderer submits to the same queue.
		 */
		class OpenXRSystem : public client::IVRSystem {
		public:
			/** Returns null, having logged why, when there is no runtime or no
			 * headset. The game then runs flat. */
			static OpenXRSystem* TryCreate();
			~OpenXRSystem() override;

			/** For the launcher: the active runtime's name, or empty when there is
			 * none (or it cannot give Vulkan images), with the reason in `why`.
			 * Asks only about the runtime, not a headset, which may well be
			 * switched on after the launcher is. */
			static std::string ProbeRuntime(std::string& why);

			// --- Vulkan bootstrap (XR_KHR_vulkan_enable2) -----------------
			VkResult CreateVulkanInstance(const VkInstanceCreateInfo& info, VkInstance& out);
			VkPhysicalDevice GetVulkanPhysicalDevice(VkInstance instance);
			VkResult CreateVulkanDevice(VkPhysicalDevice physicalDevice,
			                            const VkDeviceCreateInfo& info, VkDevice& out);
			/** The Vulkan API version the runtime needs the instance to ask for. */
			uint32_t GetRequiredVulkanApiVersion() const { return requiredVulkanApiVersion; }

			bool CreateSession(VkInstance instance, VkPhysicalDevice physicalDevice,
			                   VkDevice device, uint32_t queueFamily, uint32_t queueIndex);
			bool HasSession() const { return session != XR_NULL_HANDLE; }

			/** The optional extensions this run enabled. Features check this, never
			 * the platform. */
			const XrCaps& GetCaps() const { return caps; }
			/** Ends and destroys the session and everything made from it. Must run
			 * before the Vulkan device it was created with is destroyed. */
			void DestroySession();

			// --- frame ----------------------------------------------------
			/** Handles runtime events: session state changes, instance loss. */
			void PollEvents();
			/** xrWaitFrame + xrBeginFrame when the session is running; syncs
			 * input and locates the head, eyes and hands. Blocks to pace the game
			 * at the headset's refresh rate. */
			void BeginFrame();
			/** Ends the frame begun by BeginFrame, submitting the eye views if
			 * `submitEyes`, the HUD panel if `submitHud` and the wrist panel if
			 * `submitWrist`. A no-op when no frame is in progress. */
			void EndFrame(bool submitEyes, bool submitHud, bool submitWrist = false);

			/** The eye images for this frame, created at the size in the frame
			 * state. The image is in COLOR_ATTACHMENT_OPTIMAL when acquired and
			 * must be left in it before release. */
			bool AcquireEyeImage(int eye, VkImage& image);
			void ReleaseEyeImage(int eye);
			VkFormat GetEyeFormat() const { return eyeSwapchains[0].format; }

			/** The HUD panel's image, `width` x `height`; the swapchain is
			 * (re)created to match. Same layout rules as the eye images. */
			bool AcquireHudImage(uint32_t width, uint32_t height, VkImage& image);
			void ReleaseHudImage();
			VkFormat GetHudFormat() const { return hudSwapchain.format; }

			/** The wrist panel's image, kVRWristCanvasWidth x kVRWristCanvasHeight,
			 * placed where the frame state's `wristPanel` says. Same layout rules. */
			bool AcquireWristImage(VkImage& image);
			void ReleaseWristImage();

			/** Recentres the play space on where the head is now. */
			void RequestRecenter() { recenterRequested = true; }
			std::string GetControllerProfile() const override { return controllerProfile; }
			std::string GetHeadsetName() const override { return headsetName; }
			bool ConsumeRecenterRequest() override {
				bool r = recenterRequested;
				recenterRequested = false;
				return r;
			}

			/** The runtime asked the app to quit (e.g. from its system menu). */
			bool WantsQuit() const { return quitRequested; }

			// --- client::IVRSystem ----------------------------------------
			const client::VRFrameState& GetFrameState() override { return frameState; }
			void Vibrate(int hand, float amplitude, float seconds) override;
			void Vibrate(int hand, client::VRHapticShape shape, float amplitude,
			             float seconds) override;
			void SetHeldMap(bool held) override;
			void SetHudFollowsHead() override { hudFollowAsked = true; }

		private:
			OpenXRSystem() = default;
			bool Initialize();
			bool CreateActions();
			bool SuggestBindings();
			void CreateEyeSwapchains();
			bool CreateSwapchain(XRSwapchain& sc, uint32_t width, uint32_t height,
			                     const std::vector<int64_t>& preferredFormats);
			void DestroySwapchain(XRSwapchain& sc);
			bool AcquirePanelImage(XRSwapchain& sc, uint32_t width, uint32_t height,
			                       VkImage& image, const char* what);
			void ReleasePanelImage(XRSwapchain& sc, const char* what);
			void UpdateInput();
			void LocateViews();
			/** vr_traceRecord / vr_traceReplay, after the frame's input is read. */
			void UpdateTrace();
			void HandleSessionStateChange(XrSessionState state);
			/** Asks for the CPU and GPU levels vr_performanceBoost wants, when it changes. */
			void ApplyPerformanceLevels();
			XrPath Path(const char* s);
			bool Check(XrResult r, const char* what);

			XrInstance instance = XR_NULL_HANDLE;
			XrSystemId systemId = XR_NULL_SYSTEM_ID;
			XrCaps caps;
			XrSession session = XR_NULL_HANDLE;
			XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
			bool sessionRunning = false;
			bool quitRequested = false;
			bool recenterRequested = false;
			std::string controllerProfile; // the tool hand's, else the other's
			std::string headsetName;
			// The session has begun and the app has not had focus since.
			bool recenterOnFocus = false;
			// The levels asked for this session: -1 none yet, 0 sustained low, 1 high
			int appliedPerformanceBoost = -1;

			XrSpace appSpace = XR_NULL_HANDLE;  // STAGE, or LOCAL without a stage
			XrSpace viewSpace = XR_NULL_HANDLE; // for the head and the HUD panel
			XrReferenceSpaceType appSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;

			std::vector<XrViewConfigurationView> viewConfigs;
			XrView views[2]{};
			XrFovf renderFov[2]{};
			/** vr_symmetricFrustum, as it was when the eye images were made. */
			bool symmetricFrustum = false;

			XRSwapchain eyeSwapchains[2];
			XRSwapchain hudSwapchain;
			XRSwapchain wristSwapchain;
			std::vector<int64_t> runtimeFormats;

			VkDevice vkDevice = VK_NULL_HANDLE;
			bool frameBegun = false;
			XrTime predictedDisplayTime = 0;

			// The input trace (UpdateTrace): the file being recorded, or the
			// lines being replayed and how far the replay is.
			bool heldMapWanted = false;
			// The front panel (PlaceHudPanel): asked to follow the head this frame,
			// and where it stands in the room otherwise.
			bool hudFollowAsked = false;
			bool hudStanding = false;
			client::VRPose hudStandPose;
			float hudAwayTime = 0.0F;
			void PlaceHudPanel();
			std::FILE* traceOut = nullptr;
			std::vector<std::string> traceIn;
			size_t traceFrame = 0;
			bool traceStarted = false;

			uint32_t requiredVulkanApiVersion = VK_API_VERSION_1_0;

			// Input
			XrActionSet actionSet = XR_NULL_HANDLE;
			XrAction gripPoseAction = XR_NULL_HANDLE;
			XrAction aimPoseAction = XR_NULL_HANDLE;
			XrAction triggerAction = XR_NULL_HANDLE;
			XrAction squeezeAction = XR_NULL_HANDLE;
			XrAction thumbstickAction = XR_NULL_HANDLE;
			XrAction thumbstickClickAction = XR_NULL_HANDLE;
			XrAction primaryAction = XR_NULL_HANDLE;
			XrAction secondaryAction = XR_NULL_HANDLE;
			XrAction menuAction = XR_NULL_HANDLE;
			XrAction hapticAction = XR_NULL_HANDLE;
			XrPath handPaths[2]{};
			XrSpace gripSpaces[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
			XrSpace aimSpaces[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};

			client::VRFrameState frameState;

			PFN_xrCreateVulkanInstanceKHR pfnCreateVulkanInstance = nullptr;
			PFN_xrCreateVulkanDeviceKHR pfnCreateVulkanDevice = nullptr;
			PFN_xrGetVulkanGraphicsDevice2KHR pfnGetVulkanGraphicsDevice = nullptr;
			PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanGraphicsRequirements = nullptr;
			PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevel = nullptr;
		};

		/** The live headset, typed; the same object as client::GetVRSystem(). */
		OpenXRSystem* GetOpenXRSystem();
		void SetOpenXRSystem(OpenXRSystem*);
	} // namespace vr
} // namespace spades

#endif // USE_OPENXR
